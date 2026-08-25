# Plano de correção da orientação UV OBJ -> Saturn

## Sintoma reproduzido

- A face `Zebra_64.038` do `seg_069.obj` usa `F05464.TGA`.
- No OBJ, seus UVs estão aproximadamente entre `6.28` e `10.88` e formam uma ilha rotacionada.
- `generate_segment_component.ps1` converte cada coordenada diretamente para `int16` normalizado e satura valores acima de `1.0` em `32767`.
- Como consequência, os quatro UVs dessa face chegam ao `S069.GEO` como `(32767,32767)`.
- `generate_segment_draw_ready.ps1` não consegue mais determinar a orientação e mantém a ordem bruta dos vértices. Com `sprNoflip`, a textura horizontal é aplicada 90 graus fora da orientação vista no Blender.

## Causa raiz

O pipeline usa UV apenas como metadado temporário para escolher qual canto geométrico receberá cada canto da textura TGA completa. O formato draw-ready não armazena UV por vértice. A quantização atual destrói esse metadado quando o Blender exporta UVs repetidos, negativos ou acima de `1.0`.

Além disso, a canonização atual testa simultaneamente duas convenções de V e permite permutações espelhadas. Em quads simétricos ou rotacionados isso torna o resultado ambíguo e pode inverter o winding definido no OBJ.

O primeiro refinamento substituiu essa ambiguidade pelo canto mais próximo de `(minU,minV)`, mas esse critério ainda era incorreto para faces já ordenadas. No `seg_207`, as faces `F02664.TGA` já começavam pela aresta horizontal `+U`; mover o canto inferior esquerdo para o primeiro slot girava a textura completa em 90 graus.

## Correção

1. Normalizar U e V localmente por face antes da quantização para `int16`, preservando a posição relativa dos quatro cantos mesmo quando os UVs estão fora de `[0,1]`.
2. Para quads, escolher como primeira a aresta UV cíclica mais alinhada com `+U`. Isso preserva faces já corretas e recupera ilhas rotacionadas sem depender de um canto absoluto.
3. Aplicar somente rotação cíclica dos quatro índices; não espelhar e não inverter o winding.
4. Manter triângulos e faces degeneradas na ordem original.
5. Manter as correções manuais existentes como compatibilidade posterior à canonização automática.

## Validação

- Caso de regressão principal: `seg_069`, face zebra, deve passar de `1,5,9,8` para `5,9,8,1` no SDR (índices zero-based).
- Confirmar que os quatro UVs da face deixam de ser idênticos no GEO.
- Auditar todos os quads para garantir que cada mudança é uma rotação cíclica, nunca um espelhamento.
- Executar `build_all_nya_geo_mat.ps1` e o gate `validate-interlagos-saturn.ps1`.
- Confirmar que o ISO final mantém o tamanho estável de `4134912` bytes.

## Resultado da execução (2026-08-24)

- Backup anterior à alteração criado em `C:\saturn\backups\Interlagos_uv_orientation_converter_20260824_145639`.
- Caso `seg_069`, face 5: GEO com quatro pares UV distintos e SDR `5,9,8,1`, conforme esperado.
- Auditoria de 580 pares GEO/SDR e 9.988 quads: zero espelhamentos, zero ordens inválidas e zero divergências de contagem.
- Pipeline completo: 290 segmentos e validação interna final `OK`.
- Build SH-2, testes host e validações de headers: `PASS`.
- ISO reproduzido duas vezes com `7534592` bytes. O gate histórico de `4134912` está desatualizado: o próprio `HEAD` contém aproximadamente 7,13 MB apenas em arquivos rastreados de `cd/data`. A constante não foi alterada neste trabalho.

## Refinamento por direção de aresta (2026-08-24)

- Backup criado em `C:\saturn\backups\Interlagos_uv_edge_direction_fix_20260824_163309`.
- `seg_207`, faces 2 e 3 (`F02664.TGA`): a ordem SDR agora permanece igual à ordem OBJ/GEO.
- `seg_069`, face 5 (`F05464.TGA`): a ordem SDR continua `5,9,8,1`.
- Auditoria high + low: 580 pares, 9.988 quads, zero espelhamentos/ordens inválidas e zero divergências de contagem.
- SDR/RDR/BDR e packs finais reconstruídos com sucesso.
- ISO gerado com `8927232` bytes; testes host e headers SH-2 passaram. O aumento do ISO vem dos quatro aliases do mapa atual com `600938` bytes cada, não dos packs de geometria, cujos tamanhos permaneceram estáveis.
