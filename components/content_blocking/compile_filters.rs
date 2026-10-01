// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
// Standalone public filter-data compiler. No browser implementation dependency.
use adblock::{Engine, lists::{FilterSet, ParseOptions}, resources::PermissionMask};
use std::{env, fs, io};

fn main() -> io::Result<()> {
    let args: Vec<_> = env::args_os().skip(1).collect();
    if args.len() != 3 {
        return Err(io::Error::new(io::ErrorKind::InvalidInput,
            "Usage: yee_compile_filters BUNDLED_FILTERS TRUSTED_FILTERS OUTPUT"));
    }
    let mut filters = FilterSet::new(false);
    filters.add_filter_list(fs::read_to_string(&args[0])?, ParseOptions::default());
    filters.add_filter_list(fs::read_to_string(&args[1])?, ParseOptions {
        permissions: PermissionMask::from_bits(1), ..ParseOptions::default()
    });
    fs::write(&args[2], Engine::new_with_filter_set(filters).serialize())
}
