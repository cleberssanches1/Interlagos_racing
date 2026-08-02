# Análise SEG_027–SEG_045 (inclinação / suavidade)

**Data:** 2026-07-24  
**Fonte NYA:** `Interlagos_racing_old/cd/data/SETORES` (único pack com SEG_027..045 completo no disco; mapa `.tmp_segments_map_test.json` confirma 12–22 faces/segmento).  
**Ferramenta:** `tools/analyze_seg_slope_27_45.py`

## Conclusão curta

| Pergunta | Resposta |
|----------|----------|
| A rampa 27→45 é “errada”? | **Há subida/descida real** ao longo do trecho, mas a amostragem por faces “quase horizontais” mostra **bandas Y enormes** e **mudanças bruscas de tan** entre segmentos. |
| Precisa mais faces no asfalto? | **Sim, recomendado** no asfalto da rampa (e soldar costuras) — não só por visual, mas para MapHeight/pitch estáveis. |
| Bob da câmera é só asset? | **Não.** Parte era filtro de pitch/grade-behind reativo a ruído; **corrigido no código**. Seams ainda podem tremer o **carro** se as lajes forem poucas e descontínuas. |

## Evidências

### Mapa (`segments_map`)
- Cada segmento 27–45: **~12–22 faces totais**, família asfalto (`familyId=1`) tipicamente **2 faces**.
- Um único pair de quads de asfalto por fatia ≈ **um plano** por ~100–300 unidades de caminho → o pitch do carro só muda “em degraus” na costura.

### NYA (normal |ny|>0.75, banda “topo” ±12)
- Vários segmentos reportam `roadSpan` **>150** (ex.: 31, 32, 33, 35–38, 43–45): faces classificadas como “chão” em **alturas muito diferentes** (asfalto + plataformas/patamares/tops).
- `tan` de caminho entre centros oscila com **|Δtan| até ~0.40** em costuras (27→28, 29→31, 33→35, 41→43…).
- Isso **não prova** erro de export 100%, mas mostra que **não há rampa monótona suave amostrável** só com 2 faces de asfalto + geometria mista.

## O que fazer no Blender / pipeline NYA

1. **Asfalto da rampa 27–45:** subdividir o piso ao longo do eixo da pista (não só um quad por setor). Meta prática: **4–8 faces de asfalto** por segmento na rampa, com vértices de borda **compartilhados** com o vizinho (mesmo Y na costura).  
2. **Soldar costuras:** na junção N|N+1, vértices de borda do asfalto com **mesmo Y** (tolerância &lt; 0.25 unidade).  
3. **Separar família/superfície:** o que não for pista dirigível não deve ser `asfalto` / `surfaceType asphalt` (evita MapHeight “pular” para um patamar).  
4. **Rebuild:**  
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_all_nya_geo_mat.ps1 -RebuildSegmentsMap`  
   e repor `SEG_*.NYA` no CD de runtime.  
5. Revalidar: `python tools/analyze_seg_slope_27_45.py` — alvo: `roadSpan` do asfalto **&lt; ~15** por segmento e `|Δtan|` entre segs **&lt; ~0.12**.

## Câmera (já ajustada nesta entrega)

- Deadzone pitch maior; grade só em rampa clara.  
- Pitch inicia em **0** (sem snap no boot).  
- Rate-limit **1°/frame** + blend mais lento.  
- Grade-behind só com pitch ≥ 6° (não no arranque plano).  
- Lift residual de grade só com pitch comprometido.

## Física do carro (se ainda tremer após asset)

- Manter filtros de grade/`topologyDrop` (já assimétricos).  
- Opcional futuro: filtro extra em `gradeTanRaw` só neste trecho — **secundário** vs. soldar asfalto.
