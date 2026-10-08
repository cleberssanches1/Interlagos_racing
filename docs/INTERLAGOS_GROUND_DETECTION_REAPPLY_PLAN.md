# Plano: reaplicar detecção de solo (TCOL) + janela independente

**Fonte:** `C:\desenvolvimento\sega_saturn\plano_detecçao_solo.txt`  
**Referência:** commit `1323917`  
**Baseline render:** `f518eba` (LOD 5/6/8, SGL 2000/1500)

## Decisões

- Restaurar pipeline TCOL (gerador TCL1 + queries + testes + `TCOL.BIN`).
- `kEnableTrackCollisionCartMapsAtBoot = false` no boot (evita tela preta por Cart).
- LOD visual **5/6/8**, SGL **2000/1500**.
- Janela de colisão independente **±4** segmentos (`kEnableIndependentCollisionWindow`).
- Liberar FSMAP quando TCOL estiver carregado e válido.
- Adiar: merge coplanar agressivo, sticky por roda, kerb/pit 4/5.

## Fases

0. Baseline LOD  
1. Offline TCOL + `build_all` 3.2  
2. Runtime gated + `FindSurfaceY`/walls  
3. Janela independente ±4  
4. Testes host + ISO  

## Aceite

- `TCOL.BIN` no ISO; testes TCL1 PASS  
- Boot padrão: `tc:0`, render estável  
- Gate ON (A/B): TCOL válido, queries por célula, FSMAP liberado  
- Queries TCOL não dependem de `segmentRenderers_`

## Status (2026-10-07)

| Fase | Estado |
|------|--------|
| 0 LOD 5/6/8 | Feito |
| 1 Offline TCOL + build_all 3.2 | Feito |
| 2 Runtime gated (`tc` no boot) | Feito (gate OFF) |
| 3 Janela ±4 independente | Feito |
| 4 ISO + host tests | Feito (emulador manual pendente) |

A/B no emulador: `-DPHYS_TRACK_COLLISION_CART_MAPS_AT_BOOT=1`
