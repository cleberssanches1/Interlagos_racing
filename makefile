# Configuration
SRL_MAX_TEXTURES = 512          # Number of VDP1 texture slots (memory-optimized for Saturn)
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 0               # Framerate control (0=dynamic VDP1 double-buffer, 1=fixed, 2=30fps, etc.)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 256          # 91 files currently; leaves safe directory headroom
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read
SRL_MALLOC_METHOD = TLSF        # Allocation method: TLSF or SIMPLE are supported.

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 1    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
# WorkArea is fixed at 0x060C0000; TransList at 0x060FB800 (~238 KB max).
# Keep WorkArea below TransList at 0x060FB800 with explicit safety margin.
SGL_MAX_VERTICES = 2200
SGL_MAX_POLYGONS = 1700
SGL_MAX_EVENTS = 64             # Number of events that can be used
SGL_MAX_WORKS = 64              # Number of works that can be used

# Physics POC mode:
#   1 = desliga o fluxo principal e roda um mini-circuito sintetico de fisica.
#   0 = mantem o fluxo principal do jogo.
PHYSICS_POC_MODE ?= 1
AUDIO_PROFILE ?= 1
# Detailed Master-frame phase telemetry (HUD rows M/P/L and long-stall capture).
# Current diagnostic build keeps it enabled. For a normal release, use
# make MASTER_FRAME_PHASE_TELEMETRY=0 to recover High Work RAM headroom.
MASTER_FRAME_PHASE_TELEMETRY ?= 0
SPEEDOMETER_HUD ?= 1
GAME_MENU_ENABLED ?= 1
# 1 = Auto Lap drives the normal car physics from PATH.NYA; 0 = legacy
# direct-pose route stepper for A/B diagnosis. No geometry rebuild required.
AUTO_LAP_PHYSICS_DRIVER ?= 1
# Two independently moving PATH cars (lines 0 and 2). Set to 0 for the
# established single-car Auto Lap; no generated track assets need rebuilding.
AUTO_LAP_NPCS ?= 2
# Voltas cronometradas do Time Attack (1..12). Futuramente ajustavel no menu.
TIME_ATTACK_LAPS ?= 3
# Temporarily disable the minimap while exposing runtime diagnostics.
# Re-enable with: make MINIMAP_HUD=1
MINIMAP_HUD ?= 1
# Memory-pool readout sampled every ten seconds; set to 0 for release builds.
PHYS_ENABLE_MEM10_OVERLAY ?= 1
# Emit detailed memory/texture state only when a tail segment is soft-admitted.
TRACK_FAILURE_TELEMETRY ?= 1
# Sample named track-stage LWR deltas; disable after the long-run diagnosis.
TRACK_LWR_STAGE_TRACE ?= 1

# Visible track residency. Override at build time, for example:
# make TRACK_LOD0_SEGMENTS=3 TRACK_LOD1_SEGMENTS=8 TRACK_LOD2_SEGMENTS=6
# lod_0: detailed GEO + 64x64; lod_1: lighter GEO + 64x64;
# lod_2: lighter GEO + 32x32. Their sum is the visible window size.
# := prevents stale shell env from forcing 0/10/10.
# Keep the project LOD window as configured (do not change for resource A/B).
TRACK_LOD0_SEGMENTS := 0
TRACK_LOD1_SEGMENTS := 20
TRACK_LOD2_SEGMENTS := 0

