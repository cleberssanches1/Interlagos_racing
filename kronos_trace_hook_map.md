# Hook Map — Kronos Telemetria para IA

## Frame loop (ponto principal)
- `C:\saturn\Emulador\Kronos\yabause\src\ctrl\src\yabause.c`
- `YabauseEmulate()`
  - início: emitir `KTRACE_EV_FRAME_BEGIN`
  - fim: emitir `KTRACE_EV_FRAME_END`
  - no loop por deciline:
    - antes/depois de `ScuExec(...)`
    - após update de `yabsys.DecilineCount`/`yabsys.LineCount`

## SH2 CPU
- `C:\saturn\Emulador\Kronos\yabause\src\sys\sh2\src\sh2core.c`
- `SH2Exec(...)`: `KTRACE_EV_SH2_EXEC_SLICE`
- `DMAProc(...)` + `DMATransferCycles(...)`: `KTRACE_EV_SH2_DMA`
- pontos de interrupção: `KTRACE_EV_SH2_INTERRUPT`

## SCU
- `C:\saturn\Emulador\Kronos\yabause\src\sys\scu\src\scu.c`
- `ScuExec(...)`: contadores por frame
- `SucDmaExec(...)` / `DoDMA(...)`: `KTRACE_EV_SCU_DMA`
- `ScuSendLevel0DMAEnd/1/2`, `ScuSendDrawEnd`, `ScuSendVBlankIN/OUT`: `KTRACE_EV_SCU_INTERRUPT`

## VDP1
- `C:\saturn\Emulador\Kronos\yabause\src\sys\vdp1\src\vdp1.c`
- `Vdp1DrawCommands(...)`:
  - begin/end: `KTRACE_EV_VDP1_DRAW_BEGIN` / `KTRACE_EV_VDP1_DRAW_END`
  - por comando: `KTRACE_EV_VDP1_CMD`
- `Vdp1SwitchFrame()`: `KTRACE_EV_VDP1_SWAP`
- `Vdp1VBlankIN_It()` / `Vdp1VBlankOUT()`: marcadores de sincronização

## VDP2
- `C:\saturn\Emulador\Kronos\yabause\src\sys\vdp2\src\vdp2.c`
- `Vdp2VBlankIN()`: `KTRACE_EV_VDP2_VBI_IN`
- `Vdp2VBlankOUT()`: `KTRACE_EV_VDP2_VBI_OUT`

## Input
- `C:\saturn\Emulador\Kronos\yabause\src\core\peripheral\common\peripheral.c`
- ponto consolidado de leitura do controle por frame:
  - `KTRACE_EV_INPUT_STATE`

## Watchpoints (game-specific)
- Em wrappers de acesso de memória mapeada do SH2:
  - quando addr bater em tabela de watch:
    - write -> `KTRACE_EV_WATCH_WRITE`
    - read  -> `KTRACE_EV_WATCH_READ`

