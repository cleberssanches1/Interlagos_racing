# Plano de ação — 2026-07-16

Teste com pista completa: declive em degraus (S do Senna), segmentos param de montar, curva em inércia.

## Problemas e causas

| # | Sintoma | Causa raiz | Arquivos |
|---|---------|------------|----------|
| 1 | Descida em degraus | Probe reuse (6 frames) congela `surfaceYTarget` + hard snap Y no low-cost; gate de “steep” em 8 units não desarmava reuse em declives reais | `car_ground_follower.hpp` |
| 2 | Segmentos param de montar | Slide fail-closed em textura/prefetch + cooldown longo + merge/retire agressivo + catch-up 1/frame | `track_system.cxx` |
| 3 | Não vira em inércia | Coast com `throttle==0` aplicava scale 0.125 + damping de yaw e zero forçado | `car_dynamics_model.hpp`, `car_physics_shared.hpp` |

## Correções aplicadas

### 1) Adesão em declives
- Não reutilizar superfície se `lastSlopeAbsY >= 1` (grade ativa).
- Low-cost: blend de Y (~0.45) + max step 2.5/2.0 em vez de snap total.
- Full 4-probe ainda em grades ≥ 6.

### 2) Slide / memória
- Catch-up: 2 slides/frame se backlog ≥ 2 e HWR saudável.
- Stall: cooldown menor; **prefetch não congela** junto (max 1 frame).
- Em falha: flush de slots aposentados VDP1.
- Tail miss: retry após flush + fallback LOD 32↔64.
- Prefetch “residente” mais permissivo (metadata ou renderer hot).
- Family merge isolation: cooldown 2 (era 0).
- Grace de recycle de textura: 1 frame (era 0).

### 3) Curva em inércia
- `kCoastSteerMinSpeed` (~0.75 wu/frame).
- Com speed + steer: sem `coastNoSlide` scale, sem kill de yaw no coast.
- Min steer gate também em coast com movimento.

## Como validar
1. S do Senna: Y do carro deve descer de forma contínua (sem platô/salto).
2. Volta longa: cauda da janela continua entrando; sem freeze prolongado de slide.
3. Soltar acelerador em velocidade com volante: o carro deve curvar.

## Follow-up (mesmo dia) — pitch + escadinha + PKG pf fail id:60

### Diagnóstico extra
| Sintoma | Causa |
|---------|--------|
| Sem inclinação no declive | Deadzone pitch 10u + filtro 1/16 + flatten por speedProxy/brake |
| Escadinha ainda | Reuse residual + snap; grade gate falhava |
| `PKG pf fail id:60 md:0` | RDR-only sob stabilization (sem SDR) + budget bloqueava metadata |

### Correções
1. **Pitch** (`car_wheel_rig`): deadzone 2, filtro 1/4, max 18°, step 2.5°, rad≈45, hold speed 4 km/h; não flatten no freio.
2. **Y solo** (`car_ground_follower`): reuse **desligado**; follow 0.85; max step 4.
3. **Slide** (`track_system`):
   - `LoadRuntimeFamilyIdsForSegment` → **SDR fallback** se RDR falhar
   - `BuildRendererFromRuntimeBlob` → **SDR fallback** se RDR falhar
   - Prefetch: metadata **sem budget**; renderer budgeted; true se só metadata ok

### Validar
- Declive: nariz desce e Y contínuo
- Segmento 60+: janela continua (sem freeze em `PKG pf fail`)
