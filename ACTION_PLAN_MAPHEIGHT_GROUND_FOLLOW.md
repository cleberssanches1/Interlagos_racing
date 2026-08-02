# Plano de ação — chão estilo MapHeight (REDRIVER2)

Data: 2026-07-21  
Referência: `Projetos_Exemplos/REDRIVER2` (`MapHeight` / `sdHeightOnPlane` / `FindSurfaceD2` / `wheelforces.c`)

## Problema

No declive o carro:

1. Anda em **XZ retilíneo** e só “cola” Y com atraso → parece **voar**.
2. **Pitch fraco** porque front/rear veem o mesmo platô até a costura de segmento.
3. **FPS baixo** quando o sample varre muitos segmentos/faces por frame.

Causa raiz: ranking de face por **gap ao Y do corpo** (`preferAsSupport`) + cache/scan caros.  
Nos exemplos (esp. Driver 2): altura é **f(X,Z) no plano**, não “face perto do Y atual do carro”.

## Modelo alvo (arcade barato, SH2)

| Camada | Comportamento |
|--------|----------------|
| Sample | Ponto (X,Z) dentro do triângulo → **Y da equação do plano** (MapHeight) |
| Empate multi-face | Preferir segmento próximo (seed); em overlap de rampa/degrau, face **mais baixa** (Y maior) na vizinhança |
| Probes | 2 (frente/trás) low-cost; frente com lookahead moderado |
| Cache | Face cache + peek **seed+1** (barato); miss → anel estreito |
| Corpo Y | Colar ao target (snap se erro pequeno; step se grande) |
| Pitch | Corda front/rear (já no wheel rig) — só fica certo se o sample estiver certo |
| Não fazer | Rigid-body completo TORCS; varrer janela inteira de faces todo frame |

## Fases de implementação

### F0 — Ranking MapHeight (`track_system.cxx`) — **corrigido (top surface)**

- Hit inside: Y do plano em (X,Z).
- Entre faces no mesmo ponto: **topo** = menor Y (Y-down) — não a face mais baixa
  (preferir max Y afundava o carro sob o asfalto em subidas).
- Fallback: planar + topo + seed.
- Cache + peek seed+1 (FPS).
- **Não congelar XZ** sem suporte (1ª curva re-adquire o chão).

### F1 — Follow do carro — **corrigido**

- Target = média front/rear (sem bias frontal).
- Clamp de salto do target (máx 2× step por sample).
- Snap 0.75 / step 2.0 simétrico (sem snap de descida 3.5 que perfurava).

### F2 — Validação em runtime

1. Aclive suave: sobe colado, sem grudar em parede.
2. Declive (S do Senna): Y desce **durante** o segmento, não só na costura; pitch coerente.
3. FPS: próximo do baseline com cache hit (sem lag de textura/segmento).
4. Telemetria `WHD` / `OVR why`: distâncias estáveis perto do ride height.

### F3 — Opcional (depois)

- Gravidade arcade leve na grade (só se F0–F1 ok).
- Normal da face para roll / wall (já há SurfaceClassify).

## Critério de saída

- Carro **não voa** em linha reta sobre rampa.
- Inclinação e altura acompanham o asfalto **antes** do limite de segmento.
- FPS jogável no Saturn (low-cost path).

## Arquivos

- `src/track_system.cxx` — `FindSurfaceYByFamilySet` ranking + cache
- `src/car_ground_follower.hpp` — probes, blend Y, adesão
- `src/car_physics_shared.hpp` — tunables
- `src/car_physics_v2.hpp` — ordem do step (sem projection kill)
