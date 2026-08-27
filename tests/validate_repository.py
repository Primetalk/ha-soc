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
    ".github/workflows/hardware-schematic.yaml",
    "CONTRIBUTING.md",
    "Taskfile.yml",
    "battery-monitor.yaml",
    "secrets.example.yaml",
    "include/battery_monitor_types.h",
    "assets/fonts/RobotoMono-Variable.ttf",
    "assets/fonts/OFL.txt",
    "docs/home-assistant.md",
    "docs/commissioning.md",
    "docs/fuse-selection.md",
    "hardware/battery-monitor-schematic.tex",
    "hardware/battery-monitor-schematic.svg",
    "hardware/battery-system-installation.tex",
    "hardware/battery-system-installation.svg",
    "plans/04-system-installation-diagram-plan.md",
    "scripts/preview-schematics.sh",
    "scripts/render-schematic.sh",
    "tests/battery_monitor_helpers_test.cpp",
    "tests/validate_repository.py",
}

SCHEMATIC_PAIRS = (
    (
        "hardware/battery-monitor-schematic.tex",
        "hardware/battery-monitor-schematic.svg",
    ),
    (
        "hardware/battery-system-installation.tex",
        "hardware/battery-system-installation.svg",
    ),
)

EXPECTED_DETAILED_MODULE_PORTS = {
    "ina-vin-plus",
    "ina-vin-minus",
    "ina-3v3",
    "ina-gnd",
    "ina-sda",
    "ina-scl",
    "esp-3v3",
    "esp-gnd",
    "esp-sda",
    "esp-scl",
    "oled-3v3",
    "oled-gnd",
    "oled-sda",
    "oled-scl",
    "supply-12v-in",
    "supply-3v3-out",
    "supply-gnd",
}

EXPECTED_BALANCE_TAP_PORTS = {
    "bms-tap-bminus",
    "bms-tap-b1",
    "bms-tap-b2",
    "bms-tap-b3",
    "bms-tap-bplus",
}

