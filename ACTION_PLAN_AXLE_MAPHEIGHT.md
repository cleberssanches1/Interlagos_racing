# Plano de ação — solo por eixos frente/trás (MapHeight)

Data: 2026-07-21  
Performance: **2 samples/frame** no path Saturn (sem 4 cantos).

## Objetivo

As rodas (eixos) detectam a altura do asfalto; o carro **sente** e **inclina** pela corda frente–trás.

```
Yf = MapHeight(eixo dianteiro)   // plano da face em (X,Z)
Yr = MapHeight(eixo traseiro)

Y_corpo = (Yf + Yr) / 2 + ride_height
pitch   = f(Yf − Yr, wheelbase)   // wheel rig (já existente)
```

## Por que não “só CM”

| Errado | Certo (este plano) |
|--------|---------------------|
| 1 sample no centro | 2 samples nos eixos |
| Pitch cosmético sem Y coerente | Y do corpo = média dos eixos |
| Probe frontal longe (quebra atan) | Frente/trás em **± half-wheelbase** (mesmo L do pitch) |
| Preferir face “mais baixa” | Top surface (min Y em Y-down) |

## Performance (obrigatório)

- Saturn / low-cost: **sempre 2 probes** (centro da bitola).
- Cache de face + peek seed+1 (já no `FindSurfaceY`).
- Sem 4 cantos, sem global pass se local ok.
- Contact/surface-type refresh continua em cadence (já existente).

## Fases

### F0 — Contrato dos eixos (`car_ground_follower`) — **implementado**

- Probes: `+halfWB` (frente), `−halfWB` (trás), lateral 0.
- Publicar `debugGroundYFront/Rear` (+ raw) = Yf/Yr.
- `surfaceYTarget = (Yf+Yr)/2 + ride` quando ambos válidos; senão o eixo válido.
- Grade tan = (Yf−Yr)/wheelbase para telemetria.
- **2 samples/frame** (`kForceAxleCenterlineProbes`).

### F1 — Sample estável (`track_system`) — **implementado**

- Ranking **top surface** (não afundar).
- Cache + seed+1 (FPS).

### F2 — Adesão + pitch — **implementado**

- Adesão Y simétrica; snap 1.5× com eixos válidos.
- Pitch no `car_wheel_rig` usa Yf/Yr raw (wheelbase = 2×half).
- Overlay: `OVR axl dF/dR yF/yR` + gr/gf/gt.

### F3 — Validação (manual)

1. 1ª curva: encontra asfalto (XZ livre sem suporte).
2. Subida: não afunda sob a face.
3. Declive: pitch + Y acompanham corda F–T.
4. FPS estável com 2 samples.

## Arquivos

- `src/car_ground_follower.hpp`
- `src/car_physics_shared.hpp` (tunables de probe/adesão)
- `src/car_wheel_rig.*` (sem mudança de fórmula se L bater)
- `src/track_system.cxx` (sample já top-surface)
