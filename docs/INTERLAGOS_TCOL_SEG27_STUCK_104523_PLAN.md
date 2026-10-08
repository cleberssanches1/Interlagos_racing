# Plano: stuck SEG27 no S (vídeo 104523) — pós-wallfix ainda trava

Ver plano de sessão aprovado. Resumo da implementação (2026-10-08):

## Feito no código (Fases 1–2)

1. **Desligado `segmentsCross`** em `track_system.cxx` (proximidade + sweep curto apenas).
2. **Invalidar prev** se cheb > 16u (não só clamp); `InvalidateWallQueryPrevPosition` em seedΔ/jump.
3. **HUD** `GP wh wx wz ws wd g` (`wd` = dist aprox. em units).
4. **Cooldown no-support** 12 frames após wall hit; skip no-support com throttle e speed≈0.
5. **Force gear 1** se grounded + throttle + N + speed baixa.

## Reteste pedido

- Confirmar HUD com `ws`/`wd`/`g`.
- Miolo SEG26–30 sem stuck.
- A/B opcional: `PHYS_WALL_COLLISION_RUNTIME=0` se ainda travar.
