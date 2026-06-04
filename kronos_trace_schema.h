#ifndef KRONOS_TRACE_SCHEMA_H
#define KRONOS_TRACE_SCHEMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTRACE_MAGIC 0x4352544B /* 'KTRC' */
#define KTRACE_VERSION 1

typedef enum ktrace_event_type_e {
  KTRACE_EV_FRAME_BEGIN     = 0x0001,
  KTRACE_EV_FRAME_END       = 0x0002,
  KTRACE_EV_SH2_EXEC_SLICE  = 0x0010,
  KTRACE_EV_SH2_INTERRUPT   = 0x0011,
  KTRACE_EV_SH2_DMA         = 0x0012,
  KTRACE_EV_SCU_DMA         = 0x0020,
  KTRACE_EV_SCU_INTERRUPT   = 0x0021,
  KTRACE_EV_VDP1_DRAW_BEGIN = 0x0030,
  KTRACE_EV_VDP1_DRAW_END   = 0x0031,
  KTRACE_EV_VDP1_CMD        = 0x0032,
  KTRACE_EV_VDP1_SWAP       = 0x0033,
  KTRACE_EV_VDP2_VBI_IN     = 0x0040,
  KTRACE_EV_VDP2_VBI_OUT    = 0x0041,
  KTRACE_EV_INPUT_STATE     = 0x0050,
  KTRACE_EV_WATCH_WRITE     = 0x0060,
  KTRACE_EV_WATCH_READ      = 0x0061,
  KTRACE_EV_PERF_COUNTERS   = 0x0070,
  KTRACE_EV_ALERT           = 0x0080
} ktrace_event_type_t;

#pragma pack(push, 1)
typedef struct ktrace_file_header_s {
  uint32_t magic;
  uint16_t version;
  uint8_t endianness;
  uint8_t reserved0;
  uint64_t tick_hz;
  uint32_t build_crc32;
  uint64_t session_start_tick;
} ktrace_file_header_t;

typedef struct ktrace_event_header_s {
  uint16_t type;
  uint16_t size;
  uint32_t frame;
  uint32_t line;
  uint16_t deciline;
  uint16_t flags;
  uint64_t tick;
} ktrace_event_header_t;

typedef struct ktrace_frame_payload_s {
  uint16_t max_line;
  uint16_t vblank_line;
  uint8_t is_pal;
  uint8_t is_ssh2_running;
  uint16_t reserved;
} ktrace_frame_payload_t;

typedef struct ktrace_sh2_exec_payload_s {
  uint8_t cpu; /* 0=MSH2,1=SSH2 */
  uint8_t mode;
  uint16_t reserved;
  uint32_t cycles_requested;
  uint32_t cycles_executed;
  uint32_t pc_begin;
  uint32_t pc_end;
} ktrace_sh2_exec_payload_t;

typedef struct ktrace_dma_payload_s {
  uint8_t channel;
  uint8_t width;
  uint16_t flags;
  uint32_t src;
  uint32_t dst;
  uint32_t units;
  uint32_t bytes;
} ktrace_dma_payload_t;

typedef struct ktrace_vdp1_cmd_payload_s {
  uint16_t cmd_type;
  uint16_t cmd_ctrl;
  uint32_t cmd_addr;
  int16_t xa, ya, xb, yb;
  int16_t xc, yc, xd, yd;
} ktrace_vdp1_cmd_payload_t;

typedef struct ktrace_input_payload_s {
  uint16_t digital_mask;
  int8_t analog_x;
  int8_t analog_y;
  uint8_t trigger_l;
  uint8_t trigger_r;
} ktrace_input_payload_t;

typedef struct ktrace_watch_payload_s {
  uint32_t addr;
  uint32_t value;
  uint8_t width;
  uint8_t access;
  uint16_t watch_id;
  uint8_t cpu;
  uint8_t reserved;
} ktrace_watch_payload_t;

typedef struct ktrace_perf_payload_s {
  uint32_t sh2_master_cycles;
  uint32_t sh2_slave_cycles;
  uint32_t scu_cycles;
  uint32_t vdp1_cmd_count;
  uint32_t vdp1_draw_calls;
  uint32_t dma_bytes_total;
} ktrace_perf_payload_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
