// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use adblock::{Engine, lists::ParseOptions, request::Request};
use adblock::resources::{InMemoryResourceStorage, PermissionMask, Resource};
use std::collections::{HashMap, HashSet};

#[cxx::bridge(namespace = "yee::content_blocking")]
mod ffi {
    struct DocumentRules {
        selectors: Vec<String>,
        exceptions: Vec<String>,
        script: String,
        generic_hide: bool,
    }
    struct EngineDecision {
        blocked: bool,
        replacement: String,
        rewritten_url: String,
    }

    extern "Rust" {
        type FilterEngine;
        fn new_engine(filters: &str, resources: &str, trusted_filters: &str, community_resources: &str) -> Box<FilterEngine>;
        fn community_resources_valid(resources: &str, bundled: &str) -> bool;
        fn resources_valid(self: &FilterEngine) -> bool;
        fn check(self: &FilterEngine, url: &str, source: &str, kind: &str, method: &str) -> bool;
        fn evaluate(self: &FilterEngine, url: &str, source: &str, kind: &str, method: &str) -> EngineDecision;
        fn csp_directives(
            self: &FilterEngine,
            url: &str,
            source: &str,
            kind: &str,
            method: &str,
        ) -> String;
        fn document_rules(self: &FilterEngine, url: &str) -> DocumentRules;
        fn generic_selectors(
            self: &FilterEngine,
            classes: Vec<String>,
            ids: Vec<String>,
            exceptions: Vec<String>,
        ) -> Vec<String>;
    }
}

pub struct FilterEngine(Engine, bool);

// Original scriptlets are external JavaScript resources, not Rust dependencies.
// Only the separately supplied community list receives uBO trusted permission.
pub fn new_engine(filters: &str, resources: &str, trusted_filters: &str, community_resources: &str) -> Box<FilterEngine> {
    let mut set = adblock::lists::FilterSet::new(false);
    set.add_filter_list(filters.to_owned(), ParseOptions::default());
    if !trusted_filters.is_empty() {
        set.add_filter_list(trusted_filters.to_owned(), ParseOptions {
            permissions: PermissionMask::from_bits(1),
            ..ParseOptions::default()
        });
    }
    let mut engine = Engine::new_with_filter_set(set);
    let storage = resource_storage(resources, community_resources);
    let valid = storage.is_some();
    if let Some(storage) = storage { engine.use_resource_storage(storage); }
    Box::new(FilterEngine(engine, valid))
}

pub fn community_resources_valid(resources: &str, bundled: &str) -> bool {
    resource_storage(bundled, resources).is_some()
}

fn resource_storage(bundled: &str, community: &str) -> Option<InMemoryResourceStorage> {
    let parse = |text: &str| -> Option<Vec<Resource>> {
        if text.is_empty() { Some(Vec::new()) }
        else { serde_json::from_str(text).ok() }
    };
    let mut resources = parse(community)?;
    let community_identifiers: HashSet<String> = resources.iter()
        .flat_map(|r| std::iter::once(&r.name).chain(r.aliases.iter())).cloned().collect();
    // Original redirect aliases take precedence over Yee's fallback aliases.
    // Canonical-name conflicts remain an error, as do duplicates within a pack.
    for mut resource in parse(bundled)? {
        resource.aliases.retain(|name| !community_identifiers.contains(name));
        resources.push(resource);
    }
    if resources.len() > 4096 { return None; }
    let mut storage = InMemoryResourceStorage::default();
    let mut names = HashSet::new();
    let mut dependencies = HashMap::new();
    let mut identifiers = HashSet::new();
    for resource in &resources {
        names.insert(resource.name.clone());
        dependencies.insert(resource.name.clone(), resource.dependencies.clone());
        for name in std::iter::once(&resource.name).chain(resource.aliases.iter()) {
            if !identifiers.insert(name.clone()) { return None; }
        }
    }
    let mut resolved = HashSet::new();
    let mut depths = HashMap::new();
    loop {
        let before = resolved.len();
        for (name, deps) in &dependencies {
            if !resolved.contains(name) && deps.iter().all(|dep| names.contains(dep) && resolved.contains(dep)) {
                let depth = 1 + deps.iter().map(|dep| depths[dep]).max().unwrap_or(0);
                // Upstream expands dependencies recursively when injecting.
                if depth > 128 { return None; }
                depths.insert(name.clone(), depth);
                resolved.insert(name.clone());
            }
        }
        if resolved.len() == before { break; }
    }
    if resolved.len() != names.len() { return None; }
    for resource in resources { storage.add_resource(resource).ok()?; }
    Some(storage)
}

impl FilterEngine {
    fn resources_valid(&self) -> bool {
        self.1
    }
    fn check(&self, url: &str, source: &str, kind: &str, method: &str) -> bool {
        self.evaluate(url, source, kind, method).blocked
    }

    fn evaluate(&self, url: &str, source: &str, kind: &str, method: &str) -> ffi::EngineDecision {
        let Ok(request) = Request::new(url, source, kind, method) else {
            return ffi::EngineDecision { blocked: false, replacement: String::new(), rewritten_url: String::new() };
        };
        let result = self.0.check_network_request(&request);
        let blocked = result.should_block();
        ffi::EngineDecision {
            blocked,
            replacement: if blocked { result.redirect.unwrap_or_default() } else { String::new() },
            rewritten_url: if !blocked { result.rewritten_url.unwrap_or_default() } else { String::new() },
        }
    }

    fn document_rules(&self, url: &str) -> ffi::DocumentRules {
        let rules = self.0.url_cosmetic_resources(url);
        ffi::DocumentRules {
            selectors: rules.hide_selectors.into_iter().collect(),
            exceptions: rules.exceptions.into_iter().collect(),
            script: rules.injected_script,
            generic_hide: !rules.generichide,
        }
    }

    fn csp_directives(&self, url: &str, source: &str, kind: &str, method: &str) -> String {
        let Ok(request) = Request::new(url, source, kind, method) else {
            return String::new();
        };
        self.0.get_csp_directives(&request).unwrap_or_default()
    }

    fn generic_selectors(
        &self,
        classes: Vec<String>,
        ids: Vec<String>,
        exceptions: Vec<String>,
    ) -> Vec<String> {
        self.0
            .hidden_class_id_selectors(&classes, &ids, &exceptions.into_iter().collect())
    }
}
