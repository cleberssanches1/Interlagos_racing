# Plano de ação: tela preta (só HUD) — 2026-10-07

**Evidência:** `Captura de tela 2026-10-07 132512.png`

## Leitura do print

| Linha | Valor | Significado |
|-------|--------|-------------|
| `MEM10 H:0/1024 L:1024/0` | HWR **0 used / 1024 free**; LWR **1024 used / 0 free** | LWR esgotado; HWR ocioso |
| `V1:0/421` | Nenhuma textura VDP1 | Esteira não chegou a upload |
| `BLT/BL2` zeros | Sem soft-admit/upload | Pacotes não Ready |
| `BL3 hwr:1048576` | ~1 MiB HWR livre (bytes) | Confirma HWR ocioso |
| `SCM … fm:1 tc:0 td:1` | FSMAP+TDIR OK; TCOL Cart off | Não é pressão de TCOL Cart |
| FPS ~14 | Loop vivo | Hang não é o problema |

Janela visual existe (LOD 5+6+8=19), mas **nenhum segmento ficou Ready** → cena 3D preta.

## Causa mais provável

Consumidores de **LWR antes da esteira** (POC: car → **sky/BG** → track). O céu (`SkyPanorama` / tiles) aloca em LWR; com LWR cheio, `PKG slot alloc` / build falha → TB/TP/V1 zerados.

TCOL Cart (`tc:0`) e TDIR (~1,5 KiB) **não** explicam LWR 100%.

## A/B escolhido

**Desligar sky/BG no POC** até a esteira subir; manter TCOL gate OFF; LOD 5/6/8.

## Passos

1. `enableBg = false` no caminho POC (ou equivalente).
2. Reativar `printInitRam` pre-maps / post-maps / post-belt (`hf/lf/cf`).
3. Rebuild ISO; validar: `L:` free > 0, V1>0, segmentos Ready, pista visível.
4. Depois: religar BG com carga **após** `trackSystem.Initialize`, se LWR permitir.

## Fallback se ainda preto

- Adiar FSMAP Cart para depois da esteira.
- Reduzir janela boot.
- Confirmar `PKG *` / `RAM bg` vs `RAM trk` no overlay.

## Status

| Item | Estado |
|------|--------|
| Diagnóstico + plano | Feito |
| `enableBg=false` no POC | Feito |
| `printInitRam` pre/maps/belt/post | Feito |
| ISO `BuildDrop/Interlagos_racing.cue` | Rebuild 13:36 |
| Validação emulador | Pendente (você) |
