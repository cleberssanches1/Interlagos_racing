# Offline contact-plane test — SEG 27–50

**Data:** 2026-08-06  
**Fonte:** `Interlagos_racing_old/cd/data/SETORES/SEG_*.NYA`  
**Script:** one-shot Python (see session) / `tools/analyze_seg_slope_27_45.py` extended range  

## Método

Para cada segmento 27–50:

1. Ler NYA, faces com normal |ny| > 0.75 (chão).  
2. Medir Yspan, max tan da face (`sqrt(1-ny²)/|ny|`).  
3. Centro médio das faces chão e tan de caminho entre centros.  
4. Chord F−R esperado = `tan × wheelbase` com **wb = 75** (CAR1).

## Resultados resumidos

| Faixa | Observação |
|-------|------------|
| Junções 27–42 (auditoria anterior) | **Soldadas** (ΔY borda ≈ 0) — degrau não é furo de malha |
| Faces asfalto/chão por seg | Poucas (2–14); Yspan às vezes >150 (multi-band / não-só-pista) |
| Path tan 27→50 | Típico **0.05–0.20**, picos **~0.21** (27→28, 38→39, 41→42) |
| maxTan **por face** | Até **0.39–0.80** em alguns segs (inclui faces íngremes misturadas) |
| Chord F−R para tan 0.39 | **~29 u** ≈ **21°** pitch |
| Chord F−R para path tan 0.20 | **~15 u** ≈ **11°** |

## Implicação para o runtime

| Antes (cap 14 u / ~10.5°) | Efeito |
|---------------------------|--------|
| Malha pede até ~21–30 u de chord | Pitch **achatado** → rodas **descolam** do plano real |
| Lift body **2.0 + 0.25** | Ar sob o monocoque / pneus |

| Ajuste fino | Valor |
|-------------|--------|
| max attitude chord | **30 u** (~22°) |
| stair reject | **24 u** jump |
| ride base | **-0.125** |
| car lift | **-0.5** (era -2) |
| heave snap | **0.125 u** residual → colado |
| pitch track | filter 1, jump 12 u, 2.5°/f |

## Nota asset (ainda válida)

Subdividir asfalto 27–50 e garantir só faces dirigíveis no MapHeight reduz multi-band e picos de tan “falsos”. Junções já soldadas — prioridade é **runtime glue + famílias limpas**, não só soldar de novo.

## Validação no jogo

Overlay: `yF/yR` estáveis; `p` ~ tan×57; rodas visualmente no asfalto no trecho 27–50.  
Re-run offline após rebuild NYA: `python tools/analyze_seg_slope_27_45.py`