EXPECTED_IMPLEMENTED_MONITOR_PORTS = {
    "monitor-vmon",
    "monitor-kbus",
    "monitor-kbat",
    "monitor-gnd",
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


def check_renderer_tooling(checks: Checks) -> None:
    tool_paths = (
        "scripts/render-schematic.sh",
        "scripts/preview-schematics.sh",
    )
    for relative_path in tool_paths:
        tool = ROOT / relative_path
        if not tool.is_file():
            continue
        checks.require(
            bool(tool.stat().st_mode & 0o111),
            f"schematic tool is not executable: {relative_path}",
        )

    renderer = ROOT / "scripts/render-schematic.sh"
    if renderer.is_file():
        renderer_text = renderer.read_text(encoding="utf-8")
        for stem in ("battery-monitor-schematic", "battery-system-installation"):
            checks.require(
                f'"{stem}"' in renderer_text,
                f"canonical renderer does not include schematic stem: {stem}",
            )

    previewer = ROOT / "scripts/preview-schematics.sh"
    if previewer.is_file():
        previewer_text = previewer.read_text(encoding="utf-8")
        checks.require(
            "./scripts/render-schematic.sh" in previewer_text,
            "schematic preview helper does not invoke the canonical renderer",
        )
        checks.require(
            "qlmanage" in previewer_text,
            "schematic preview helper does not use macOS Quick Look",
        )
        checks.require(
            'PREVIEW_DIR="build/schematic-preview"' in previewer_text,
            "schematic preview helper does not keep output under ignored build storage",
        )

    taskfile = ROOT / "Taskfile.yml"
    if taskfile.is_file():
        taskfile_text = taskfile.read_text(encoding="utf-8")
        checks.require(
            "schematic:preview:" in taskfile_text
            and "./scripts/preview-schematics.sh" in taskfile_text,
            "Taskfile does not expose the canonical schematic preview helper",
        )

    workflow = ROOT / ".github/workflows/hardware-schematic.yaml"
    if workflow.is_file():
        workflow_text = workflow.read_text(encoding="utf-8")
        checks.require(
            "run: ./scripts/render-schematic.sh" in workflow_text,
            "hardware schematic workflow does not invoke the canonical renderer",
        )
        checks.require(
            not re.search(r"^\s{2}(push|pull_request):", workflow_text, re.MULTILINE),
            "hardware schematic workflow must remain manual-only",
        )
        checks.require(
            "  workflow_dispatch:" in workflow_text,
            "hardware schematic workflow is missing its manual dispatch trigger",
        )
        for _, svg_path in SCHEMATIC_PAIRS:
            checks.require(
                svg_path in workflow_text,
                f"hardware schematic workflow does not upload {svg_path}",
            )


def macro_invocation_ids(source: str, macro_name: str) -> list[str]:
    return re.findall(
        rf"^\s*\\{re.escape(macro_name)}\{{([^{{}}]+)\}}",
        source,
        re.MULTILINE,
    )


def require_exact_ids(
    checks: Checks,
    actual_ids: list[str],
    expected_ids: set[str],
    description: str,
) -> None:
    checks.require(
        len(actual_ids) == len(expected_ids) and set(actual_ids) == expected_ids,
        f"{description} differ from the expected set: found {actual_ids}",
    )


def check_schematic_sources(checks: Checks) -> None:
    detailed_path = ROOT / "hardware/battery-monitor-schematic.tex"
    if detailed_path.is_file():
        detailed_source = detailed_path.read_text(encoding="utf-8")
        require_exact_ids(
            checks,
            macro_invocation_ids(detailed_source, "ModulePort"),
            EXPECTED_DETAILED_MODULE_PORTS,
            "detailed monitor module contacts",
        )

    installation_path = ROOT / "hardware/battery-system-installation.tex"
    if not installation_path.is_file():
        return

    installation_source = installation_path.read_text(encoding="utf-8")
    cell_ids = re.findall(r"\\texttt\{(BT[0-9]+)\}", installation_source)
    checks.require(
        len(cell_ids) == 4 and set(cell_ids) == {"BT1", "BT2", "BT3", "BT4"},
        f"installation diagram must contain exactly BT1-BT4 once: found {cell_ids}",
    )
    require_exact_ids(
        checks,
        macro_invocation_ids(installation_source, "BalanceTapPort"),
        EXPECTED_BALANCE_TAP_PORTS,
        "installation balance-tap ports",
    )
    require_exact_ids(
        checks,
        macro_invocation_ids(installation_source, "ImplementedMonitorPort"),
        EXPECTED_IMPLEMENTED_MONITOR_PORTS,
        "implemented monitor installation ports",
    )

    required_fragments = {
        r"{K\_BUS / VIN+}": "K_BUS / VIN+ monitor conductor",
        r"{K\_BAT / VIN-}": "K_BAT / VIN- monitor conductor",
        "{VMON+ / +12V IN}": "VMON+ / +12V IN monitor conductor",
        r"{GND / P$-$ ref}": "GND / P- monitor conductor",
        r"{$F_1$}": "independent F1 Kelvin fuse",
        r"{$F_2$}": "independent F2 Kelvin fuse",
        r"{$F_3$}": "independent F3 monitor-supply fuse",
        "{RESERVE = MAINS}": "reversed ATS reserve input",
        "{NORMAL = INVERTER}": "reversed ATS normal input",
        r"interlocked\\break-before-make": "interlocked break-before-make ATS",
        "Mains available:": "mains-available transition state",
        "Mains missing:": "mains-missing transition state",
        "Mains restored:": "mains-restored transition state",
        r"{CHARGER\_ENABLE}": "future CHARGER_ENABLE output",
        "{future output --- not implemented}": "future-output implementation status",
        "{control now}": "current Home Assistant control status",
        "HA smart-plug control and future local enable are alternatives.": (
            "current/future charger-control alternative warning"
        ),
    }
    for fragment, description in required_fragments.items():
        checks.require(
            fragment in installation_source,
            f"installation diagram is missing {description}",
        )


def check_rendered_schematics(checks: Checks) -> None:
    for source_path, svg_path in SCHEMATIC_PAIRS:
        source = ROOT / source_path
        svg = ROOT / svg_path
        if source.is_file():
            source_text = source.read_text(encoding="utf-8")
            checks.require(
                "fill=white" in source_text and "fit=(diagram-content)" in source_text,
                f"schematic source lacks its fitted opaque canvas: {source_path}",
            )
        if not svg.is_file():
            continue

        svg_text = svg.read_text(encoding="utf-8")
        checks.require("<svg" in svg_text, f"rendered file is not SVG: {svg_path}")
        checks.require(
            "<text" not in svg_text,
            f"rendered schematic contains font-dependent text elements: {svg_path}",
        )
        checks.require(
            "<path" in svg_text,
            f"rendered schematic contains no path-based drawing data: {svg_path}",
        )
        checks.require(
            re.search(
                r"<g id=['\"]page1['\"]>\s*<path\b[^>]*fill=['\"]#fff['\"]",
                svg_text,
            )
            is not None,
            f"rendered schematic lacks an opaque white page canvas: {svg_path}",
        )


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
    for relative_path in (
        "README.md",
        "CONTRIBUTING.md",
        "docs/home-assistant.md",
        "docs/commissioning.md",
        "docs/fuse-selection.md",
    ):
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
    check_renderer_tooling(checks)
    check_schematic_sources(checks)
    check_rendered_schematics(checks)
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
