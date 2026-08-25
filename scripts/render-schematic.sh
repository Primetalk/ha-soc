#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly SOURCE="hardware/battery-monitor-schematic.tex"
readonly COMMITTED_SVG="hardware/battery-monitor-schematic.svg"
readonly BUILD_DIR="build/schematic"
readonly CANDIDATE_SVG="${BUILD_DIR}/battery-monitor-schematic.svg"
readonly DVISVGM_LOG="${BUILD_DIR}/dvisvgm.log"

mode="render"

usage() {
  cat <<'EOF'
Usage: ./scripts/render-schematic.sh [--check]

Render hardware/battery-monitor-schematic.tex as a font-independent SVG.

Options:
  --check  Render to build/schematic and fail if the committed SVG is stale.
           The committed artifact is never modified in this mode.
  -h, --help
           Show this help text.

Environment:
  DVISVGM_LIBGS
           Optional path to the Ghostscript shared library passed to dvisvgm.
           On macOS, the script discovers Homebrew Ghostscript automatically.
EOF
}

die() {
  printf 'render-schematic: %s\n' "$*" >&2
  exit 1
}

case "${1:-}" in
  "")
    ;;
  --check)
    mode="check"
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    die "unknown argument: $1"
    ;;
esac

if (( $# > 1 )); then
  usage >&2
  die "expected at most one argument"
fi

# VS Code and other GUI processes on macOS may not inherit MacTeX's path.
if [[ -d /Library/TeX/texbin ]]; then
  export PATH="/Library/TeX/texbin:${PATH}"
fi

for command_name in latexmk dvisvgm kpsewhich; do
  command -v "${command_name}" >/dev/null 2>&1 ||
    die "required command not found: ${command_name}"
done

[[ -n "$(kpsewhich circuitikz.sty)" ]] ||
  die "CircuitikZ is not installed (circuitikz.sty was not found)"
[[ -n "$(kpsewhich standalone.cls)" ]] ||
  die "the LaTeX standalone package is not installed (standalone.cls was not found)"

dvisvgm_args=(--no-fonts --exact)

if [[ -n "${DVISVGM_LIBGS:-}" ]]; then
  [[ -f "${DVISVGM_LIBGS}" ]] ||
    die "DVISVGM_LIBGS does not name a file: ${DVISVGM_LIBGS}"
  dvisvgm_args+=("--libgs=${DVISVGM_LIBGS}")
elif [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
  ghostscript_prefix="$(brew --prefix ghostscript 2>/dev/null || true)"
  homebrew_libgs="${ghostscript_prefix}/lib/libgs.dylib"
  if [[ -n "${ghostscript_prefix}" && -f "${homebrew_libgs}" ]]; then
    dvisvgm_args+=("--libgs=${homebrew_libgs}")
  fi
fi

cd -- "${REPO_ROOT}"

[[ -f "${SOURCE}" ]] || die "source file is absent: ${SOURCE}"
if [[ "${mode}" == "check" && ! -f "${COMMITTED_SVG}" ]]; then
  die "committed SVG is absent: ${COMMITTED_SVG}"
fi

rm -rf -- "${BUILD_DIR}"
mkdir -p -- "${BUILD_DIR}"

latexmk \
  -dvi \
  -interaction=nonstopmode \
  -halt-on-error \
  -file-line-error \
  -outdir="${BUILD_DIR}" \
  "${SOURCE}"

set +e
dvisvgm \
  "${dvisvgm_args[@]}" \
  --output="${CANDIDATE_SVG}" \
  "${BUILD_DIR}/battery-monitor-schematic.dvi" \
  2>&1 | tee "${DVISVGM_LOG}"
dvisvgm_status="${PIPESTATUS[0]}"
set -e

(( dvisvgm_status == 0 )) || die "dvisvgm failed with status ${dvisvgm_status}"
if grep -Eiq 'PostScript specials ignored|Ghostscript not found' "${DVISVGM_LOG}"; then
  die "dvisvgm could not process Ghostscript specials; install Ghostscript or set DVISVGM_LIBGS"
fi

[[ -s "${CANDIDATE_SVG}" ]] || die "renderer produced an empty SVG"
grep -q '<svg' "${CANDIDATE_SVG}" || die "renderer output is not an SVG document"
if grep -q '<text' "${CANDIDATE_SVG}"; then
  die "renderer output contains font-dependent text elements"
fi

if [[ "${mode}" == "check" ]]; then
  if ! cmp -s -- "${CANDIDATE_SVG}" "${COMMITTED_SVG}"; then
    cat >&2 <<'EOF'
render-schematic: the committed hardware schematic SVG is stale.
Regenerate it with:

  ./scripts/render-schematic.sh
EOF
    exit 1
  fi
  printf 'Hardware schematic SVG is current: %s\n' "${COMMITTED_SVG}"
  exit 0
fi

temporary_svg="$(mktemp "${COMMITTED_SVG}.tmp.XXXXXX")"
trap 'rm -f -- "${temporary_svg}"' EXIT
cp -- "${CANDIDATE_SVG}" "${temporary_svg}"
chmod 0644 "${temporary_svg}"
mv -f -- "${temporary_svg}" "${COMMITTED_SVG}"
trap - EXIT

printf 'Rendered hardware schematic: %s\n' "${COMMITTED_SVG}"
