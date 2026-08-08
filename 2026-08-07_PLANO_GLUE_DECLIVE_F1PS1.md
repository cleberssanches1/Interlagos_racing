# Plano: glue no declive + settle ao parar (F1 PS1)

**Data:** 2026-08-07  
**Vídeo bug:** `Yabause v0.9.14 2026-08-07 14-09-09.mp4`  
**Referência:** `Curva do S.mp4` (F1 PS1 1996 — pneus colados, pitch da face)

## Diagnóstico (vídeo + código)

| Sintoma | Causa raiz |
|---------|------------|
| Em movimento: “escorrega” mas **longe do asfalto** | Heave com glue 1/8 + LPF lento + `maxSlideDown` 4u + lift chord 1/16 + ride extra −0.5 → corpo fica **acima** do MapHeight |
| Ao parar: **afunda com traseira baixa** (nariz para cima) | `gradeDy=0` + pitch segue F−R colapsado/ruído em vez do **grade contínuo** → perde nose-down da face e assenta invertido |

Referência PS1: pneus **colados**, pitch = plano da pista em movimento **e** parado.

## Ações (implementadas)

### H1 — Colar heave (anti-float)
- Parado (`speed < kGradeHoldMinSpeed`): target = plano LPF plantado no measured (não `last+gradeDy` lagado).
- Em movimento: glue **assimétrico** — float (`plane > slid`) 1/2 ou 1/4; anti-túnel 1/8.
- Teto de float: se `slid` ainda >2u acima do plano, puxa para o plano.
- Adhesion body: residual 1/2 parado / 1/4 float grande / 1/8 leve; plant permite até `kMaxYStepDown`.
- LPF: parado → half-step + snap; float sem seam → step normal (não crawl).
- Removido lift de chord no ride (1/16).
- Ride extra main: **−0.25** (era −0.5); base −0.125 → total **−0.375**.
- `kMaxBodySlideDownY` 6u; `kHeaveSnapEpsY` 0.125.

### A1 — Pitch ao parar no declive (anti traseira-baixa)
- Parado + grade válido: `rawChord = gradeChord` (assenta no plano da face).
- Em movimento no declive: se raw quase zero/negativo, puxa para grade (nunca flip rear-down).
- Recover de chord: se parado em declive e chord < grade, sobe para grade (não achata).
- `kGradeAttitudeHoldMaxFrames` 12 → **24**.

## Validação
1. Senna S em movimento: pneus colados, deslize contínuo (sem escada, sem ar).
2. Soltar acelerador e parar no declive: afunda **mantendo nariz baixo** (frente mais baixa que traseira na face).
3. Retomar aceleração: sem re-escada.
4. Comparar visual com `Curva do S.mp4`.

## Rollback
- Tunables em `car_physics_shared.hpp` + blocos comentados 14-09-09 em `car_ground_follower.hpp`.
- main.cxx ride: restaurar `0x00008000` (−0.5) se clipping.
