#!/usr/bin/env python3
"""표시 이름과 독립적인 제품 식별자를 검증하고 플랫폼 빌드 입력을 생성한다."""

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re
import tempfile
import uuid


REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = REPO_ROOT / "branding/identity.json"


@dataclass(frozen=True)
class WindowsIdentity:
    application_name: str
    app_id: str
    browser_prog_id_prefix: str
    pdf_prog_id_prefix: str
    direct_launch_scheme: str
    active_setup_guid: str
    toast_activator_clsid: str
    elevator_clsid: str
    tracing_service_clsid: str


@dataclass(frozen=True)
class ProductIdentity:
    product_id: str
    profile_directory: str
    windows: WindowsIdentity

    @property
    def windows_policy_key(self):
        return "SOFTWARE\\Policies\\" + self.profile_directory

    @property
    def sandbox_sid_prefix(self):
        # Chromium과 다른 여섯 subauthority를 고정하고 마지막 값은 native가 붙인다.
        digest = hashlib.sha256(("yee-sandbox:" + self.product_id).encode()).digest()
        values = [str(int.from_bytes(digest[i:i + 4], "little")) for i in range(0, 24, 4)]
        return "S-1-15-2-" + "-".join(values) + "-"


def load_identity(config_path=CONFIG_PATH):
    raw = json.loads(Path(config_path).read_text(encoding="utf-8"))
    if (not isinstance(raw, dict)
            or set(raw) != {"schema_version", "product_id", "profile_directory", "windows"}
            or type(raw["schema_version"]) is not int or raw["schema_version"] != 1):
        raise ValueError("Unsupported identity.json schema")
    for key in ("product_id", "profile_directory"):
        value = raw[key]
        if (not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,39}", value)
                or value.lower() in {"chromium", "chrome", "google", "con", "nul", "prn", "aux"}
                or re.fullmatch(r"(?i)(com|lpt)[1-9]", value)):
            raise ValueError(f"Invalid or reserved {key}")
    windows = raw["windows"]
    if not isinstance(windows, dict) or set(windows) != set(WindowsIdentity.__dataclass_fields__):
        raise ValueError("Invalid Windows identity fields")
    for key, maximum in (("application_name", 40), ("app_id", 40),
                         ("browser_prog_id_prefix", 11), ("pdf_prog_id_prefix", 11)):
        value = windows[key]
        if (not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9.]*", value)
                or len(value) > maximum or value.lower().startswith(("chromium", "chrome", "google"))):
            raise ValueError(f"Invalid or reserved Windows {key}")
    scheme = windows["direct_launch_scheme"]
    if not isinstance(scheme, str) or not re.fullmatch(r"yee-[a-z0-9-]+", scheme):
        raise ValueError("Windows direct launch scheme must use the yee- namespace")
    guids = []
    for key in ("active_setup_guid", "toast_activator_clsid", "elevator_clsid", "tracing_service_clsid"):
        value = windows[key]
        if not isinstance(value, str) or not re.fullmatch(r"\{[0-9A-F-]{36}\}", value):
            raise ValueError(f"Invalid Windows {key}")
        parsed = uuid.UUID(value)
        if parsed.int == 0:
            raise ValueError(f"Empty Windows {key}")
        guids.append(parsed)
    if len(set(guids)) != len(guids):
        raise ValueError("Windows registration GUIDs must be distinct")
    reserved = {
        uuid.UUID(value) for value in (
            "7D2B3E1D-D096-4594-9D8F-A6667F12E0AC",
            "635EFA6F-08D6-4EC9-BD14-8A0FDE975159",
            "D133B120-6DB4-4D6B-8BFE-83BF8CA1B1B0",
            "83F69367-442D-447F-8BCC-0E3F97BE9CF2",
        )
    }
    if reserved.intersection(guids):
        raise ValueError("Windows registration GUIDs must not reuse Chromium identity")
    if windows["browser_prog_id_prefix"].lower() == windows["pdf_prog_id_prefix"].lower():
        raise ValueError("Browser and PDF ProgIDs must be distinct")
    return ProductIdentity(raw["product_id"], raw["profile_directory"], WindowsIdentity(**windows))


