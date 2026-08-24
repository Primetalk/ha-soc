#!/usr/bin/env python3
"""Deterministic clean-checkout checks for the canonical ESPHome project."""

from __future__ import annotations

import base64
import binascii
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_PACKAGES = {
    "packages/battery-config.yaml",
    "packages/connectivity.yaml",
    "packages/measurement.yaml",
    "packages/soc.yaml",
    "packages/soc-rules.yaml",
    "packages/display.yaml",
}

REQUIRED_PATHS = {
    ".gitignore",
    ".github/workflows/esphome.yaml",
    "battery-monitor.yaml",
    "secrets.example.yaml",
    "include/battery_monitor_types.h",
    "assets/fonts/RobotoMono-Variable.ttf",
    "assets/fonts/OFL.txt",
    "docs/home-assistant.md",
    "docs/commissioning.md",
    "tests/battery_monitor_helpers_test.cpp",
    "tests/validate_repository.py",
}

CANONICAL_TEXT_PATHS = {
    "battery-monitor.yaml",
    "include/battery_monitor_types.h",
    *EXPECTED_PACKAGES,
}

FORBIDDEN_CANONICAL_IDENTIFIERS = {
    "litime",
    "litime_solar_mppt.h",
    "ble_client",
    "esp32_ble_tracker",
}

EXPECTED_SECRET_VALUES = {
    "wifi_ssid": "replace-with-wifi-ssid",
    "wifi_password": "replace-with-wifi-password",
    "fallback_ap_password": "replace-me-1234",
    "api_encryption_key": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
    "ota_password": "replace-with-a-strong-ota-password",
}

EXPECTED_SECRET_REFERENCES = {
    "ssid": "wifi_ssid",
    "password": "wifi_password",
    "ap_password": "fallback_ap_password",
    "api_key": "api_encryption_key",
    "ota_password": "ota_password",
}


