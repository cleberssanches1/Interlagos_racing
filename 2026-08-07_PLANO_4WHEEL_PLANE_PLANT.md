# Plano: 4-wheel plane plant (pneus rentes às faces)

**Data:** 2026-08-07  
**Vídeos Teste 1459:** rápido `14-55-40`, devagar `14-56-50`, parar `14-58-12`  
**Meta:** quatro rodas tocam as faces → forçam o chassis a se nivelar (F1 PS1)

## Diagnóstico (Teste 1459)
- Ar sob os pneus (heave com lag + ride extra)
- Pitch/roll atrasados (filtros + diagonal 2 rodas/frame)
- Ao parar: não assenta no plano da face

## Modelo
```
heave  = avg(Y_FL,Y_FR,Y_RL,Y_RR) + rideOffset   // snap
pitch  = atan((Yf−Yr)/wb) from live F−R          // near 1:1
roll   = atan((Yr−Yl)/track) from live R−L       // near 1:1
body   → full step toward heave (cap 24u/f)
```

## Implementado (hard plant)
1. **4 probes/frame** — `kForceAxleCenterlineProbes = false`
2. **Heave** — target = measured; air ≤ 0.06u; body snap 24u/f
3. **Chord** — live F−R/R−L, blend shift 0, step 20u
4. **Wheel rig** — 6°/f pitch & roll; delta filter 0; susp max 0.25
5. **Ride** — só base −0.125 (sem lift extra em main.cxx)

## Validar
1. Rápido no S — pneus colados, nivelado  
2. Devagar — idem  
3. Parar no declive — assenta no plano da face  