def guid_initializer(value):
    parsed = uuid.UUID(value)
    data = parsed.bytes
    return (f"{{0x{parsed.time_low:08X}, 0x{parsed.time_mid:04X}, 0x{parsed.time_hi_version:04X}, {{"
            + ", ".join(f"0x{part:02X}" for part in data[8:]) + "}}")


def generated_inputs(identity):
    windows = identity.windows
    header = [
        "// branding/identity.json에서 생성. 표시 이름 변경 시 이 식별자는 유지한다.",
        "#ifndef CHROME_APP_YEE_PRODUCT_IDENTITY_H_",
        "#define CHROME_APP_YEE_PRODUCT_IDENTITY_H_",
        "#include <guiddef.h>",
        "namespace yee::identity {",
    ]
    strings = {
        "ProfileDirectory": identity.profile_directory,
        "ApplicationName": windows.application_name,
        "AppId": windows.app_id,
        "BrowserProgIdPrefix": windows.browser_prog_id_prefix,
        "PdfProgIdPrefix": windows.pdf_prog_id_prefix,
        "ActiveSetupGuid": windows.active_setup_guid,
        "SandboxSidPrefix": identity.sandbox_sid_prefix,
    }
    for name, value in strings.items():
        header.append(f"inline constexpr wchar_t k{name}[] = L{json.dumps(value)};")
    header.append(f"inline constexpr char kDirectLaunchScheme[] = {json.dumps(windows.direct_launch_scheme)};")
    for name, value in (("ToastActivatorClsid", windows.toast_activator_clsid),
                        ("ElevatorClsid", windows.elevator_clsid),
                        ("TracingServiceClsid", windows.tracing_service_clsid)):
        header.append(f"inline constexpr GUID k{name} = {guid_initializer(value)};")
    header += ["}  // namespace yee::identity", "#endif", ""]
    return {
        "chrome/app/yee_product_identity.h": "\n".join(header),
        "chrome/app/yee_product_identity.gni": (
            "# branding/identity.json에서 생성.\n"
            f"yee_windows_policy_key = {json.dumps(identity.windows_policy_key)}\n"),
        "chrome/app/yee_windows_registration.h": (
            "// branding/identity.json에서 생성. CRT 없이 쓰는 설치 등록 문자열이다.\n"
            "#ifndef CHROME_APP_YEE_WINDOWS_REGISTRATION_H_\n"
            "#define CHROME_APP_YEE_WINDOWS_REGISTRATION_H_\n"
            '#define YEE_WINDOWS_PRODUCT_REGISTRY_KEY L'
            + json.dumps("Software\\" + identity.profile_directory) + "\n"
            '#define YEE_WINDOWS_REGISTRY_APPLICATION_NAME L'
            + json.dumps(windows.app_id + ".exe") + "\n"
            "#endif\n"),
    }


def install_identity(chromium_src, identity, check_only=False):
    root = Path(chromium_src)
    if not root.is_absolute() or not (root / "chrome/install_static/chromium_install_modes.h").is_file():
        raise ValueError("Expected an absolute Chromium source directory")
    plan = generated_inputs(identity)
    # 다른 overlay 입력과 마찬가지로 쓰기 전에 모든 출력 경로를 검사한다.
    for name in plan:
        path = root / name
        for ancestor in (path, *path.parents):
            if ancestor == root:
                break
            try:
                attributes = ancestor.lstat()
            except FileNotFoundError:
                continue
            if ancestor.is_symlink() or getattr(attributes, "st_file_attributes", 0) & 0x400:
                raise ValueError(f"Identity output traverses a link: {path}")
            if (ancestor == path and ancestor.is_dir()) or (ancestor != path and not ancestor.is_dir()):
                raise ValueError(f"Identity output has a file/directory conflict: {path}")
    for name, content in plan.items():
        path = root / name
        data = content.encode("utf-8")
        if path.is_file() and path.read_bytes() == data:
            continue
        if not check_only:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
                    temporary = Path(output.name)
                    output.write(data)
                temporary.replace(path)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        print(f"{'Would install' if check_only else 'Installed'}: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("show", "install"))
    parser.add_argument("chromium_src", nargs="?", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        identity = load_identity()
        if args.command == "show":
            print(json.dumps(asdict(identity)))
        else:
            if args.chromium_src is None:
                parser.error("install requires chromium_src")
            install_identity(args.chromium_src, identity, args.check)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
