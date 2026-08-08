# Plano: movimento F1 PS1 + anti-embicada na junção

**Data:** 2026-08-07  
**Bug video:** `Yabause … 17-15-57.mp4`  
**Referência:** `Curva do S.mp4` (F1 1996 — deslize contínuo, pneus colados, sem mergulho de nariz por laje)

## Diagnóstico

| Sintoma (17-15-57) | Causa |
|--------------------|--------|
| Movimento “estranho” / trêmulo | Hard plant: chord step 20u, pitch 6°/f, heave 24u snap |
| Nariz para baixo ao **entrar em novo segmento** | `frontSeg≠rearSeg` → F−R cru spika; cap embicada 12u + step 12u ainda mergulha |

**Referência PS1:** rampa contínua; junção de laje **não** embica; pitch = tan da face, não ΔY virtual entre eixos em segs diferentes.

## Ações

| ID | Ação | Detalhe |
|----|------|---------|
| **J1** | Anti-embicada junção | Em `frontSeg≠rearSeg` ou `raw > grade+3u`: chord **só grade/last**, step dive **1.0u/f**, hold **10f** |
| **A1** | Pitch suave | Step normal 3u; blend 1/2; wheel rig **1.5°/f** dive, **2.5°** recover; delta jump 6u |
| **H1** | Heave colado sem snap | Body max **6u/f**; residual 1/2; air max **0.5u**; sem full-error snap 24u |
| **G1** | Grade manda na junção | Não publicar F−R spike para o wheel rig durante split |

## Gate de validação
1. Entrada de cada segmento no S: **sem** mergulho de nariz  
2. Meio da face: deslize contínuo, pneus colados  
3. Comparar visual com `Curva do S.mp4`  
