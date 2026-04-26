# Configuration
SRL_MAX_TEXTURES = 1000         # Number of VDP1 texture slots
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 0               # Framerate control (0=dynamic VDP1 double-buffer, 1=fixed, 2=30fps, etc.)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 4096         # Maximum number of files on a CD
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read
SRL_MALLOC_METHOD = TLSF        # Allocation method: TLSF or SIMPLE are supported.

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 1    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
SGL_MAX_VERTICES = 2800         # Keep SGL work area below TransList (0x060FB800)
SGL_MAX_POLYGONS = 2200         # Values above this overflow WORK_AREA and corrupt TransList/stack
SGL_MAX_EVENTS = 64             # Number of events that can be used
SGL_MAX_WORKS = 64              # Number of works that can be used

# Extra compile flags — two profiles:
#   make                      → debug (default): LWR stage tracing enabled
#   make BUILD_PROFILE=perf   → perf: tracing disabled, cleanest LWR baseline
ifeq ($(BUILD_PROFILE),perf)
SRL_CUSTOM_CCFLAGS =
else
SRL_CUSTOM_CCFLAGS = -DTRACK_LWR_STAGE_TRACE
endif

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
