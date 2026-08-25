# Contributing

This document covers repository maintenance, automated checks, and schematic
generation. Installation, wiring, configuration, flashing, and operation of the
battery monitor are documented in the user-facing [`README.md`](README.md).

## Development prerequisites

The deterministic repository checks require Python 3. The production-helper
tests require a C++17 compiler. Firmware changes additionally require the pinned
ESPHome version documented in [`README.md`](README.md).

[Task](https://taskfile.dev/) is supported as an optional command runner. It is
not required by CI or by the underlying scripts.

## Recurring checks

List the available tasks and run the aggregate local check with:

```sh
task --list
task check
```

The Task interface exposes:

| Task | Purpose |
| ---- | ------- |
| `task validate` | Run deterministic clean-checkout and repository invariants |
| `task test:helpers` | Compile and run the production C++ helper tests with strict warnings |
| `task schematic` | Regenerate the committed hardware schematic SVG |
| `task schematic:check` | Verify the committed SVG without modifying it |
| `task check` | Run validation, helper tests, and schematic freshness checking |

Without Task, run the underlying repository and helper checks directly:

```sh
python3 tests/validate_repository.py

temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/battery-monitor-helper-tests.XXXXXX")"
trap 'rm -rf -- "${temp_dir}"' EXIT
binary="${temp_dir}/battery-monitor-helper-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror -I. \
  tests/battery_monitor_helpers_test.cpp -o "${binary}"
"${binary}"
```

When firmware changes, also validate and compile the canonical ESPHome entry
point as described in [`README.md`](README.md).

## Render the hardware schematic

[`hardware/battery-monitor-schematic.tex`](hardware/battery-monitor-schematic.tex)
is the editable CircuitikZ source. The generated
[`hardware/battery-monitor-schematic.svg`](hardware/battery-monitor-schematic.svg)
is committed so documentation consumers do not need a TeX installation.

Install TeX Live or MacTeX with CircuitikZ, `latexmk`, `dvisvgm`, and
Ghostscript. Then regenerate the committed artifact with the canonical script:

```sh
./scripts/render-schematic.sh
```

Verify freshness without modifying the committed SVG with:

```sh
./scripts/render-schematic.sh --check
```

The renderer resolves the repository root from its own location, so it works
from any current directory. It renders through DVI with
`dvisvgm --no-fonts --exact`, writes intermediates and the comparison candidate
under `build/schematic/`, rejects incomplete PostScript-special conversion, and
atomically replaces the committed SVG only after successful rendering.

On macOS the script adds the standard MacTeX binary directory to `PATH` and
discovers Homebrew Ghostscript automatically. Set `DVISVGM_LIBGS` when another
Ghostscript shared library must be used:

```sh
DVISVGM_LIBGS=/path/to/libgs.dylib ./scripts/render-schematic.sh
```

Regenerate and commit the SVG whenever its CircuitikZ source changes.

## Continuous integration

The read-only
[hardware schematic workflow](.github/workflows/hardware-schematic.yaml) installs
the TeX toolchain on Ubuntu and calls the canonical renderer in `--check` mode.
It rejects stale committed output and uploads the generated build candidate as
the `battery-monitor-schematic-svg` artifact; it never pushes changes to a
contributor branch.

The [ESPHome workflow](.github/workflows/esphome.yaml) runs repository and helper
checks from a clean checkout, creates private test secrets from the valid-shaped
fixture, installs the pinned ESPHome release, validates the canonical
configuration, and compiles the default ESP32-C3 target.

## Tooling layout

| Path | Purpose |
| ---- | ------- |
| [`Taskfile.yml`](Taskfile.yml) | Optional interface for recurring local commands |
| [`scripts/render-schematic.sh`](scripts/render-schematic.sh) | Canonical local and CI schematic renderer |
| [`tests/validate_repository.py`](tests/validate_repository.py) | Clean-checkout and repository-invariant checks |
| [`tests/battery_monitor_helpers_test.cpp`](tests/battery_monitor_helpers_test.cpp) | Deterministic host tests for production helpers |
| [`.github/workflows/esphome.yaml`](.github/workflows/esphome.yaml) | Firmware validation and compilation |
| [`.github/workflows/hardware-schematic.yaml`](.github/workflows/hardware-schematic.yaml) | Schematic freshness check and artifact upload |

Before submitting a change, run `task check` or all equivalent underlying
commands. Run ESPHome validation and compilation whenever firmware inputs
change, and regenerate the SVG whenever the schematic source changes.
