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
SGL_MAX_VERTICES = 2000
SGL_MAX_POLYGONS = 1500
SGL_MAX_EVENTS = 64             # Number of events that can be used
SGL_MAX_WORKS = 64              # Number of works that can be used

# Physics POC mode:
#   1 = desliga o fluxo principal e roda um mini-circuito sintetico de fisica.
#   0 = mantem o fluxo principal do jogo.
PHYSICS_POC_MODE ?= 1
AUDIO_PROFILE ?= 1

# Visible track residency. Override at build time, for example:
# make TRACK_LOD0_SEGMENTS=2 TRACK_LOD1_SEGMENTS=8 TRACK_LOD2_SEGMENTS=6
# lod_0: detailed GEO + 64x64; lod_1: lighter GEO + 64x64;
# lod_2: lighter GEO + 32x32. Their sum is the visible window size.
TRACK_LOD0_SEGMENTS ?= 2
TRACK_LOD1_SEGMENTS ?= 8
TRACK_LOD2_SEGMENTS ?= 6

# Extra compile flags — two profiles:
#   make                      → debug (default): LWR stage tracing enabled
#   make BUILD_PROFILE=perf   → perf: tracing disabled, cleanest LWR baseline
ifeq ($(BUILD_PROFILE),perf)
SRL_CUSTOM_CCFLAGS = -DPHYSICS_POC_MODE=$(PHYSICS_POC_MODE) -DAUDIO_PROFILE=$(AUDIO_PROFILE) -DPHYS_SATURN_LOW_COST=1 -DPHYS_WALL_COLLISION_RUNTIME=1
else
SRL_CUSTOM_CCFLAGS = -DPHYSICS_POC_MODE=$(PHYSICS_POC_MODE) -DAUDIO_PROFILE=$(AUDIO_PROFILE) -DPHYS_SATURN_LOW_COST=1 -DPHYS_WALL_COLLISION_RUNTIME=1
endif
SRL_CUSTOM_CCFLAGS += -DTRACK_LOD0_SEGMENTS=$(TRACK_LOD0_SEGMENTS) -DTRACK_LOD1_SEGMENTS=$(TRACK_LOD1_SEGMENTS) -DTRACK_LOD2_SEGMENTS=$(TRACK_LOD2_SEGMENTS)

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
