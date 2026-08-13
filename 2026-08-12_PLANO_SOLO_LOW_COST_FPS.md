# Plano: solo low-cost (FPS)

## Problema
FPS baixo. Path atual de solo faz **muitos** MapHeight por frame:
- 4 cantos × (seed + lat/2 + seed±1) ≈ 8–12 probes  
- + tan planar N e N+1 (2–4 probes)  
- + look-ahead heave (1–2 probes)  
→ **~12–18 queries/frame** no SH2.

## Modelo simplificado (mantém topologia)
1. **2 probes/eixo** (centro dianteiro + traseiro) — pitch = F−R, heave = média  
2. **1 retry** só seed±1 se miss (sem lat half)  
3. **Sem** tan planar multi-ponto next-seg  
4. **Sem** look-ahead MapHeight no heave  
5. Grade hold + `gradeDy = tan·speed` entre samples (deslize contínuo)  
6. Pitch: grade + F−R same-seg, rate-limit moderado  

## Custo alvo
**2–4 MapHeight/frame** (vs 12–18).

## Tradeoff
- Roll lateral só se reativar 4 wheels  
- Pre-tilt next-seg mais fraco (só grade hold)

## Implementação
Flags + `sampleWheel` + desligar look-ahead attitude/heave probe.
