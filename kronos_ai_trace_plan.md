# Plano Detalhado — Telemetria Kronos + Bridge para IA

## Objetivo
Permitir rastreabilidade de execução do emulador (CPU, memória, VDP1/VDP2, DMA, input e eventos de frame) com custo baixo, para que uma IA consiga:
1. Entender estado e causalidade por frame.
2. Detectar regressões automaticamente.
3. Sugerir correções com base em evidência temporal.

---

## 1) Arquitetura proposta

### 1.1 Componentes
- **Kronos Instrumentado**: gera eventos em buffer circular em memória.
- **Bridge (`kronos_trace_bridge`)**: lê eventos e publica em:
  - Named Pipe local (tempo real),
  - arquivo binário (`.ktrace`),
  - export opcional JSONL (`.jsonl`) para análise IA.
- **Consumidor IA**: agente que lê stream + regras e gera diagnóstico.

### 1.2 Princípios de performance (Saturn dev workflow)
- Nada de `printf` em hot path.
- Eventos em structs fixas (`POD`), sem alocação dinâmica por evento.
- Ring buffer lock-free (single-producer/single-consumer).
- Níveis de coleta:
  - `OFF`: sem custo.
  - `BASIC`: métricas por frame + eventos críticos.
  - `DEEP`: janelas curtas por trigger.

---

## 2) Schema de eventos (binário, estável)

## 2.1 Header global (`ktrace_header`)
- magic: `KTRC`
- version: `u16` (ex: 1)
- endianness: `u8` (0=little,1=big)
- tick_hz: `u64`
- build_id_crc32: `u32`
- session_start_tick: `u64`

## 2.2 Header por evento (`ktrace_event_header`)
- `u16 type`
- `u16 size`
- `u32 frame`
- `u32 line`
- `u16 deciline`
- `u16 flags`
- `u64 tick`

## 2.3 Tipos de evento (v1)
- `0x0001 FRAME_BEGIN`
- `0x0002 FRAME_END`
- `0x0010 SH2_EXEC_SLICE`
- `0x0011 SH2_INTERRUPT`
- `0x0012 SH2_DMA`
- `0x0020 SCU_DMA`
- `0x0021 SCU_INTERRUPT`
- `0x0030 VDP1_DRAW_BEGIN`
- `0x0031 VDP1_DRAW_END`
- `0x0032 VDP1_CMD`
- `0x0033 VDP1_FRAME_SWAP`
- `0x0040 VDP2_VBLANK_IN`
- `0x0041 VDP2_VBLANK_OUT`
- `0x0050 INPUT_STATE`
- `0x0060 WATCH_WRITE`
- `0x0061 WATCH_READ`
- `0x0070 PERF_COUNTERS`
- `0x0080 ALERT`

## 2.4 Payloads mínimos por tipo
- `FRAME_BEGIN/END`:
  - `u32 frame`
  - `u16 max_line`
  - `u16 vblank_line`
  - `u8 is_pal`
  - `u8 is_ssh2_running`
  - `u16 reserved`
- `SH2_EXEC_SLICE`:
  - `u8 cpu` (0=MSH2,1=SSH2)
  - `u8 mode` (interp/jit)
  - `u16 reserved`
  - `u32 cycles_requested`
  - `u32 cycles_executed`
  - `u32 pc_begin`
  - `u32 pc_end`
- `SH2_DMA` / `SCU_DMA`:
  - `u8 channel`
  - `u8 width` (1/2/4 bytes)
  - `u16 flags`
  - `u32 src`
  - `u32 dst`
  - `u32 units`
  - `u32 bytes`
- `VDP1_CMD`:
  - `u16 cmd_type`
  - `u16 cmd_ctrl`
  - `u32 cmd_addr`
  - `u16 xa, ya, xb, yb`
  - `u16 xc, yc, xd, yd`
- `INPUT_STATE`:
  - `u16 digital_mask`
  - `s8 analog_x`
  - `s8 analog_y`
  - `u8 trigger_l`
  - `u8 trigger_r`
- `WATCH_WRITE/READ`:
  - `u32 addr`
  - `u32 value`
  - `u8 width`
  - `u8 access`
  - `u16 watch_id`
  - `u8 cpu`
  - `u8 reserved`

---

## 3) Mapa de hooks exatos no código do Kronos

## 3.1 Fronteira de frame (obrigatório)
Arquivo: `C:\saturn\Emulador\Kronos\yabause\src\ctrl\src\yabause.c`
- `YabauseEmulate()`
  - início da função: emitir `FRAME_BEGIN`
  - fim da função (antes do return): emitir `FRAME_END`
- Dentro do loop principal de deciline/line:
  - após `yabsys.DecilineCount`/`LineCount` update: opcional heartbeat leve (somente BASIC+)

## 3.2 Execução SH2
Arquivo: `C:\saturn\Emulador\Kronos\yabause\src\sys\sh2\src\sh2core.c`
- `SH2Exec(...)` e `SH2TestExec(...)`:
  - entrada/saída de slice: `SH2_EXEC_SLICE`
