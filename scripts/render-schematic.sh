#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly BUILD_DIR="build/schematic"
readonly -a SCHEMATIC_STEMS=(
  "battery-monitor-schematic"
  "battery-system-installation"
)

mode="render"

usage() {
  cat <<'EOF'
Usage: ./scripts/render-schematic.sh [--check]

Render every hardware CircuitikZ source as a font-independent committed SVG.

Options:
  --check  Render to build/schematic and fail if any committed SVG is stale.
           Committed artifacts are never modified in this mode.
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

# VS Code and other GUI processes on macOS may not inherit MacTeX's path. Keep
# an explicitly configured TeX toolchain first unless a required command is
# missing.
if [[ -d /Library/TeX/texbin ]] &&
  { ! command -v latexmk >/dev/null 2>&1 ||
    ! command -v dvisvgm >/dev/null 2>&1 ||
    ! command -v kpsewhich >/dev/null 2>&1; }; then
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

for stem in "${SCHEMATIC_STEMS[@]}"; do
  source_path="hardware/${stem}.tex"
  [[ -f "${source_path}" ]] || die "source file is absent: ${source_path}"
done

rm -rf -- "${BUILD_DIR}"
mkdir -p -- "${BUILD_DIR}"

for stem in "${SCHEMATIC_STEMS[@]}"; do
  source_path="hardware/${stem}.tex"
  candidate_svg="${BUILD_DIR}/${stem}.svg"
  dvisvgm_log="${BUILD_DIR}/${stem}.dvisvgm.log"

  latexmk \
    -dvi \
    -interaction=nonstopmode \
    -halt-on-error \
    -file-line-error \
    -outdir="${BUILD_DIR}" \
    "${source_path}"

  set +e
  dvisvgm \
    "${dvisvgm_args[@]}" \
    --output="${candidate_svg}" \
    "${BUILD_DIR}/${stem}.dvi" \
    2>&1 | tee "${dvisvgm_log}"
  dvisvgm_status="${PIPESTATUS[0]}"
  set -e

  (( dvisvgm_status == 0 )) ||
    die "dvisvgm failed for ${source_path} with status ${dvisvgm_status}"
  if grep -Eiq 'PostScript specials ignored|Ghostscript not found' "${dvisvgm_log}"; then
    die "dvisvgm could not process Ghostscript specials for ${source_path}; install Ghostscript or set DVISVGM_LIBGS"
  fi

  [[ -s "${candidate_svg}" ]] ||
    die "renderer produced an empty SVG for ${source_path}"
  grep -q '<svg' "${candidate_svg}" ||
    die "renderer output is not an SVG document for ${source_path}"
  if grep -q '<text' "${candidate_svg}"; then
    die "renderer output contains font-dependent text elements for ${source_path}"
  fi
done

if [[ "${mode}" == "check" ]]; then
  stale_count=0
  for stem in "${SCHEMATIC_STEMS[@]}"; do
    candidate_svg="${BUILD_DIR}/${stem}.svg"
    committed_svg="hardware/${stem}.svg"
    if [[ ! -f "${committed_svg}" ]] || ! cmp -s -- "${candidate_svg}" "${committed_svg}"; then
      printf 'render-schematic: committed SVG is absent or stale: %s\n' \
        "${committed_svg}" >&2
      stale_count=$((stale_count + 1))
    else
      printf 'Hardware schematic SVG is current: %s\n' "${committed_svg}"
    fi
  done
  if (( stale_count > 0 )); then
    cat >&2 <<'EOF'
Regenerate every committed hardware diagram with:

  ./scripts/render-schematic.sh
EOF
    exit 1
  fi
  exit 0
fi

# Stage every candidate beside its destination before replacing anything. Keep
# rollback copies so an unexpected replacement failure cannot leave only part
# of the committed diagram set updated.
temporary_svgs=()
backup_svgs=()
had_originals=()

cleanup_transaction_files() {
  for path in "${temporary_svgs[@]}" "${backup_svgs[@]}"; do
    [[ -n "${path}" ]] && rm -f -- "${path}"
  done
  return 0
}
trap cleanup_transaction_files EXIT

for stem in "${SCHEMATIC_STEMS[@]}"; do
  candidate_svg="${BUILD_DIR}/${stem}.svg"
  committed_svg="hardware/${stem}.svg"
  temporary_svg="$(mktemp "${committed_svg}.tmp.XXXXXX")"
  cp -- "${candidate_svg}" "${temporary_svg}"
  chmod 0644 "${temporary_svg}"
  temporary_svgs+=("${temporary_svg}")

  if [[ -f "${committed_svg}" ]]; then
    backup_svg="$(mktemp "${committed_svg}.backup.XXXXXX")"
    cp -p -- "${committed_svg}" "${backup_svg}"
    backup_svgs+=("${backup_svg}")
    had_originals+=("yes")
  else
    backup_svgs+=("")
    had_originals+=("no")
  fi
done

replaced_count=0
for index in "${!SCHEMATIC_STEMS[@]}"; do
  stem="${SCHEMATIC_STEMS[$index]}"
  committed_svg="hardware/${stem}.svg"
  if mv -f -- "${temporary_svgs[$index]}" "${committed_svg}"; then
    temporary_svgs[$index]=""
    replaced_count=$((replaced_count + 1))
    continue
  fi

  rollback_failed=0
  while (( replaced_count > 0 )); do
    replaced_count=$((replaced_count - 1))
    rollback_stem="${SCHEMATIC_STEMS[$replaced_count]}"
    rollback_target="hardware/${rollback_stem}.svg"
    if [[ "${had_originals[$replaced_count]}" == "yes" ]]; then
      if ! mv -f -- "${backup_svgs[$replaced_count]}" "${rollback_target}"; then
        rollback_failed=1
      else
        backup_svgs[$replaced_count]=""
      fi
    elif ! rm -f -- "${rollback_target}"; then
      rollback_failed=1
    fi
  done
  if (( rollback_failed != 0 )); then
    die "could not replace ${committed_svg}; rollback also failed"
  fi
  die "could not replace ${committed_svg}; previous replacements were rolled back"
done

for stem in "${SCHEMATIC_STEMS[@]}"; do
  printf 'Rendered hardware schematic: hardware/%s.svg\n' "${stem}"
done
