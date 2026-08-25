# Plano executado — Interlagos low poly com envelope 8×8

## Alvo efetivamente encontrado

O caminho informado termina em uma pasta com extensão `.obj`. Dentro dela existe o arquivo:

`INTERLAGOS_mundo_all_sem_objetos_antigas.obj`

Não existe um arquivo interno terminado em `antigos.obj`. O processo utilizou o único OBJ disponível e preservou o original.

## Objetivo

Criar uma variante portátil do modelo low poly em que todos os materiais usados tenham uma textura 8×8 válida, sem alterar posições, UVs, normais ou faces.

## Auditoria da origem

| Métrica | Valor |
|---|---:|
| Objetos com faces | 1 |
| Vértices geométricos | 842 |
| Coordenadas UV | 767 |
| Normais | 368 |
| Faces | 405 |
| Quads | 389 |
| Triângulos | 1 |
| Pentágonos | 5 |
| Hexágonos | 5 |
| Heptágonos | 3 |
| Decágonos | 1 |
| Dodecágonos | 1 |
| Faces de área zero | 13 |
| Arestas não manifold | 7 |
| Maior grau de vértice | 5 |
| Vértices com mais de 4 ligações | 10 |
| Slots de material usados | 155, contando o slot vazio |
| Faces no slot vazio | 154 |
| Mapas de textura únicos usados | 16 |

Esses defeitos topológicos foram documentados, mas não corrigidos. Corrigi-los alteraria a geometria e fugiria do objetivo de envelopamento.

## Estratégia executada

1. Preservar literalmente todas as linhas `v`, `vt`, `vn` e `f` do OBJ.
2. Substituir apenas a referência `mtllib` pela nova MTL portátil.
3. Converter o `usemtl` vazio em um material explícito chamado `SEM_MATERIAL_8X8`.
4. Usar como fonte as 16 texturas 128×128 já preparadas para Interlagos.
5. Reduzir cada fonte para exatamente 8×8 com Lanczos, nitidez moderada e no máximo 16 cores.
6. Criar três texturas sólidas compartilhadas para cinza neutro, cinza-claro e faixa amarela.
7. Reescrever todos os `map_Kd` como caminhos relativos à pasta `ARQ_TGA_8X8_ENVELOPE`.
8. Validar o resultado com um parser independente e com importação real no Blender 4.3.

## Resultado

| Métrica | Resultado |
|---|---:|
| Vértices | 842, idênticos |
| UVs | 767, idênticas |
| Normais | 368, idênticas |
| Faces | 405, idênticas |
| Materiais definidos | 155 |
| Materiais sem textura | 0 |
| Texturas únicas | 19 |
| Texturas fora de 8×8 | 0 |
| Caminhos absolutos no MTL | 0 |

O Blender importou o pacote como um objeto com 842 vértices, 405 polígonos, 155 materiais e 19 imagens 8×8.

## Orçamento de textura

Para 19 texturas de 8×8, sem contar CLUTs e alinhamentos do conversor:

- 4 bpp: aproximadamente 608 bytes;
- 8 bpp: aproximadamente 1.216 bytes;
- TGA fonte 24 bpp: 3.648 bytes de pixels.

Os TGAs entregues são fontes TrueColor. A conversão final para o formato de VDP1 deve escolher 4 bpp ou 8 bpp conforme o pipeline do projeto.

## Arquivos gerados

- `INTERLAGOS_mundo_all_sem_objetos_antigas_8x8_enveloped.obj`
- `INTERLAGOS_mundo_all_sem_objetos_antigas_8x8_enveloped.mtl`
- `ARQ_TGA_8X8_ENVELOPE/`
- `INTERLAGOS_mundo_all_sem_objetos_antigas_8x8_enveloped_report.json`
- `INTERLAGOS_mundo_all_sem_objetos_antigas_8x8_enveloped_textures_preview.png`

O processo é reproduzível com `tools/build_interlagos_lowpoly_8x8.py`.

## Próxima etapa recomendada

Antes da integração como LOD distante, criar uma segunda variante geométrica corrigindo separadamente:

1. as 13 faces de área zero;
2. os n-gons, escolhendo quads apropriados;
3. as sete arestas não manifold;
4. os dez vértices com grau maior que quatro;
5. a divisão do objeto único em segmentos adequados ao carregamento do jogo.

Essa correção deve gerar outro arquivo, mantendo este envelope 8×8 como referência visual estável.
