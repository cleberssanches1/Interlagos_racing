# Speedometer VDP2 sprites

Run `generate.ps1` to produce the TGA files in `cd/data`. ImageMagick is
required. All source art is vector SVG; generated TGA images are 32-bit true
color with transparent black and dimensions divisible by 16.

## Runtime atlas

`SPD_ATLAS.TGA` is the 32-bit art-review atlas. The game uses the generated
`cd/data/SPDHUD.TGA`: 8-bit indexed source TGA with at most 16 palette entries
(index 0 transparent), 256x240. The game packs it to VDP2 4bpp on loading.
The visible overlay is 80x80 at `(240,160)` in the 320x240 logical screen; the rest of the atlas stores
dynamic alternatives and is never drawn as one large image. The generator
composites every live gear, digit and RPM cell over its panel background.
`Bmp2Tile` uses 16x16 map cells in this project.
The runtime atlas regions are:

| Asset | Pixel origin | Size | Map-cell origin |
|---|---:|---:|---:|
| Static curved RPM arc and gear frame | `(0,0)` | `80x80` | `(0,0)` |
| Composited gear glyphs | `(0,80)` | `192x16` | `(0,5)` |
| Composited speed digits | `(0,96)` | `160x16` | `(0,6)` |
| Twelve RPM states × ten affected cells | `(0,112)` | `256x128` | rows `7..14` |

The runtime loads this indexed atlas once and writes only changed map words:
three speed digits, the gear glyph and the RPM arc. The separate
`SPD_PANEL.TGA`, `SPD_DIGITS.TGA`, `SPD_GEARS.TGA`, `SPD_RPM_SEGMENTS.TGA`, and
`SPD_UNITS.TGA` files are convenient for art review and individual sprite use.

## Component sheets

- `SPD_PANEL.TGA`, 80x80: transparent overlay with dark curved RPM arc,
  4..14 scale, red hatched end, SPEED label and black MT/gear badge.
- `SPD_DIGITS.TGA`, 80x32: ten 16x16 seven-segment glyph cells, arranged in
  five columns by two rows, reading `0` through `9`.
- `SPD_GEARS.TGA`, 96x32: twelve 16x16 cells in six columns by two rows;
  `1 2 3 4 5 6 / 7 8 N R 0 A`.
- `SPD_RPM_SEGMENTS.TGA`, 80x80: fully lit yellow/orange arc. The generator
  clips it at twelve progressive thresholds and bakes ten affected 16x16
  cells for each state. The game updates those ten map words only when the
  RPM state changes.
- `SPD_UNITS.TGA`, 128x32: four 32x32 cells for `km/h`, `RPM`, `AT`, and `x1000`.

The current engine operates between 4,200 and 12,500 RPM. The twelve arc
states light progressively across that range; the remaining dark/red hatched
end is a fixed redline cue. Speed always shows three digits (`000`, `073`,
`311`). Gear glyph index order is `1..8, N, R, 0, A` and digit order `0..9`.

Enable or disable the game component at build time with `SPEEDOMETER_HUD=1` or
`SPEEDOMETER_HUD=0`. The graphics live in VDP2 VRAM; there is no VDP1 texture
slot or frame-time allocation. The 80x80 overlay is placed at the lower right;
the art still needs visual confirmation in the emulator.
`SPD:6` at the top left means the atlas reached VDP2; `SPD:1` means the CD
file was not found, `SPD:2` insufficient Cart RAM, `SPD:3` invalid TGA,
`SPD:4` tile conversion failed, `SPD:5` insufficient VDP2 VRAM, and
`SPD:7` indicates a CD read failure. The TGA and tile-conversion buffers are
staged in Cart RAM and freed after the copy into VDP2 VRAM; only a one-sector
CD read buffer remains temporary in Work RAM.

Transparent pixels are palette index 0 in `SPDHUD.TGA`. The game selects
the current gear, three speed digits and RPM state through the NBG2 map.