# Extra compile flags ??? two profiles:
#   make                      ??? debug (default): LWR stage tracing enabled
#   make BUILD_PROFILE=perf   ??? perf: tracing disabled, cleanest LWR baseline
# Walls ON again via TCOL-only ??4 (GEO wall path stays gated off when TCOL valid).
# Soft wheel plant retained from stabilization A/B. MEM10 is on by default for
# runtime memory diagnosis and can be disabled with PHYS_ENABLE_MEM10_OVERLAY=0.
ifeq ($(BUILD_PROFILE),perf)
SRL_CUSTOM_CCFLAGS = -DPHYSICS_POC_MODE=$(PHYSICS_POC_MODE) -DAUDIO_PROFILE=$(AUDIO_PROFILE) -DPHYS_SATURN_LOW_COST=1 -DPHYS_WALL_COLLISION_RUNTIME=1 -DPHYS_WHEEL_STRICT_SURFACE=0 -DPHYS_ENABLE_MEM10_OVERLAY=$(PHYS_ENABLE_MEM10_OVERLAY) -DTRACK_FAILURE_TELEMETRY=$(TRACK_FAILURE_TELEMETRY) -DTRACK_LWR_STAGE_TRACE=$(TRACK_LWR_STAGE_TRACE)
else
SRL_CUSTOM_CCFLAGS = -DPHYSICS_POC_MODE=$(PHYSICS_POC_MODE) -DAUDIO_PROFILE=$(AUDIO_PROFILE) -DPHYS_SATURN_LOW_COST=1 -DPHYS_WALL_COLLISION_RUNTIME=1 -DPHYS_WHEEL_STRICT_SURFACE=0 -DPHYS_ENABLE_MEM10_OVERLAY=$(PHYS_ENABLE_MEM10_OVERLAY) -DTRACK_FAILURE_TELEMETRY=$(TRACK_FAILURE_TELEMETRY) -DTRACK_LWR_STAGE_TRACE=$(TRACK_LWR_STAGE_TRACE)
endif
SRL_CUSTOM_CCFLAGS += -DTRACK_LOD0_SEGMENTS=$(TRACK_LOD0_SEGMENTS) -DTRACK_LOD1_SEGMENTS=$(TRACK_LOD1_SEGMENTS) -DTRACK_LOD2_SEGMENTS=$(TRACK_LOD2_SEGMENTS)
SRL_CUSTOM_CCFLAGS += -DMASTER_FRAME_PHASE_TELEMETRY=$(MASTER_FRAME_PHASE_TELEMETRY)
SRL_CUSTOM_CCFLAGS += -DSPEEDOMETER_HUD=$(SPEEDOMETER_HUD)
SRL_CUSTOM_CCFLAGS += -DGAME_MENU_ENABLED=$(GAME_MENU_ENABLED)
SRL_CUSTOM_CCFLAGS += -DAUTO_LAP_PHYSICS_DRIVER=$(AUTO_LAP_PHYSICS_DRIVER)
SRL_CUSTOM_CCFLAGS += -DAUTO_LAP_NPCS=$(AUTO_LAP_NPCS)
SRL_CUSTOM_CCFLAGS += -DTIME_ATTACK_LAPS=$(TIME_ATTACK_LAPS)
SRL_CUSTOM_CCFLAGS += -DMINIMAP_HUD=$(MINIMAP_HUD)

# Disk name
CD_NAME = Interlagos_racing

# Directory build will be placed into (use alternate drop to avoid stale locks)
BUILD_DROP = ./BuildDrop
# Find all .c and .cxx files
SOURCES = $(patsubst ./%,%,$(shell find src/ -name '*.c'))
SOURCES += $(patsubst ./%,%,$(shell find src/ -name '*.cxx'))

# Include shared makefile
SDK_ROOT = ../../saturnringlib
include $(SDK_ROOT)/shared.mk

# Re-bake both map assets once per build. The game uses only MPILUT.BIN at
# runtime; NumPy/ICP work stays on the host, never on either SH-2.
.PHONY: minimap_assets
minimap_assets: cd/data/TCOL.BIN cd/data/mpint.tga tools/generate_minimap_assets.py
	python tools/generate_minimap_assets.py

build: minimap_assets
create_iso: minimap_assets

# The Time Attack image atlas includes the original speedometer art. Regenerate
# it whenever either source changes, without rebaking track segments.
cd/data/TAHUD.TGA: cd/data/SPDHUD.TGA tools/assets/time_attack/TA_EXTRA.svg tools/assets/time_attack/generate.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/assets/time_attack/generate.ps1

build: cd/data/TAHUD.TGA
create_iso: cd/data/TAHUD.TGA
