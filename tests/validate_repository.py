#!/usr/bin/env python3
"""Deterministic clean-checkout checks for the canonical ESPHome project."""

from __future__ import annotations

import base64
import binascii
import html
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_PACKAGES = {
    "packages/battery-config.yaml",
    "packages/connectivity.yaml",
    "packages/measurement.yaml",
    "packages/soc.yaml",
    "packages/soc-rules.yaml",
    "packages/display.yaml",
}

PUBLIC_GUIDES = {
    "docs/architecture.md",
    "docs/getting-started.md",
    "docs/glossary.md",
    "docs/home-assistant-setup.md",
    "docs/troubleshooting.md",
    "docs/wiring.md",
}

REQUIRED_PATHS = {
    ".gitignore",
    ".github/workflows/esphome.yaml",
    ".github/workflows/hardware-schematic.yaml",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    "Taskfile.yml",
    "battery-monitor.yaml",
    "secrets.example.yaml",
    "include/battery_monitor_types.h",
    "assets/fonts/RobotoMono-Variable.ttf",
    "assets/fonts/OFL.txt",
    *PUBLIC_GUIDES,
    "docs/home-assistant.md",
    "docs/commissioning.md",
    "docs/fuse-selection.md",
    "hardware/battery-monitor-schematic.tex",
    "hardware/battery-monitor-schematic.svg",
    "hardware/battery-system-installation.tex",
    "hardware/battery-system-installation.svg",
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

EXPECTED_I2C_PINS = {
    "i2c_sda_pin": "GPIO0",
    "i2c_scl_pin": "GPIO1",
}

ESP32_C3_STRAPPING_PINS = {"GPIO2", "GPIO8", "GPIO9"}

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

GPL_EXPRESSION = "GPL-3.0-only"
COPYRIGHT_NOTICE = "Copyright (C) 2026 primetalk contributors"
MARKDOWN_LINK_TARGET = re.compile(r"\]\(\s*(<[^>\n]+>|[^)\n]*?)\s*\)")
MARKDOWN_HEADING = re.compile(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$")
HTML_ANCHOR = re.compile(
    r"<(?:a|[a-z][a-z0-9-]*)\b[^>]*\b(?:id|name)=[\"']([^\"']+)[\"']",
    re.IGNORECASE,
)


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


def public_markdown_paths() -> list[str]:
    paths = sorted(ROOT.glob("*.md")) + sorted((ROOT / "docs").rglob("*.md"))
    return [path.relative_to(ROOT).as_posix() for path in paths]


def markdown_without_fenced_code(text: str) -> tuple[str, bool]:
    """Return Markdown with fenced blocks blanked while preserving line numbers."""
    output: list[str] = []
    fence_character = ""
    fence_length = 0

    for line in text.splitlines(keepends=True):
        marker = re.match(r"^\s*(`{3,}|~{3,})(.*)$", line.rstrip("\r\n"))
        if not fence_character:
            if marker is None:
                output.append(line)
                continue
            fence_character = marker.group(1)[0]
            fence_length = len(marker.group(1))
            output.append("\n" if line.endswith("\n") else "")
            continue

        closing = re.match(
            rf"^\s*{re.escape(fence_character)}{{{fence_length},}}\s*$",
            line.rstrip("\r\n"),
        )
        if closing is not None:
            fence_character = ""
            fence_length = 0
        output.append("\n" if line.endswith("\n") else "")

    return "".join(output), not fence_character


def markdown_link_targets(text: str) -> list[tuple[int, str]]:
    without_code, _ = markdown_without_fenced_code(text)
    targets: list[tuple[int, str]] = []
    for match in MARKDOWN_LINK_TARGET.finditer(without_code):
        target = match.group(1).strip()
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1].strip()
        else:
            # A non-angle-bracket destination cannot contain unescaped spaces;
            # anything after one is an optional Markdown link title.
            target = target.split(maxsplit=1)[0] if target else ""
        targets.append((without_code.count("\n", 0, match.start()) + 1, html.unescape(target)))
    return targets


def github_heading_slug(heading: str) -> str:
    heading = re.sub(r"!?(?:\[([^\]]*)\])\([^)]*\)", r"\1", heading)
    heading = re.sub(r"<[^>]+>", "", heading)
    heading = html.unescape(heading).replace("`", "").strip().lower()
    heading = re.sub(r"[^\w\s-]", "", heading, flags=re.UNICODE)
    return re.sub(r"\s+", "-", heading)


def markdown_anchors(text: str) -> set[str]:
    without_code, _ = markdown_without_fenced_code(text)
    anchors = {unquote(anchor) for anchor in HTML_ANCHOR.findall(without_code)}
    slug_counts: dict[str, int] = {}
    for line in without_code.splitlines():
        match = MARKDOWN_HEADING.match(line)
        if match is None:
            continue
        base_slug = github_heading_slug(match.group(1))
        if not base_slug:
            continue
        count = slug_counts.get(base_slug, 0)
        anchors.add(base_slug if count == 0 else f"{base_slug}-{count}")
        slug_counts[base_slug] = count + 1
    return anchors


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


def check_project_status_and_licensing(checks: Checks) -> None:
    license_path = ROOT / "LICENSE"
    if license_path.is_file():
        license_text = license_path.read_text(encoding="utf-8")
        checks.require(
            re.search(
                r"GNU GENERAL PUBLIC LICENSE\s+Version 3, 29 June 2007",
                license_text,
            )
            is not None
            and "END OF TERMS AND CONDITIONS" in license_text,
            "LICENSE does not contain the canonical GNU GPL version 3 text",
        )

    licensing_documents = ("README.md", "CONTRIBUTING.md")
    for relative_path in licensing_documents:
        path = ROOT / relative_path
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        checks.require(
            GPL_EXPRESSION in text,
            f"{relative_path} is missing the exact {GPL_EXPRESSION} expression",
        )
        checks.require(
            COPYRIGHT_NOTICE in text,
            f"{relative_path} is missing the project copyright notice",
        )

    readme_path = ROOT / "README.md"
    if not readme_path.is_file():
        return
    readme = readme_path.read_text(encoding="utf-8")
    readme_opening = "\n".join(readme.splitlines()[:20]).lower()
    checks.require(
        "prototype only" in readme_opening,
        "README must show prototype-only status within its first 20 lines",
    )
    checks.require(
        "assets/fonts/OFL.txt" in readme,
        "README does not preserve the separate Roboto Mono font-license reference",
    )
    checks.require(
        "https://github.com/jurgen2005/esphome-shunt" in readme
        and "independent implementation" in readme
        and "does not claim license or permission" in readme,
        "README acknowledgement does not preserve independent-implementation provenance",
    )

    linked_paths: set[str] = set()
    for _, raw_target in markdown_link_targets(readme):
        parsed = urlsplit(raw_target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        linked_paths.add(unquote(parsed.path))
    for guide in sorted(PUBLIC_GUIDES):
        checks.require(guide in linked_paths, f"README does not link required guide: {guide}")


def check_public_current_state(checks: Checks) -> None:
    forbidden_phrases = (
        "obsolete prototype configuration",
        "obsolete prototype yaml",
        "prototype yaml files are retained",
        "prototype yaml files remain",
    )
    obsolete_yaml_reference = re.compile(r"shunt[^\n)]*\.ya?ml", re.IGNORECASE)
    for relative_path in public_markdown_paths():
        text, _ = markdown_without_fenced_code(read_text(relative_path))
        lowered = text.lower()
        for phrase in forbidden_phrases:
            checks.require(
                phrase not in lowered,
                f"stale current-state prototype claim appears in {relative_path}: {phrase}",
            )
        checks.require(
            obsolete_yaml_reference.search(text) is None,
            f"public current-state documentation references an obsolete shunt YAML in {relative_path}",
        )


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


def check_i2c_hardware_defaults(checks: Checks) -> None:
    battery_config = read_text("packages/battery-config.yaml")
    for substitution, expected_pin in EXPECTED_I2C_PINS.items():
        match = re.search(
            rf"^\s*{re.escape(substitution)}:\s*(GPIO[0-9]+)\s*(?:#.*)?$",
            battery_config,
            re.MULTILINE,
        )
        checks.require(
            match is not None,
            f"canonical I2C substitution is missing or malformed: {substitution}",
        )
        if match is None:
            continue

        actual_pin = match.group(1)
        checks.require(
            actual_pin == expected_pin,
            f"canonical {substitution} must be {expected_pin}, found {actual_pin}",
        )
        checks.require(
            actual_pin not in ESP32_C3_STRAPPING_PINS,
            f"canonical {substitution} uses ESP32-C3 strapping pin {actual_pin}",
        )

    detailed_source = read_text("hardware/battery-monitor-schematic.tex")
    checks.require(
        r"{GPIO0: SDA\\GPIO1: SCL};" in detailed_source,
        "detailed monitor schematic must label GPIO0 as SDA and GPIO1 as SCL",
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


def check_markdown_markup(checks: Checks) -> None:
    disclosure_tag = re.compile(r"<\s*(/?)\s*(details|summary)\b[^>]*>", re.IGNORECASE)

    for relative_path in public_markdown_paths():
        text = read_text(relative_path)
        without_code, fences_balanced = markdown_without_fenced_code(text)
        checks.require(
            fences_balanced,
            f"Markdown code fences are unbalanced in {relative_path}",
        )

        stack: list[tuple[str, int]] = []
        for match in disclosure_tag.finditer(without_code):
            closing, raw_name = match.groups()
            name = raw_name.lower()
            line_number = without_code.count("\n", 0, match.start()) + 1
            if not closing:
                if name == "summary":
                    checks.require(
                        any(open_name == "details" for open_name, _ in stack),
                        f"summary appears outside details in {relative_path}:{line_number}",
                    )
                stack.append((name, line_number))
                continue

            if not stack:
                checks.failures.append(
                    f"closing {name} has no opener in {relative_path}:{line_number}"
                )
                continue
            open_name, open_line = stack.pop()
            checks.require(
                open_name == name,
                f"closing {name} in {relative_path}:{line_number} does not match "
                f"{open_name} opened at line {open_line}",
            )

        for open_name, line_number in stack:
            checks.failures.append(
                f"unclosed {open_name} tag in {relative_path}:{line_number}"
            )


def check_markdown_links(checks: Checks) -> None:
    root = ROOT.resolve()
    anchor_cache: dict[Path, set[str]] = {}

    for relative_path in public_markdown_paths():
        source = ROOT / relative_path
        for line_number, raw_target in markdown_link_targets(read_text(relative_path)):
            checks.require(
                bool(raw_target),
                f"empty Markdown link target in {relative_path}:{line_number}",
            )
            if not raw_target:
                continue

            parsed = urlsplit(raw_target)
            # External URLs, mail links, and same-page anchors deliberately remain
            # offline and outside local target checks.
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue

            decoded_path = unquote(parsed.path)
            candidate = (source.parent / decoded_path).resolve()
            try:
                candidate.relative_to(root)
            except ValueError:
                checks.failures.append(
                    f"local Markdown link escapes the repository in "
                    f"{relative_path}:{line_number}: {raw_target}"
                )
                continue

            checks.require(
                candidate.is_file(),
                f"local Markdown link target is absent in "
                f"{relative_path}:{line_number}: {raw_target}",
            )
            if not candidate.is_file() or not parsed.fragment:
                continue

            fragment = unquote(html.unescape(parsed.fragment))
            if candidate.suffix.lower() != ".md":
                continue
            anchors = anchor_cache.setdefault(
                candidate,
                markdown_anchors(candidate.read_text(encoding="utf-8")),
            )
            checks.require(
                fragment in anchors,
                f"local Markdown fragment is absent in "
                f"{relative_path}:{line_number}: {raw_target}",
            )


def main() -> int:
    checks = Checks()
    check_required_paths(checks)
    check_project_status_and_licensing(checks)
    check_public_current_state(checks)
    check_renderer_tooling(checks)
    check_schematic_sources(checks)
    check_i2c_hardware_defaults(checks)
    check_rendered_schematics(checks)
    check_canonical_composition(checks)
    check_secrets_fixture(checks)
    check_yaml_credentials(checks)
    check_canonical_identifiers(checks)
    check_tracked_artifacts(checks)
    check_ignore_policy(checks)
    check_markdown_markup(checks)
    check_markdown_links(checks)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
