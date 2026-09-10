# Display image sources and derivatives

The three original JPEGs were supplied by the project owner on 2026-09-10.
They show the earlier firmware layout, before the two-color layout correction.
The measurement JPEG is retained unchanged as a historical overview, including
its original metadata. The other two raw JPEGs were removed after preserving
their important content in exact PNG crops; they remain in Git history
(commit `aefc8c4`, which added all three photos).

| State | Source JPEG | Original-pixel crop | AI-enhanced documentation illustration |
| --- | --- | --- | --- |
| Measurements | [20260910_201403.jpg](20260910_201403.jpg) | [display-measurements-original.png](display-measurements-original.png) | [display-measurements-enhanced.png](display-measurements-enhanced.png) |
| SOC not anchored | `20260910_201406.jpg` (Git history) | [display-soc-unset-original.png](display-soc-unset-original.png) | [display-soc-unset-enhanced.png](display-soc-unset-enhanced.png) |
| Rules unavailable | `20260910_201411.jpg` (Git history) | [display-rules-original.png](display-rules-original.png) | [display-rules-enhanced.png](display-rules-enhanced.png) |

## Exact crops for publication

The `*-original.png` files are lossless PNG crops of the decoded JPEG pixels
after applying EXIF orientation. There is no resizing, interpolation,
perspective correction, sharpening, recoloring, or generated content. Coordinates
below are `(left, top, right, bottom)` in the oriented **3000×4000** image, with
right/bottom exclusive:

| State | Crop rectangle | Output size |
| --- | --- | --- |
| Measurements | `(1118, 1645, 1798, 2325)` | 680×680 |
| SOC not anchored | `(1206, 1689, 1886, 2368)` | 680×679 |
| Rules unavailable | `(1294, 1864, 1952, 2500)` | 658×636 |

These crops retain the complete OLED module and a small margin of context.
Use these exact crops or the retained JPEG when photographic authenticity matters.

## Enhanced versions and prompt

The `*-enhanced.png` files were produced with the built-in imagegen tool from
the corresponding exact crop. The intended edit isolates the module from the
hand/workbench onto a neutral dark background. Generative editing can alter fine
details, so these are explicitly labeled **illustrations**, not exact photos,
calibration evidence, or images of the revised firmware. The original-pixel crops
remain the authority for displayed readings and layout defects.

Each of the three calls used the following prompt, replacing `{state}` with
`measurements`, `soc-unset`, or `rules` and supplying its matching original crop:

```text
Edit target: the attached real photograph of a small OLED module, state {state}.
Create a clean documentation product-photo cutout. Isolate ONLY the complete
physical rectangular OLED circuit-board module, preserve its perspective and
all original physical details, with a narrow plain neutral dark-gray surrounding
background. Remove the hand and workshop behind it. Most importantly preserve
the display screen content EXACTLY as photographed, including cropped characters,
the yellow/blue color split THROUGH characters, blur/glare, numeric readings,
and placement. Do NOT repair, sharpen, redraw, complete, retype, correct, or
change any characters or pixels within the black glass screen. The image
documents old layout defects, those defects MUST remain visible. No new text,
labels, decorations or mockup. Keep module orientation as input, modest border,
square output. This is background cleanup only; the screen must remain unchanged.
```

## Revised-layout preview

[`display-layout-preview.svg`](display-layout-preview.svg) is a deterministic
software illustration made from the revised display lambda's drawing calls and
ESPHome-generated glyph bitmaps for the bundled Roboto Mono font. Its blue and
yellow regions simulate the photographed panel at rotation 180°. Its readings
and rule states are examples, not measured hardware results. The font's license
is preserved in [`../../assets/fonts/OFL.txt`](../../assets/fonts/OFL.txt).

See [`../display.md`](../display.md) for the analysis and physical acceptance
checks. Do not replace a source photo with either kind of illustration.
