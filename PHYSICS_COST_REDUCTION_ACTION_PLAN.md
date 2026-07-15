# Plano de Ação — Redução de Custo da Física (sem impacto negativo)

**Projeto:** Interlagos_racing  
**Data:** 2026-07-14  
**Baseline:** Master faz física sync; Slave só track async; `PHYS_SATURN_LOW_COST=1`

## Objetivo

Reduzir queries de chão/parede por frame no SH2 Master **sem**:
- atravessar muro sob aceleração
- afundar/flutuar no asfalto
- piorar handling (steer/yaw/shift)

## Princípio

Manter **dinâmica planar todo frame** (throttle, freio, yaw, marcha).  
Baratear só a **amostragem geométrica** (rays / hull).

## Estado já existente (não reverter)

- 1× `StepOnce` / frame
- ground probe reduzido (frente/trás centro) em low dynamics
- reuse de surface com wall refresh
- body-clip caro desligado
- wall low-cost (4 hull + center fallback)

## Fases

### Fase 1a — 1 constante (sem funções novas)

| Item | Status |
|------|--------|
| Wall hull low-cost `probeCount` **4 → 2** (frente L/R; center fallback mantido) | **aplicado** |
| Rollback | `2u` → `4u` na mesma linha em `ResolveWallPushMultiProbe` |
| Tentativa adaptativa anterior | **REVERTIDA** (invalid opcode; código demais no header) |

**Aceite:** reta throttle + curva + freio na parede; ISO pin; sem invalid opcode.

### Fase 1b — ground reduced threshold

| Item | Status |
|------|--------|
| `steeringAbs <= 35` → `<= 42` (mais frames com probe 2 pts) | **aplicado** |
| Rollback | `42` → `35` na mesma linha |

### Fase 1c — surface contact cadence

| Item | Status |
|------|--------|
| `kSurfaceContactCadenceFrames` low-cost **4 → 6** | **aplicado** |
| Rollback | `6u` → `4u` em `car_physics_shared.hpp` |

### Fase 1d — grip probe interval

| Item | Status |
|------|--------|
| `kGripProbeIntervalFrames` low-cost **4 → 6** | **aplicado** |
| Rollback | `6u` → `4u` em `car_physics_shared.hpp` |

### Fase 1e — low-dynamics surface reuse interval

| Item | Status |
|------|--------|
| `kLowDynamicsProbeIntervalFrames` low-cost **4 → 6** | **aplicado** |
| Rollback | `6u` → `4u` em `car_physics_shared.hpp` |

### Fase 1f — medium-dynamics surface reuse interval

| Item | Status |
|------|--------|
| `kMediumDynamicsProbeIntervalFrames` low-cost **4 → 6** | **aplicado** |
| Rollback | `6u` → `4u` em `car_physics_shared.hpp` |

### Fase 1g — medium-dynamics steering band

| Item | Status |
|------|--------|
| `kMediumDynamicsSteeringThreshold` **6 → 12** | **aplicado** |
| Nota | `kAuxProbeCadenceFrames` **não é usado** no runtime |
| Rollback | `12` → `6` |

### Fase 1h — low-dynamics speed band

| Item | Status |
|------|--------|
| `kLowDynamicsSpeedProxyThreshold` **30 → 40** km/h | **aplicado** |
| Rollback | `40` → `30` |

### Fase 1i — low-dynamics steering band

| Item | Status |
|------|--------|
| `kLowDynamicsSteeringThreshold` **8 → 12** | **aplicado** |
| Rollback | `12` → `8` |

### Fase 1j+ (depois)

- pausar se retornos diminutos; medir FPS/queries antes de mais cortes

### Fase 2 (depois, se medido)

- lateral ground cadenciado em full-probe frames
- wall hull 2 pts + radius levemente maior
- telemetria de query a cada N frames

### Fase 3 (evitar por enquanto)

- sim async na Slave
- multi-step physics
- pular wall sob aceleração

## Validação

1. `tools/validate_saturn_stable_build.ps1` → ISO `4134912`
2. Yabause: reta full throttle, curva, freio na parede, offroad
3. Overlay queries (se ativo): `wallQuery` / surface calls tendem a cair em reta

## Arquivos

- `src/car_physics_shared.hpp` — flags/tunables
- `src/car_ground_follower.hpp` — wall probe count + reduced ground threshold
