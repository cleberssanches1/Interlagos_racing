# Plano — 3 Levels of Design (lod_0 / lod_1 / lod_2)

**Data:** 2026-08-01  
**Backup:** `tools/_backup_lod3_20260801_150746/`

## 1. Estado atual

| Etapa | Comportamento |
|-------|----------------|
| Fonte OBJ | `obj_64` / `obj_32` / `obj_16` / `obj_8` |
| GEO | Um `S###.GEO` por segmento (último LOD processado sobrescreve) |
| MAT | `S###M8/16/32/64.MAT` por tamanho de textura |
| Runtime janela 20 | ranks **0–9 → 64×64**, ranks **10–19 → 32×32** |
| RDR | Um `S###.RDR` (malha + family ids) |

## 2. Alvo (3 design LODs)

| Design | Pasta | Uso na janela (ranks 0 = mais perto do carro) |
|--------|-------|-----------------------------------------------|
| **lod_0** | `...\result\lod_0` | Ranks **0–1** (1º e 2º): **mais faces** (solo/parede) + texturas 64 |
| **lod_1** | `...\result\lod_1` | Ranks **2–9** (3º–10º): malha mid + texturas **64×64** |
| **lod_2** | `...\result\lod_2` | Ranks **10–19** (11º–20º): malha far + texturas **32×32** |

### Saturn-smart (geometria vs textura)

- **GEO de runtime (`S###.GEO` / RDR)** gerado a partir de **lod_0** (máx. faces) para **toda** a esteira na 1ª entrega: MapHeight/paredes usam a malha densa sem dual-RDR (evita 2× pack + rebuild de mesh no SH2).
- **Texturas** seguem o rank 3-bandas: 0–9 = 64, 10–19 = 32 (lod_1/lod_2 ARQ alimentam o pipeline de TGA).
- **Fase 2 (opcional):** `S###L.GEO` + `S###L.RDR` para ranks 2–19 com malha lod_1/2 se LWR apertar.

## 3. Esteira (leve)

```
rank:  0  1 | 2  3  4  5  6  7  8  9 | 10 11 ... 19
tex:   64 64 | 64 ............... 64 | 32  ........ 32
geo:   high  | high (fase1) / low (fase2)
bordas:     1→2 (só se dual geo)     9→10 (32↔64 tex)
```

No **slide** de 1 segmento: só as bordas de banda mudam de target LOD → `QueuePendingStabilizedLodRank` nos ranks 9 e 10 (já existente). Sem rebuild full da janela.

## 4. Arquivos

- `tools/build_all_nya_geo_mat.ps1` — pastas lod_*, ordem de GEO/MAT  
- `tools/generate_segment_component.ps1` — `-SkipGeo`  
- `tools/build_geo_mat_from_lod_sets.ps1` — mapear lod_*  
- `src/track_system.cxx` / `track_streaming_policy.hpp` — ranks 0–1 / 2–9 / 10–19  

## 5. Fase 2 (implementada) — dual mesh

| Asset | Conteúdo |
|-------|----------|
| `S###.GEO` / `S###.RDR` / `TRKRDR.BIN` | lod_0 (alta) |
| `S###L.GEO` / `S###L.RDR` / `TRKRDRL.BIN` | lod_1 (média) |
| `S###M64` / `S###LM64` | texturas 64 |
| `S###M32` / `S###LM32` | texturas 32 |

Runtime: rank 0–1 carrega **high** RDR; rank 2+ carrega **low** RDR (fallback high se L ausente).  
Borda rank 1↔2: `ApplyStabilizedLodForLogicalRank` faz **rebuild de 1 slot** (não a janela inteira).

## 6. Validação

1. Rebuild: `build_all_nya_geo_mat.ps1 -RebuildSegmentsMap`  
2. Conferir `S###.GEO`, `S###L.GEO`, `TRKRDR.BIN`, `TRKRDRL.BIN`  
3. Emulador: 1–2 densos; 3–20 malha L; textura 32 a partir do 11º  
