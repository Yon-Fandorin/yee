// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use adblock::{Engine, lists::ParseOptions, request::Request};
use adblock::resources::{InMemoryResourceStorage, PermissionMask, Resource};
use std::collections::{HashMap, HashSet};

#[cxx::bridge(namespace = "yee::content_blocking")]
mod ffi {
    struct CatalogRuleSelection {
        valid: bool,
        rules: String,
        omitted_rules: i32,
        omitted_examples: Vec<String>,
    }
    struct DocumentRules {
        selectors: Vec<String>,
        procedural_actions: Vec<String>,
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
        fn select_catalog_rules(rules: &str, bundled: &str, community: &str, trusted: bool) -> CatalogRuleSelection;
        fn baseline_rules_valid(rules: &str) -> bool;
        fn compile_baseline_rules(rules: &str, trusted_rules: &str) -> Vec<u8>;
        type FilterEngine;
        fn new_engine(filters: &str, resources: &str, trusted_filters: &str, community_resources: &str, compiled_filters: &[u8]) -> Box<FilterEngine>;
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

// 엔진의 실제 파서와 설치된 리소스 저장소로 규칙을 검사한다. 미지원 옵션을 삭제해 차단 범위를 넓히지 않는다.
fn select_catalog_rules(
    rules: &str,
    bundled: &str,
    community: &str,
    trusted: bool,
) -> ffi::CatalogRuleSelection {
    use adblock::filters::cosmetic::{
        CosmeticFilterError, CosmeticFilterMask, CosmeticFilterOperator,
    };
    use adblock::filters::network::NetworkFilterError;
    use adblock::lists::{FilterParseError, ParsedLine};
    let mut result = ffi::CatalogRuleSelection {
        valid: false,
        rules: String::new(),
        omitted_rules: 0,
        omitted_examples: Vec::new(),
    };
    let Some(backend) = resource_storage(bundled, community) else {
        return result;
    };
    let storage = adblock::resources::ResourceStorage::from_backend(backend);
    let permission = PermissionMask::from_bits(u8::from(trusted));
    let options = ParseOptions {
        permissions: permission,
        ..ParseOptions::default()
    };
    let mut total = 0usize;
    let mut parsed = 0usize;
    let mut unsupported = 0usize;
    for line in rules.lines().map(str::trim) {
        if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
            continue;
        }
        total += 1;
        let compatible = match adblock::lists::parse_filter(line, false, options) {
            Ok(ParsedLine::Cosmetic(filter)) => {
                parsed += 1;
                if filter.mask.contains(CosmeticFilterMask::UNHIDE) {
                    // 예외는 코드를 실행하지 않는다. 낮은 권한 목록도 상위 목록의 주입을 제외할 수 있다.
                    true
                } else if filter.mask.contains(CosmeticFilterMask::SCRIPT_INJECT) {
                    match filter.selector.first() {
                        Some(CosmeticFilterOperator::CssSelector(args)) => {
                            args.is_empty()
                                || !storage
                                    .get_scriptlet_resources([(args.as_str(), permission)])
                                    .is_empty()
                        }
                        _ => false,
                    }
                } else {
                    true
                }
            }
            Ok(ParsedLine::Network(filter)) => {
                parsed += 1;
                if filter.is_redirect() {
                    filter.modifier_option.is_some_and(|name| {
                        let canonical = name
                            .rsplit_once(':')
                            .filter(|(_, priority)| priority.parse::<i32>().is_ok())
                            .map_or(name, |(name, _)| name);
                        storage.get_redirect_resource(canonical).is_some()
                    })
                } else {
                    true
                }
            }
            Err(error) => {
                // 지원 범위 밖의 문법은 손상된 입력과 구분하고, 해당 규칙 전체를 제외한다.
                if matches!(
                    error,
                    FilterParseError::Network(
                        NetworkFilterError::UnrecognisedOption
                            | NetworkFilterError::RemoveparamRegexUnsupported
                            | NetworkFilterError::FullRegexUnsupported
                    ) | FilterParseError::Cosmetic(
                        CosmeticFilterError::UnsupportedSyntax
                            | CosmeticFilterError::HtmlFilteringUnsupported
                            | CosmeticFilterError::LocationModifiersUnsupported
                    ) | FilterParseError::Unsupported
                ) {
                    unsupported += 1;
                }
                false
            }
        };
        if compatible {
            result.rules.push_str(line);
            result.rules.push('\n');
        } else {
            result.omitted_rules += 1;
            if result.omitted_examples.len() < 20 {
                result
                    .omitted_examples
                    .push(line.chars().take(256).collect());
            }
        }
    }
    result.valid = total == 0 || (parsed + unsupported) * 100 >= total * 90;
    result
}

fn baseline_rules_valid(rules: &str) -> bool {
    let mut total = 0usize;
    let mut valid = 0usize;
    for line in rules.lines().map(str::trim) {
        if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
            continue;
        }
        total += 1;
        if adblock::lists::parse_filter(line, false, ParseOptions::default()).is_ok() {
            valid += 1;
        }
    }
    // Some official syntax may be unsupported. Reject unusable/error documents
    // without making every future valid list depend on 100% parser coverage.
    valid > 0 && valid * 100 >= total * 90
}

fn compile_baseline_rules(rules: &str, trusted_rules: &str) -> Vec<u8> {
    let mut set = adblock::lists::FilterSet::new(false);
    set.add_filter_list(rules.to_owned(), ParseOptions::default());
    set.add_filter_list(trusted_rules.to_owned(), ParseOptions {
        permissions: PermissionMask::from_bits(1), ..ParseOptions::default()
    });
    Engine::new_with_filter_set(set).serialize()
}

// Original scriptlets are external JavaScript resources, not Rust dependencies.
// Only the separately supplied community list receives uBO trusted permission.
pub fn new_engine(filters: &str, resources: &str, trusted_filters: &str, community_resources: &str, compiled_filters: &[u8]) -> Box<FilterEngine> {
    let mut engine = Engine::default();
    if compiled_filters.is_empty() || engine.deserialize(compiled_filters).is_err() {
        let mut set = adblock::lists::FilterSet::new(false);
        set.add_filter_list(filters.to_owned(), ParseOptions::default());
        if !trusted_filters.is_empty() {
            set.add_filter_list(trusted_filters.to_owned(), ParseOptions {
                permissions: PermissionMask::from_bits(1),
                ..ParseOptions::default()
            });
        }
        engine = Engine::new_with_filter_set(set);
    }
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
    let bundled = parse(bundled)?;
    // uBO's blank MP4 uses H.264, which Yee's Chromium codec configuration
    // cannot decode. Keep its canonical name and aliases, but deliver Yee's
    // independently generated VP9 payload through those same redirects.
    if let Some(blank) = bundled.iter().find(|r| r.name == "yee-blank.mp4") {
        for resource in resources.iter_mut().filter(|r| r.name == "noop-1s.mp4") {
            // Substitution must not conceal an invalid input pack.
            if resource.kind != blank.kind { return None; }
            InMemoryResourceStorage::default().add_resource(resource.clone()).ok()?;
            resource.content.clone_from(&blank.content);
        }
    }
    let community_identifiers: HashSet<String> = resources.iter()
        .flat_map(|r| std::iter::once(&r.name).chain(r.aliases.iter())).cloned().collect();
    // Original redirect aliases take precedence over Yee's fallback aliases.
    // Canonical-name conflicts remain an error, as do duplicates within a pack.
    for mut resource in bundled {
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
            procedural_actions: rules.procedural_actions.into_iter().collect(),
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
