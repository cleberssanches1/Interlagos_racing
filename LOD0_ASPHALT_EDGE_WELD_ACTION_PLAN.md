# Plano de ação — solda de arestas do asfalto LOD 0

## Objetivo

Eliminar microfrestas entre faces de asfalto no LOD 0 sem segunda malha de
colisão, sem custo no runtime Saturn e sem alterar UV, materiais, famílias ou
ordem de faces.

## Implementação

1. O `build_all_nya_geo_mat.ps1` classifica as faces por `surfaceTypeId`.
2. Antes de GEO/MAT/SDR, chama `weld_lod0_asphalt_edges.py` sobre os OBJ de
   `lod_0`.
3. A ferramenta reproduz a triangulação em leque do gerador, encontra somente
   arestas de faces `surfaceTypeId == 1` e compara suas duas pontas em 3D.
4. Quando as duas pontas correspondentes estão a até 0,25 unidade, grava a
   coordenada canônica determinística do menor `(segmento, vertice)`.
5. Desníveis reais e agrupamentos fora dessa tolerância são recusados.

## Garantias

- Não há criação ou remoção de vértices, faces, índices, UVs ou bindings.
- A colisão continua a ler a mesma geometria LOD 0, agora com coordenadas de
  borda coincidentes.
- Há backup do gerador e dos OBJ alterados antes de qualquer escrita.
- O relatório `lod0_asphalt_weld_report.json` registra a auditoria e as
  alterações aplicadas.

## Validação

1. Verificar o relatório da solda: nenhuma recusa e deslocamento máximo dentro
   da tolerância.
2. Gerar GEO, SDR, RDR e packs pelo `build_all`.
3. Confirmar integridade GEO/MAT e testar visualmente retas, aclives, declives
   e fronteiras de segmentos afetados.
