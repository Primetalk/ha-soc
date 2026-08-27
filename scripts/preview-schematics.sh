#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly PREVIEW_DIR="build/schematic-preview"
readonly -a SCHEMATIC_STEMS=(
  "battery-monitor-schematic"
  "battery-system-installation"
)

die() {
  printf 'preview-schematics: %s\n' "$*" >&2
  exit 1
}

cd -- "${REPO_ROOT}"

# Always refresh the canonical committed SVGs before deriving local previews.
./scripts/render-schematic.sh

[[ "$(uname -s)" == "Darwin" ]] ||
  die "Quick Look preview generation is available only on macOS"
command -v qlmanage >/dev/null 2>&1 ||
  die "required macOS command not found: qlmanage"
command -v python3 >/dev/null 2>&1 ||
  die "required command not found: python3"

temporary_dir="$(mktemp -d "build/schematic-preview.tmp.XXXXXX")"
readonly wrapper_dir="${temporary_dir}/wrappers"
mkdir -p -- "${wrapper_dir}"

cleanup() {
  [[ -n "${temporary_dir}" ]] && rm -rf -- "${temporary_dir}"
  return 0
}
trap cleanup EXIT

source_paths=()
for stem in "${SCHEMATIC_STEMS[@]}"; do
  source_paths+=("hardware/${stem}.svg")
done

# Quick Look emits square thumbnails and can crop a wide SVG. Center each
# diagram's original viewBox in a square canvas without changing its content.
python3 - "${wrapper_dir}" "${source_paths[@]}" <<'PY'
from decimal import Decimal
from pathlib import Path
import re
import sys


def decimal_text(value: Decimal) -> str:
    text = format(value, "f").rstrip("0").rstrip(".")
    return text or "0"


wrapper_dir = Path(sys.argv[1])
for source_argument in sys.argv[2:]:
    source = Path(source_argument)
    document = source.read_text(encoding="utf-8")
    root_match = re.search(r"<svg\b[^>]*>", document)
    if root_match is None:
        raise SystemExit(f"preview-schematics: SVG root is absent: {source}")

    root = root_match.group(0)
    view_box_match = re.search(r"\bviewBox='([^']+)'", root)
    if view_box_match is None:
        raise SystemExit(f"preview-schematics: viewBox is absent: {source}")

    view_box = [Decimal(value) for value in view_box_match.group(1).split()]
    if len(view_box) != 4:
        raise SystemExit(f"preview-schematics: invalid viewBox: {source}")

    x, y, width, height = view_box
    side = max(width, height)
    square_values = (
        x - (side - width) / 2,
        y - (side - height) / 2,
        side,
        side,
    )
    square_view_box = " ".join(decimal_text(value) for value in square_values)

    square_root = re.sub(
        r"\bwidth='[^']+'",
        f"width='{decimal_text(side)}pt'",
        root,
        count=1,
    )
    square_root = re.sub(
        r"\bheight='[^']+'",
        f"height='{decimal_text(side)}pt'",
        square_root,
        count=1,
    )
    square_root = re.sub(
        r"\bviewBox='[^']+'",
        f"viewBox='{square_view_box}'",
        square_root,
        count=1,
    )

    destination = wrapper_dir / f"{source.stem}.preview.svg"
    destination.write_text(
        document[: root_match.start()]
        + square_root
        + document[root_match.end() :],
        encoding="utf-8",
    )
PY

for stem in "${SCHEMATIC_STEMS[@]}"; do
  wrapper_svg="${wrapper_dir}/${stem}.preview.svg"
  generated_png="${temporary_dir}/${stem}.preview.svg.png"
  preview_png="${temporary_dir}/${stem}.png"

  qlmanage -t -s 1600 -o "${temporary_dir}" "${wrapper_svg}" >/dev/null
  [[ -s "${generated_png}" ]] ||
    die "Quick Look did not create ${generated_png}"
  mv -f -- "${generated_png}" "${preview_png}"
done

rm -rf -- "${wrapper_dir}" "${PREVIEW_DIR}"
mv -- "${temporary_dir}" "${PREVIEW_DIR}"
temporary_dir=""

for stem in "${SCHEMATIC_STEMS[@]}"; do
  printf 'Created schematic preview: %s/%s.png\n' "${PREVIEW_DIR}" "${stem}"
done
