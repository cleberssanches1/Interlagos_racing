# Interlagos minimap

`cd/data/mpint.tga` is the authored 128x128 map. Do not overwrite it with a
generated file. `tools/generate_minimap_assets.py` reads it together with
`cd/data/TCOL.BIN` and writes:

- `cd/data/MPHUD.TGA`: a 256x240 indexed VDP2 atlas containing the map and eight
  16x16 red/white player arrows, one for each 45-degree heading;
- `cd/data/MPILUT.BIN`: compact `(u,v)` map coordinates plus the map-space yaw
  offset and handedness for each LOD0 collision segment. The host tool registers
  asphalt-face centers against the map's dark circuit outline, anchored at
  segment 1 on the upper-left straight; this fitting runs during
  `make build`, not in the game.

Python 3 with NumPy is required on the build host. The runtime reads the active
ground segment and car yaw, draws the matching arrow centered on the mapped
position to the LUT pixel, and restores the nine covered map cells when it
moves. Two banks of nine VDP2 cells blend the arrow over those map cells;
transparent arrow pixels keep the circuit visible. NBG1 uses
8x8 cells in a 64x64 pattern-name page. The 128x128 map starts at screen
pixel (0, 80); arrow patterns are stored in offscreen rows 32-33, so the
atlas itself never appears in the game view. `MINIMAP_HUD=0` disables the
minimap and allows the sky to disable NBG1 as before.

The current generated fit reports its average distance to the outline. Final
pixel placement, NBG priority, and the cursor appearance still require an
emulator/hardware run; the image and LOD0 data are registered automatically,
without per-frame geometry scans.