- `DMAProc(...)`, `DMATransferCycles(...)`:
  - início/fim de DMA: `SH2_DMA`
- Pontos de interrupção (`SH2IntcSetIrl`/tratamento de int):
  - emitir `SH2_INTERRUPT`

## 3.3 SCU (DMA/interrupções)
Arquivo: `C:\saturn\Emulador\Kronos\yabause\src\sys\scu\src\scu.c`
- `ScuExec(...)`: counters agregados por frame
- `SucDmaExec(...)` / `DoDMA(...)`: `SCU_DMA`
- `ScuSendLevel{0,1,2}DMAEnd()`, `ScuSendDrawEnd()`, `ScuSendVBlankIN/OUT()`:
  - `SCU_INTERRUPT`

## 3.4 VDP1 (pipeline de draw)
Arquivo: `C:\saturn\Emulador\Kronos\yabause\src\sys\vdp1\src\vdp1.c`
- `Vdp1DrawCommands(...)`:
  - begin/end: `VDP1_DRAW_BEGIN/END`
  - por comando processado: `VDP1_CMD` (filtrável por tipo)
- `Vdp1SwitchFrame()`:
  - `VDP1_FRAME_SWAP`
- `Vdp1VBlankIN_It()`, `Vdp1VBlankOUT()`:
  - marcar sincronização de campo/frame

## 3.5 VDP2 (fronteiras de vídeo)
Arquivo: `C:\saturn\Emulador\Kronos\yabause\src\sys\vdp2\src\vdp2.c`
- `Vdp2VBlankIN()` -> `VDP2_VBLANK_IN`
- `Vdp2VBlankOUT()` -> `VDP2_VBLANK_OUT`

## 3.6 Input
Arquivo sugerido: `yabause/src/core/peripheral/common/peripheral.c`
- no ponto consolidado de leitura de pad por frame:
  - emitir `INPUT_STATE` somente quando mudar estado ou a cada N frames.

---

## 4) Watchpoints para o seu jogo (Interlagos)

## 4.1 Arquivo de configuração
Criar `trace_watch.yaml` com:
- `name`
- `addr`
- `width`
- `cpu_mask`
- `sample_mode` (`on_change`, `every_frame`, `trigger_window`)

Exemplo:
```yaml
watches:
  - name: car_pos_x
    addr: 0x060F1000
    width: 4
    cpu_mask: [MSH2]
    sample_mode: on_change
  - name: car_pos_z
    addr: 0x060F1004
    width: 4
    cpu_mask: [MSH2]
    sample_mode: on_change
  - name: car_yaw
    addr: 0x060F1010
    width: 2
    cpu_mask: [MSH2]
    sample_mode: every_frame
```

## 4.2 Estratégia de coleta
- Evitar dump de RAM completa.
- Coletar só watchpoints + eventos causais.
- Deep window: `[-120, +120]` frames em torno de trigger.

---

## 5) Bridge e integração IA

## 5.1 Bridge mínimo viável
- Processo externo em C++ (Windows):
  - mapear arquivo compartilhado (`CreateFileMapping`) para ring buffer,
  - consumir eventos,
  - salvar `.ktrace` e opcional `.jsonl`.

## 5.2 Interface para IA
- Endpoint local simples:
  - `pipe://kronos-trace`
  - mensagens JSONL por evento (modo debug)
- Modo produção:
  - binário + conversor offline (`ktrace2jsonl`).

## 5.3 Regras automáticas antes de LLM
- `ALERT` quando:
  - frame time > limite,
  - DMA burst acima do budget,
  - VDP1 comando inválido repetido,
  - watchpoint crítico sem atualização por N frames.

---

## 6) Roadmap de implementação

## Sprint 1 (2-3 dias)
- `trace_core` + ring buffer + `FRAME_BEGIN/END` + `PERF_COUNTERS`.
- Hook em `YabauseEmulate`, `SH2Exec`, `ScuExec`, `Vdp1DrawCommands`.
- Dump binário local.

## Sprint 2 (2-4 dias)
- `SCU_DMA`, `SH2_DMA`, `VDP1_CMD`, `INPUT_STATE`.
- `trace_watch.yaml` + `WATCH_WRITE/READ`.
- Bridge em Named Pipe.

## Sprint 3 (2-3 dias)
- Triggers/janelas deep.
- Export JSONL + relatório automático (regressão).
- Perfetto opcional como trilha paralela (já existe base no projeto).

---

## 7) Critérios de sucesso
- Overhead < 3% em `BASIC`.
- Overhead < 8% em `DEEP` (janela curta).
- Reprodução determinística de bug com mesmo input.
- IA consegue responder, por frame, “o que mudou” e “por que mudou”.

---

## 8) Próxima execução (imediata)
1. Criar módulo `trace_core` (headers + cpp/c).
2. Instrumentar 6 hooks mínimos:
   - `YabauseEmulate` begin/end
   - `SH2Exec`
   - `ScuExec`
   - `Vdp1DrawCommands` begin/end
3. Gerar primeira captura de 300 frames para validação do pipeline.