class Checks:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.failures.append(message)

    def finish(self) -> int:
        if self.failures:
            for failure in self.failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            print(
                f"Repository validation failed with {len(self.failures)} issue(s)",
                file=sys.stderr,
            )
            return 1
        print("All deterministic repository checks passed")
        return 0


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def parse_simple_quoted_yaml(relative_path: str) -> dict[str, str]:
    values: dict[str, str] = {}
    line_pattern = re.compile(r'^([a-zA-Z0-9_]+):\s*"([^"]*)"\s*$')
    for line in read_text(relative_path).splitlines():
        match = line_pattern.match(line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def check_required_paths(checks: Checks) -> None:
    for relative_path in sorted(REQUIRED_PATHS | EXPECTED_PACKAGES):
        path = ROOT / relative_path
        checks.require(path.is_file(), f"required file is absent: {relative_path}")
        if path.is_file():
            checks.require(path.stat().st_size > 0, f"required file is empty: {relative_path}")


def check_canonical_composition(checks: Checks) -> None:
    entry = read_text("battery-monitor.yaml")
    package_paths = set(
        re.findall(r"^\s+[a-zA-Z0-9_]+:\s*!include\s+([^\s#]+)\s*$", entry, re.MULTILINE)
    )
    checks.require(
        package_paths == EXPECTED_PACKAGES,
        "canonical package set differs from the six approved packages: "
        f"found {sorted(package_paths)}",
    )

    local_includes = set(
        re.findall(r"^\s+-\s+(include/[^\s#]+)\s*$", entry, re.MULTILINE)
    )
    checks.require(
        local_includes == {"include/battery_monitor_types.h"},
        f"unexpected canonical C++ include set: {sorted(local_includes)}",
    )
    for relative_path in sorted(package_paths | local_includes):
        checks.require(
            (ROOT / relative_path).is_file(),
            f"canonical configuration references an absent local file: {relative_path}",
        )

    font_paths: set[str] = set()
    for package_path in package_paths:
        font_paths.update(
            re.findall(
                r"^\s+-?\s*file:\s*([^\s#]+)\s*$",
                read_text(package_path),
                re.MULTILINE,
            )
        )
    checks.require(bool(font_paths), "canonical configuration contains no local font reference")
    for relative_path in sorted(font_paths):
        checks.require(
            (ROOT / relative_path).is_file(),
            f"canonical configuration references an absent font: {relative_path}",
        )


def check_secrets_fixture(checks: Checks) -> None:
    values = parse_simple_quoted_yaml("secrets.example.yaml")
    checks.require(
        values == EXPECTED_SECRET_VALUES,
        "secrets.example.yaml must contain only the approved valid-shaped placeholders",
    )

    api_key = values.get("api_encryption_key", "")
    try:
        decoded_key = base64.b64decode(api_key, validate=True)
    except (binascii.Error, ValueError):
        decoded_key = b""
    checks.require(
        len(decoded_key) == 32,
        "example API encryption key must be valid base64 encoding exactly 32 bytes",
    )

    connectivity = read_text("packages/connectivity.yaml")
    required_lines = {
        "ssid": r"^\s+ssid:\s*!secret\s+wifi_ssid\s*$",
        "password": r"^\s+password:\s*!secret\s+wifi_password\s*$",
        "ap_password": r"^\s+password:\s*!secret\s+fallback_ap_password\s*$",
        "api_key": r"^\s+key:\s*!secret\s+api_encryption_key\s*$",
        "ota_password": r"^\s+password:\s*!secret\s+ota_password\s*$",
    }
    for name, pattern in required_lines.items():
        checks.require(
            re.search(pattern, connectivity, re.MULTILINE) is not None,
            f"connectivity package does not use the expected !secret for {name}: "
            f"{EXPECTED_SECRET_REFERENCES[name]}",
        )


def check_yaml_credentials(checks: Checks) -> None:
    expected_secret_names = {
        "wifi_ssid",
        "wifi_password",
        "fallback_ap_password",
        "api_encryption_key",
        "ota_password",
    }
    credential_line = re.compile(r"^\s*(ssid|password|key):\s*(.*?)\s*$")
    secret_reference = re.compile(r"^!secret\s+([a-zA-Z0-9_]+)$")

    for path in sorted(ROOT.glob("*.yaml")) + sorted((ROOT / "packages").glob("*.yaml")):
        relative_path = path.relative_to(ROOT).as_posix()
        if relative_path in {"secrets.example.yaml", "secrets.yaml"}:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = credential_line.match(line)
            if match is None:
                continue
            field, value = match.groups()
            value = value.split("#", 1)[0].strip()
            scalar = value
            if len(scalar) >= 2 and scalar[0] == scalar[-1] and scalar[0] in {'"', "'"}:
                scalar = scalar[1:-1]

            # Empty `ssid:` declarations belong to wifi_info text sensors, and
            # substitutions/literals are acceptable for a node's fallback AP name.
            if field == "ssid" and (
                not scalar
                or scalar.startswith("${")
                or "Fallback Hotspot" in scalar
            ):
                continue

            reference = secret_reference.match(value)
            checks.require(
                reference is not None and reference.group(1) in expected_secret_names,
                f"credential-like field must use an approved !secret reference: "
                f"{relative_path}:{line_number}",
            )


def check_canonical_identifiers(checks: Checks) -> None:
    for relative_path in sorted(CANONICAL_TEXT_PATHS):
        text = read_text(relative_path).lower()
        for identifier in sorted(FORBIDDEN_CANONICAL_IDENTIFIERS):
            checks.require(
                identifier not in text,
                f"obsolete identifier {identifier!r} appears in canonical source {relative_path}",
            )


def tracked_paths(checks: Checks) -> set[str]:
    try:
        result = subprocess.run(
            ["git", "ls-files", "-z"],
            cwd=ROOT,
            check=True,
            capture_output=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        checks.failures.append(f"could not inspect tracked files with git: {error}")
        return set()
    return {
        path.decode("utf-8")
        for path in result.stdout.split(b"\0")
        if path
    }


def check_tracked_artifacts(checks: Checks) -> None:
    tracked = tracked_paths(checks)
    checks.require("secrets.yaml" not in tracked, "private secrets.yaml is tracked")

    forbidden_roots = {".esphome", "build", ".pio", ".pioenvs", ".piolibdeps", ".venv"}
    for relative_path in sorted(tracked):
        root = Path(relative_path).parts[0]
        checks.require(
            root not in forbidden_roots,
            f"generated/local artifact is tracked: {relative_path}",
        )


def check_ignore_policy(checks: Checks) -> None:
    probes = {
        "secrets.yaml": "private ESPHome credentials",
        ".esphome/probe": "ESPHome generated output",
        "build/probe": "generic build output",
        ".pio/probe": "PlatformIO generated output",
        ".venv/probe": "local Python virtual environment",
        ".vscode/settings.json": "VS Code metadata",
        ".idea/workspace.xml": "IDE metadata",
        ".DS_Store": "macOS metadata",
        "probe.pyc": "Python bytecode",
    }
    for probe, purpose in probes.items():
        result = subprocess.run(
            ["git", "check-ignore", "--no-index", "--quiet", probe],
            cwd=ROOT,
            check=False,
        )
        checks.require(
            result.returncode == 0,
            f".gitignore does not cover {purpose}: {probe}",
        )


def check_markdown_fences(checks: Checks) -> None:
    for relative_path in ("README.md", "docs/home-assistant.md", "docs/commissioning.md"):
        fence_count = sum(
            1 for line in read_text(relative_path).splitlines() if line.startswith("```")
        )
        checks.require(
            fence_count % 2 == 0,
            f"Markdown code fences are unbalanced in {relative_path}",
        )


def main() -> int:
    checks = Checks()
    check_required_paths(checks)
    check_canonical_composition(checks)
    check_secrets_fixture(checks)
    check_yaml_credentials(checks)
    check_canonical_identifiers(checks)
    check_tracked_artifacts(checks)
    check_ignore_policy(checks)
    check_markdown_fences(checks)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
