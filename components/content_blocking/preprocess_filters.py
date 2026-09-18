#!/usr/bin/env python3
# Copyright 2026 The Yee Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Select desktop Chromium filter branches before passing data to the engine.

The original pinned inputs stay intact for attribution and source packaging.
Unknown conditions retain both branches, matching the Brave desktop input
contract; nested false parents and malformed directives are handled explicitly.
"""
CONDITIONS = {
    "ext_ublock": True, "ext_devbuild": False, "env_devbuild": False,
    "env_chromium": True, "env_edge": False, "env_firefox": False,
    "env_mobile": False, "env_mv3": False,
    "env_legacy": False, "env_safari": False, "cap_html_filtering": False,
    "cap_user_stylesheet": True, "false": False, "ext_abp": False,
    "adguard": False, "adguard_app_android": False, "adguard_app_ios": False,
    "adguard_app_mac": False, "adguard_app_windows": False,
    "adguard_ext_android_cb": False, "adguard_ext_chromium": True,
    "adguard_ext_edge": False, "adguard_ext_firefox": False,
    "adguard_ext_opera": True, "adguard_ext_safari": False,
}


def preprocess(text, source="filters"):
    stack, output = [], []
    active = True
    for number, line in enumerate(text.splitlines(), 1):
        directive = line.strip()
        if directive.startswith("!#if "):
            name = directive[5:].strip()
            negate = name.startswith("!")
            condition = CONDITIONS.get(name[1:] if negate else name)
            if condition is not None and negate:
                condition = not condition
            stack.append([active, condition, False])
            active = active and condition is not False
        elif directive == "!#else":
            if not stack or stack[-1][2]:
                raise ValueError(f"{source}:{number}: unexpected !#else")
            parent, condition, _ = stack[-1]
            stack[-1][2] = True
            active = parent and condition is not True
        elif directive == "!#endif":
            if not stack:
                raise ValueError(f"{source}:{number}: unexpected !#endif")
            active = stack.pop()[0]
        elif directive.startswith(("!#if", "!#else", "!#endif")):
            raise ValueError(f"{source}:{number}: malformed conditional directive")
        elif active:
            output.append(line)
    if stack:
        raise ValueError(f"{source}: unterminated !#if")
    return "\n".join(output) + "\n"
