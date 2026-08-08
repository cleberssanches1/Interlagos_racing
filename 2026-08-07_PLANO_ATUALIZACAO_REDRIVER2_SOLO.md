# Plano de atualização — solo estilo REDRIVER2 / F1 PS1

**Data:** 2026-08-07  
**Base:** `tools/reports/redriver2_ground_attitude_study.md`  
**Refs:** Curva do S (F1 96), vídeos 17-15-57 / Teste 1459  

## Objetivo
Carro **desliza colado** às faces; **sem embicada** ao entrar em segmento; pitch/roll emergem de **4 contatos elásticos** (como Driver 2), não de `atan(F−R cru)`.

## Escopo

| ID | Item | Entrega |
|----|------|---------|
| **P0** | Documento + flags | Este plano + `kEnableCornerContactSolver` |
| **L1** | Anti-embicada junção | Contatos reconstruídos com **grade contínuo** antes do solver |
| **L2** | Heave tipo mola | Cap 6u/f, residual 1/2, ar ≤ 0.5u (já + glue continuous) |
| **L3** | Rate pitch/roll | Wheel rig 1.5° dive / 2.5° recover; chord face 3u |
| **L4** | `CornerContactSolver` no hot path | 4 molas → bodyY + pitchDelta + rollDelta |
| **L5** | (fora deste PR) | MapHeight só asfalto / multi-band — asset |

## Design L4 (REDRIVER2 proxy no Saturn)

```
para cada roda i:
  surfaceY[i] = MapHeight (probe)
se junção (frontSeg≠rearSeg ou raw>grade+ε):
  rebuild surfaceY no plano contínuo (mid + gradeChord/2 ± roll)
CornerContactSolver::Step(surfaceY, bodyY, state)
  → bodyY, pitchDelta (F−R), rollDelta (R−L)
publicar F/R/L/R + rideTarget a partir do solver
wheel_rig: atan(pitchDelta/wb) com rate L3
```

## Arquivos
- `car_corner_contact_solver.hpp` — constantes Senna + mola
- `car_physics_shared.hpp` — `GroundState.cornerSolver`, flag, tunables
- `car_ground_follower.hpp` — integração L1+L4 no probe path
- `car_wheel_rig.hpp` — rates L3 (confirmação)

## Validação
1. Rápido no S — sem embicada por laje  
2. Devagar / parar — colado, pitch da face  
3. Comparar `Curva do S.mp4`  
4. SH2: 4 probes/frame já ativos; solver é só aritmética int  

## Rollback
`kEnableCornerContactSolver = false` volta ao path chord-only.

## Status implementação (2026-08-07)
- [x] P0 plano + flag `kEnableCornerContactSolver = true`
- [x] L1 junção: rebuild plano contínuo + pitch→grade + avel=0
- [x] L2 heave continuous + solverBodyY
- [x] L3 rates pitch/roll (follower + wheel_rig)
- [x] L4 `CornerContactSolver` no `car_ground_follower.hpp`
- [ ] Validação in-game (rebuild + vídeo S)
