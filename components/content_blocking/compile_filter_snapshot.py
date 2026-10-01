#!/usr/bin/env python3
# Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
"""Compile an optional, generation-bound filter cache with the pinned engine."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def compile_snapshot(compiler, bundled, bundled_generation, trusted, manifest,
                     output, metadata):
    output, metadata = Path(output), Path(metadata)
    pack = json.loads(Path(manifest).read_text())
    trusted_bytes = Path(trusted).read_bytes()
    if hashlib.sha256(trusted_bytes).hexdigest() != pack["rules_sha256"]:
        raise ValueError("Filter cache input checksum mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(Path(compiler).resolve()), str(bundled), str(trusted),
                    str(output)], check=True)
    generation = hashlib.sha256(
        (pack["rules_sha256"] + "\n" + pack["resources_sha256"]).encode()).hexdigest()
    metadata.write_text(json.dumps({
        "schema_version": 1, "engine_file": "YeeCompiledFilters.dat",
        "engine_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "bundled_generation": Path(bundled_generation).read_text(),
        "community_generation": generation,
    }, indent=2) + "\n")


if __name__ == "__main__":
    compile_snapshot(*sys.argv[1:])
