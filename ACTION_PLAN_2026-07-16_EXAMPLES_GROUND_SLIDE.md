# Plano de ação — solo + slide (inspirado em Projetos_Exemplos)

Data: 2026-07-16  
Referências: REDRIVER2 (`MapHeight` / `handling.c`), SlaveDriver (hitscan/floor), VDrift/TORCS (wheel contacts).

## Lições dos exemplos

### REDRIVER2
- **Altura do solo**: `MapHeight(&pos)` **todo frame** por roda/ponto — sem cache multi-frame da Y.
- **Não congela XZ** se altura falha; o carro continua no plano.
- Streaming de mapa em tiles sob o player (working set), com recycle — preferir **avançar o set** a travar.

### Interlagos (problemas observados)
1. **Escadinha / preso no declive**: ao perder probe, `ApplyVerticalAdhesion` **teleportava XZ** para `lastStable` → carro “gruda” na ladeira; Y com lag/snap.
2. **Slide para**: abort hard em textura/boundary/RDR-only; backlog 1/frame; freeze prolongado.

## Ações implementadas

| Área | Mudança |
|------|---------|
| Solo Y | Snap direto `Y = surfaceYTarget` (estilo MapHeight) |
| Solo miss | Hold Y por 8 frames; **nunca** trava XZ em lastStable |
| Probe | Sem reuse de Y; 4-probe fora de flat lento |
| Pitch | Já mais agressivo (deadzone/filtro/max) |
| Prefetch | Metadata free of budget; SDR se RDR falhar |
| Geometria | SDR fallback se RDR falhar |
| Tail textura | Soft admit (pop-in) em vez de abortar slide |
| Boundary LOD | `continue` se falhar — não aborta slide |
| Catch-up | Até 3 slides se backlog alto; limpa cooldowns; flush VDP1 antes |

## Validação
1. Declive: Y contínuo + nariz inclina; sem prender XZ.
2. Volta longa: janela não congela após 60+; aceitável textura soft no tail.
3. HUD: `PKG tail soft` ocasional OK; sem `PKG pf fail` permanente.

## Se ainda falhar
- Inspecionar asset CD do id que trava (`RDR`/`SDR` entry missing).
- Subir janela / reduzir 64×64 near se `tx` satura.
- Compaction VDP1 limitada sob `tx` alto.
