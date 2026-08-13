# Plano: memória da esteira e FPS ao longo do tempo

**Sintoma:** FPS decai conforme voltas; suspeita de reciclagem incompleta ao mobilizar/desmobilizar segmentos.

## Diagnóstico (audit)

| Área | Status | Risco |
|------|--------|-------|
| Mesh LWR por slot | Recycle via swap + `RecycleRuntimeState` | Baixo (capacity floors) |
| Metadados do slot no drop | **Não limpa walls/workingSet/designGeo** | **Alto** — cache stale + capacity ratchet |
| Sync slide scratch geo | **Sempre high mesh** no fallback | **Alto** — pico LWR + custo draw |
| VDP1/CRAM | Recycle full **off**; palette end-frame **off** | Médio — high-water sobe |
| Family retire end-frame | Grace 1, strict face ref | OK se metadata limpa |

## Ações (ordem de impacto)

1. **Demobilizar metadados no commit do slide** (walls, working set, design geo tier)
2. **Sync `BuildSegmentIntoSlideScratch` com geo tier por rank** (low no tail)
3. **Palette recycle sob pressão de memória** (sem full-heap thrash)
4. **Compact walls/workingSet se capacity ≫ uso** no demobilize
5. Observabilidade (já existe telemetria RU/WM)

## Implementado

| Ação | Status |
|------|--------|
| `DemobilizeSegmentSlotMetadata` no commit do slide | Feito |
| Walls + working set clear + shrink capacity | Feito |
| Design geo tier copiado no drop | Feito |
| `BuildSegmentIntoSlideScratch(..., designGeoTier)` low no tail | Feito |
| `RecycleRuntimeState(true)` no demobilize mesh | Feito |
| Palette recycle sob pressão HWR | Feito |
| Rebuild `BuildDrop/Interlagos_racing.bin` | Feito |

## Como validar
1. Dar 3–5 voltas; FPS e LWR free não devem decair monotônico.
2. Overlay (se ligado): `RU` reuses vs fresh; free HWR/LWR estáveis.
3. Sem flash roxo de paleta no caminho normal (só sob pressão).
