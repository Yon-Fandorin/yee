#!/usr/bin/env python3
"""Read shared product branding and install its build inputs without building."""

import argparse
from dataclasses import dataclass
from html import escape
import json
from pathlib import Path
import re
import tempfile


OVERLAY_ROOT = Path(__file__).resolve().parent
REPO_ROOT = OVERLAY_ROOT.parents[1]
CONFIG_PATH = REPO_ROOT / "branding/brand.json"
BRANDING_MARKER = "OVERLAY_BRANDING_MANAGED=1"
LEGACY_BRANDING_MARKER = "# Product names are managed by the Yee overlay brand installer."


@dataclass(frozen=True)
class Brand:
    name: str
    short_name: str
    logo_source: Path
    logo_crop_size: int
    provisional: bool = True

    @property
    def product_values(self):
        return {
            "PRODUCT_FULLNAME": self.name,
            "PRODUCT_SHORTNAME": self.short_name,
            "PRODUCT_INSTALLER_FULLNAME": self.name + " Installer",
            "PRODUCT_INSTALLER_SHORTNAME": self.short_name + " Installer",
        }

    def metadata(self):
        return {
            "name": self.name,
            "short_name": self.short_name,
            "logo_source": str(self.logo_source),
            "logo_crop_size": self.logo_crop_size,
            "provisional": self.provisional,
        }


def load_brand(config_path=CONFIG_PATH, repo_root=REPO_ROOT, *, validate_assets=True):
    config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("brand.json must contain an object")
    if set(config) - {"name", "short_name", "logo_source", "logo_crop_size", "provisional"}:
        raise ValueError("brand.json contains an unknown field")
    name = config.get("name")
    short_name = config.get("short_name", name)
    for field, value in (("name", name), ("short_name", short_name)):
        if (not isinstance(value, str) or not value or value != value.strip()
                or any(ord(c) < 32 or ord(c) == 127 for c in value)
                or re.search(r'[\\/:*?"<>|]', value) or value.endswith(".")):
            raise ValueError(f"{field} must be a non-empty, portable app name")
        if "$" in value or re.search(r"@[A-Za-z0-9_]+@", value):
            raise ValueError(f"{field} must not contain GN interpolation or version placeholders")
    provisional = config.get("provisional", True)
    if type(provisional) is not bool:
        raise ValueError("provisional must be a boolean")
    logo = config.get("logo_source")
    if not isinstance(logo, str) or not logo or Path(logo).is_absolute():
        raise ValueError("logo_source must be a repository-relative file path")
    root = Path(repo_root).resolve()
    logo_source = (root / logo).resolve()
    if not logo_source.is_relative_to(root) or (validate_assets and not logo_source.is_file()):
        raise ValueError("logo_source must identify a file inside the repository")
    crop_size = config.get("logo_crop_size")
    if type(crop_size) is not int or crop_size <= 0:
        raise ValueError("logo_crop_size must be a positive integer")
    return Brand(name, short_name, logo_source, crop_size, provisional)


def product_parts(brand):
    def message(identifier, value):
        return (f'  <message name="{identifier}" desc="Configured product name." '
                f'translateable="false">{escape(value)}</message>\n')

    def part(messages):
        return ('<?xml version="1.0" encoding="utf-8"?>\n'
                '<!-- Generated from branding/brand.json. -->\n'
                '<grit-part>\n' + messages + '</grit-part>\n')

    return {
        "yee_product_names.grdp": part(
            message("IDS_PRODUCT_NAME", brand.name)
            + message("IDS_SHORT_PRODUCT_NAME", brand.short_name)),
        "yee_app_menu_name.grdp": part(
            message("IDS_APP_MENU_PRODUCT_NAME", brand.short_name)),
    }


def branding_plan(chromium_src, brand, check_only=False):
    app_dir = Path(chromium_src) / "chrome/app"
    branding_path = app_dir / "theme/chromium/BRANDING"
    # Preserve upstream company/copyright, install identity, and extra fields.
    text = branding_path.read_bytes().decode("utf-8")
    # version.py treats every line as KEY=VALUE; it does not accept comments.
    text = re.sub(rf"(?m)^{re.escape(LEGACY_BRANDING_MARKER)}\r?\n", "", text)
    if any("=" not in line for line in text.splitlines()):
        raise ValueError("BRANDING requires KEY=VALUE on every line for Chromium version.py")
    if BRANDING_MARKER not in text.splitlines() and not check_only:
        raise ValueError("apply the 0002 branding patch before installing names")
    for key, value in brand.product_values.items():
        pattern = rf"(?m)^{key}=[^\r\n]*"
        text, count = re.subn(pattern, lambda _: key + "=" + value, text)
        if count != 1:
            raise ValueError(f"BRANDING must contain exactly one {key}")
    return {branding_path: text, **{
        app_dir / filename: text
        for filename, text in product_parts(brand).items()
    }}


def install_brand(chromium_src, brand, check_only=False):
    plan = branding_plan(chromium_src, brand, check_only)
    for path, text in plan.items():
        data = text.encode("utf-8")
        if path.is_file() and path.read_bytes() == data:
            continue
        if not check_only:
            # Avoid partially written inputs and needless GN/GRIT rebuilds.
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as out:
                    temporary = Path(out.name)
                    out.write(data)
                temporary.chmod(0o644)
                temporary.replace(path)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        print(f"{'Would install' if check_only else 'Installed'}: {path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    show = commands.add_parser("show", help="read the validated brand configuration")
    show.add_argument("--get", choices=("name", "short_name", "logo_source", "logo_crop_size", "provisional"))
    show.add_argument("--format", choices=("json", "lines"), default="json")
    install = commands.add_parser("install", help="install branding inputs; does not build")
    install.add_argument("chromium_src", type=Path)
    install.add_argument("--check", action="store_true", help="preview without writing")
    args = parser.parse_args()
    try:
        brand = load_brand(validate_assets=not (
            args.command == "show" and args.get in ("name", "short_name", "provisional")))
        if args.command == "install":
            if not args.chromium_src.is_absolute():
                raise ValueError("Chromium src path must be absolute")
            install_brand(args.chromium_src, brand, args.check)
        elif args.get:
            print(brand.metadata()[args.get])
        elif args.format == "lines":
            print("\n".join(str(v) for v in brand.metadata().values()))
        else:
            print(json.dumps(brand.metadata(), ensure_ascii=False))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
