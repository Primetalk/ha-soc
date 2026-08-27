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

| Task                     | Purpose                                                              |
| ------------------------ | -------------------------------------------------------------------- |
| `task validate`          | Run deterministic clean-checkout and repository invariants           |
| `task test:helpers`      | Compile and run the production C++ helper tests with strict warnings |
| `task schematic`         | Regenerate both committed hardware schematic SVGs                    |
| `task schematic:check`   | Verify both committed SVGs without modifying them                    |
| `task schematic:preview` | Render both diagrams and create local macOS Quick Look PNG previews  |
| `task check`             | Run validation, helper tests, and schematic freshness checking       |

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

## Render the hardware diagrams

The two editable CircuitikZ sources and their generated artifacts are:

| CircuitikZ source                                                                      | Committed SVG                                                                          | Scope                                                        |
| -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| [`hardware/battery-monitor-schematic.tex`](hardware/battery-monitor-schematic.tex)     | [`hardware/battery-monitor-schematic.svg`](hardware/battery-monitor-schematic.svg)     | Detailed SOC-monitor wiring                                  |
| [`hardware/battery-system-installation.tex`](hardware/battery-system-installation.tex) | [`hardware/battery-system-installation.svg`](hardware/battery-system-installation.svg) | Conceptual battery, BMS, charger, inverter, and ATS overview |

Both SVGs are committed so documentation consumers do not need a TeX
installation.

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
from any current directory. It renders both sources through DVI with
`dvisvgm --no-fonts --exact`, writes per-diagram intermediates, logs, and
comparison candidates under `build/schematic/`, and rejects font-dependent SVG
text or incomplete PostScript-special conversion. It validates every candidate
before replacing either committed artifact and rolls back prior replacements if
an unexpected update failure occurs. Check mode reports every absent or stale
pair and never modifies a committed SVG.

On macOS the script uses the standard MacTeX binary directory as a fallback and
discovers Homebrew Ghostscript automatically. Set `DVISVGM_LIBGS` when another
Ghostscript shared library must be used:

```sh
DVISVGM_LIBGS=/path/to/libgs.dylib ./scripts/render-schematic.sh
```

Regenerate and commit both SVGs whenever either CircuitikZ source changes. The
unchanged SVG should remain byte-for-byte deterministic under the same
toolchain.

### macOS Quick Look previews

On macOS, create raster previews of both freshly rendered diagrams with:

```sh
task schematic:preview
```

The underlying [`scripts/preview-schematics.sh`](scripts/preview-schematics.sh)
first calls the canonical renderer, then uses the built-in `qlmanage` command.
Because Quick Look can crop a wide SVG into its square thumbnail canvas, the
helper generates temporary square-padded SVG wrappers. It writes only the final
PNG files under the ignored `build/schematic-preview/` directory and removes
the wrappers. It never writes preview products under `hardware/`.

The preview helper exits with a clear error on a non-macOS host or when
`qlmanage` is unavailable. It is a visual-review convenience, not part of the
portable aggregate `task check` command and not a source of committed artifacts.

## GitHub Actions

The read-only
[hardware schematic workflow](.github/workflows/hardware-schematic.yaml) is
manual only: it does not run for pushes or pull requests. When dispatched, it
installs the TeX toolchain on Ubuntu, calls the canonical renderer, and uploads
both generated files from `hardware/` as the `hardware-schematic-svgs` artifact.
It never pushes generated output; regenerate, check, review, and commit the
canonical SVGs locally.

The [ESPHome workflow](.github/workflows/esphome.yaml) runs repository and helper
checks from a clean checkout, creates private test secrets from the valid-shaped
fixture, installs the pinned ESPHome release, validates the canonical
configuration, and compiles the default ESP32-C3 target.

## Tooling layout

| Path                                                                                     | Purpose                                                         |
| ---------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| [`Taskfile.yml`](Taskfile.yml)                                                           | Optional interface for recurring local commands                 |
| [`scripts/render-schematic.sh`](scripts/render-schematic.sh)                             | Canonical transactional local and CI renderer for both diagrams |
| [`scripts/preview-schematics.sh`](scripts/preview-schematics.sh)                         | macOS Quick Look PNG preview helper                             |
| [`tests/validate_repository.py`](tests/validate_repository.py)                           | Clean-checkout and repository-invariant checks                  |
| [`tests/battery_monitor_helpers_test.cpp`](tests/battery_monitor_helpers_test.cpp)       | Deterministic host tests for production helpers                 |
| [`.github/workflows/esphome.yaml`](.github/workflows/esphome.yaml)                       | Firmware validation and compilation                             |
| [`.github/workflows/hardware-schematic.yaml`](.github/workflows/hardware-schematic.yaml) | Manual rendering and two-SVG artifact upload                    |

Before submitting a change, run `task check` or all equivalent underlying
commands. Run ESPHome validation and compilation whenever firmware inputs
change, and regenerate both SVGs whenever either schematic source changes. On
macOS, use `task schematic:preview` for a final visual review when a diagram was
edited.
