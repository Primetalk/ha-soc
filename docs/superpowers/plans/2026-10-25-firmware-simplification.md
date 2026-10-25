# Firmware Simplification Implementation Plan

> **For agentic workers:** Execute this approved refactoring inline, one independently verified task at a time.

**Goal:** Reduce duplicated firmware logic while preserving existing observable behavior and upgrade compatibility.

**Architecture:** Keep ESPHome declarations and hardware publication in YAML. Move reusable decisions and storage operations into focused, host-testable headers, with the existing umbrella include retained for compatibility.

**Tech Stack:** ESPHome 2026.8.0, C++17 host tests, Python 3 repository checks.

**Spec:** The five refactoring recommendations and two tooling improvements approved in the accompanying chat.

## Global Constraints

- Preserve entity identities, names, event payload fields, event order, and replay sequence semantics.
- Preserve preference names, record formats, setup priorities, and ESP8266 allocation order.
- Preserve the distinct current-only and voltage/current freshness policies; policy corrections are separate work.
- Preserve double-precision live integration and legacy persisted float SOC records.
- Leave pre-existing unrelated working-tree changes untouched; do not commit or publish automatically.

## Tasks

- [x] 1. Characterize event recording/naming and rule lifecycle with deterministic host tests. Cover startup suppression, hysteresis reconstruction, configuration invalidation/rearming, multiple crossings, sequence rollover, timestamp flags, and legacy record words. Run tests before extraction and after each implementation step.
- [x] 2. Separate persistence, SOC math, direction detection, rule logic, and ESPHome sensor availability into focused headers. Keep `battery_monitor_types.h` as the umbrella. Add `store_soc_event(...)`, `soc_rule_name(...)`, and `evaluate_soc_rules(...)`; use them from the production YAML and unify live/replay delivery in one parameterized script.
- [x] 3. Add and test `sample_is_fresh(now, last_sample, max_age)` at zero, the inclusive stale boundary, and millis rollover. Replace repeated age expressions while retaining each caller's existing policy and invalidation behavior.
- [x] 4. Extract shared package composition and project metadata to `packages/common.yaml`; leave board overrides in each entry point. Make offline include validation follow the local include graph, covering missing files and cycles without requiring a specific arrangement. Add behavior tests for the resolver.
- [x] 5. Run the preference-layout check for ESP8266 in CI. Document the new file responsibilities. Validate both configurations and compile both firmware targets with the pinned version, then run host, repository, display, preference-layout, and schematic freshness checks.
- [x] 6. Request an independent code review, address material findings, and report the final verification evidence and any limitations.

## Verification

```sh
python3 tests/validate_repository.py
task test:helpers
python3 tests/repository_validation_test.py
esphome config battery-monitor.yaml
esphome config battery-monitor-esp8266.yaml
esphome compile battery-monitor.yaml
esphome compile battery-monitor-esp8266.yaml
python3 tests/preference_layout_test.py .esphome/build/battery-monitor-esp8266/src/main.cpp
python3 tests/display_layout_test.py .esphome/build/battery-monitor/src/main.cpp
python3 tests/display_layout_test.py .esphome/build/battery-monitor-esp8266/src/main.cpp
./scripts/render-schematic.sh --check
```

Use the pinned ESPHome executable from the temporary review environment when the global installation differs. Run `repository_validation_test.py` explicitly because the generated-source checks are standalone scripts, not discoverable unit tests.

## Results

- Both configurations validate and both firmware targets compile with ESPHome 2026.8.0 using Python 3.13; neither build reports compiler warnings.
- All 60 public entity registrations per board match freshly generated source from the original tracked firmware.
- Host helper tests, seven include-resolution tests, repository invariants, ESP8266 preference layout, both 54-scenario display checks, and both schematic freshness checks pass.
- Independent code review found no actionable issues.
- Firmware was built locally; no device was flashed and no physical commissioning was performed.
