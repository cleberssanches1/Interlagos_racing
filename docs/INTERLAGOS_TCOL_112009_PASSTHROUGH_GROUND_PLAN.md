# Plano: pass-through + solo degradado (vídeo 112009)

Ver `plan.md` da sessão. Resumo da implementação iniciada:

## Por que TCOL falhou vs GEO

GEO colidia com o mesh **visual**. TCOL após cull inner-driveable ficou só com void (~300u) → `wh:0` ao atravessar a barreira vista. `g:1` aceita types 1/2/3 (runoff), não “no ribbon”.

## Feito

1. Stems reativados + rim-keep no cull inner-driveable + max-dist.
2. Contorno type=1 completo de novo (asfalto→escape ~69u na reta) com mid-lane 48u.
3. Anti-túnel restrito: `segmentsCross` só se `minEndDist ≤ 2×radius`.
4. HUD `st:` (surface type).

## TCOL rebuild

- walls **1368 → 5747**; SEG2 nearest **~69u** (era ~302u); miolo S sem walls &lt;48u do centróide.
- ISO: `BuildDrop_tcol_rim`.

## Reteste

1. Barreira lateral cedo: `wh:1`, não atravessa.
2. Miolo S: `wh:0`, sem stuck.
3. Fora no ribbon: `st` 2/3 legível.
