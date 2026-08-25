# Plano de correção do `build_all`: orientação UV, famílias novas e origem das texturas

## Objetivo

Tornar o `tools/build_all_nya_geo_mat.ps1` um build integral e reproduzível, com estas garantias:

1. a orientação visível da textura no Saturn corresponde à orientação do UV exportado pelo Blender;
2. `segments_map.json` e `textureFamilies` são sempre recriados a partir dos OBJ/MTL da execução atual;
3. nenhuma textura de entrada é lida fora destas três pastas autorizadas:
   - `C:\Models\png\sectors\result\lod_0\ARQ_TGA`
   - `C:\Models\png\sectors\result\lod_1\ARQ_TGA`
   - `C:\Models\png\sectors\result\lod_2\ARQ_TGA`
4. cada textura carregada em runtime tem origem, dimensão e LOD comprováveis por relatório;
5. nenhum arquivo residual de uma execução anterior entra no pacote novo.

## Diagnóstico confirmado

### 1. Segmento 63, textura `F05464.TGA`

O OBJ `lod_0\seg_063.obj` usa o material `Zebra_64.032`, e o MTL associa esse material a `F05464.TGA`. Há duas faces dessa família no segmento, nos índices 5 e 6 (índices iniciando em zero).

As duas faces possuem ilhas UV inclinadas e fora do intervalo 0..1. A normalização adicionada ao GEO preserva a direção relativa dessas coordenadas, mas o formato SDR/RDR não conserva UV por vértice. Ele conserva apenas os quatro vértices do polígono e o `familyId`; a textura inteira é aplicada pelo VDP1 aos quatro cantos.

A regra atual de `Reorder-QuadVerticesFromUv`, em `generate_segment_draw_ready.ps1`, escolhe como primeiro canto a aresta mais alinhada com `+U`. No segmento 63 ela produz comportamentos diferentes para as duas faces de `F05464`: uma mantém a ordem do OBJ e a outra sofre uma rotação cíclica de um canto. Essa heurística não determina de forma inequívoca qual canto UV deve ocupar o canto A do quad do VDP1, especialmente em ilhas inclinadas ou repetidas. É a causa direta da orientação divergente.

Limitação a considerar: rotação de 0/90/180/270 graus pode ser representada mudando ciclicamente os quatro vértices. Espelhamento, recorte e repetição UV arbitrária não podem ser reproduzidos fielmente enquanto o formato de desenho não carregar transformação adicional ou uma textura preparada por face.

### 2. Famílias não são realmente recriadas pelo build atual

O parâmetro `RebuildSegmentsMap` é desativado por padrão. Nessa condição, `build_all_nya_geo_mat.ps1` procura nomes antigos como `SAP.json`, `segments_map_before_rebuild.json`, `segments_map` e outros, e copia um deles sobre o JSON recém-exportado.

Além disso, `update_segments_map_with_renamed_textures.ps1` inicia em `json.textureFamilies`, mantém os IDs conhecidos e anexa novas famílias. Ele também adiciona famílias para texturas presentes no manifesto, ainda que não sejam referenciadas pelo OBJ atual.

O resultado atual comprova esse acúmulo: existem 75 famílias, mas seus IDs vão de 1 a 480 e não são contíguos. Isso é incompatível com um catálogo recriado do zero.

Há ainda uma reconstrução opcional no final do `build_all`, depois que GEO, MAT, SDR, RDR e TEXBANK já foram gerados. Alterar o mapa nesse ponto pode fazer o JSON publicado divergir dos binários que foram produzidos antes dele.

### 3. O build procura texturas em locais não autorizados

Atualmente há buscas ou fallbacks para:

- `obj_64\ARQ_TGA`, `obj_32\ARQ_TGA`, `obj_16\ARQ_TGA` e `obj_8\ARQ_TGA`;
- `pacote_rancing`;
- `cd\data`;
- o `TextureRoot` legado no OneDrive;
- diretórios `8_ren`, `16_ren`, `32_ren` e `64_ren` de uma execução anterior.

Esses fallbacks aparecem principalmente em:

- `copy_ren_textures_to_data.ps1`;
- `update_segments_map_with_renamed_textures.ps1`;
- `generate_texbanks.ps1`;
- resolução do diretório OBJ em `build_all_nya_geo_mat.ps1`.

O caminho absoluto gravado em `map_Kd` no MTL deve servir somente para obter o nome do arquivo, nunca como origem de leitura. O arquivo deve ser novamente resolvido dentro da lista autorizada.

### 4. Texturas 32x32 observadas depois do segmento 210

O `TBK64.BIN` atual contém 31 entradas cuja imagem não mede 64x64. Algumas são faixas não quadradas legítimas, mas duas entradas são exatamente 32x32:

- família 11, `torcida`, fonte `F05264.TGA`;
- família 138, `GramaLow`, fonte `F06864.TGA`.

Os próprios arquivos `F05264.TGA` e `F06864.TGA` em `lod_0\ARQ_TGA` e `lod_1\ARQ_TGA` já medem 32x32. Portanto, para essas duas famílias, o build não está reduzindo uma imagem 64x64: está aceitando uma entrada 32x32 no banco de alta resolução sem avisar. Não existe detalhe 64x64 recuperável nessas pastas; um redimensionamento automático apenas ampliaria os mesmos pixels.

Também não há uma mudança fixa de fonte no segmento 210 nos artefatos examinados. Os segmentos 210 a 225 referenciam majoritariamente entradas 64 de fato. Em runtime, porém, a janela visível é configurada com 10 segmentos usando `TBK64` e os 6 mais distantes usando `TBK32`. A escolha é por posição lógica na janela, não pelo ID do segmento. Os `faceRankOffsets` do BDR também podem fazer algumas faces cruzarem o limite 64/32 na borda entre as bandas. Por isso, o sintoma precisa ser medido em runtime antes de atribuí-lo somente ao empacotamento.

## Plano de ação

### Fase 0 — Backup e linha de base

1. Criar backup com data e hora dos scripts que serão alterados e dos manifests atuais.
2. Guardar hashes de `segments_map.json`, `TBK32.BIN`, `TBK64.BIN`, `MAT32.BIN`, `MAT64.BIN`, `TRKRDR.BIN` e dos arquivos do segmento 63.
3. Exportar um relatório da situação atual contendo família, segmento, face, arquivo fonte, dimensão e hash.
4. Não apagar o pacote atual; produzir a primeira versão corrigida em uma pasta de staging separada.

### Fase 1 — Transformar `build_all` em build integral

1. Tornar a reconstrução total o comportamento obrigatório de `build_all_nya_geo_mat.ps1`.
2. Remover do fluxo integral a cópia de qualquer `segments_map`, `SAP`, `segmap` ou catálogo anterior.
3. Não aceitar `SkipNyaExport` no fluxo integral. Se um modo incremental continuar necessário, expô-lo em outro script com nome explícito e nunca chamá-lo pelo `_all`.
4. Executar toda a geração dentro de um staging vazio e exclusivo da execução.
5. Construir o catálogo de famílias antes de GEO/MAT/SDR/RDR/TEXBANK, e não reconstruí-lo novamente no final.
6. Gerar todos os artefatos downstream usando exatamente o mesmo hash do catálogo de famílias.
7. Publicar staging para `pacote_rancing` e `cd\data` somente depois de todas as validações passarem.

### Fase 2 — Recriar as famílias de forma determinística

1. Ler somente os OBJ/MTL atuais de `lod_0`, usando `lod_1` apenas para validação de correspondência.
2. Criar uma chave canônica por associação `material -> basename(map_Kd)`, sem aproveitar IDs anteriores.
3. Manter somente famílias referenciadas por pelo menos uma face atual.
4. Ordenar as chaves canônicas antes de atribuir IDs, para duas execuções idênticas produzirem os mesmos IDs.
5. Quando o mesmo nome de material apontar para TGAs diferentes, criar famílias separadas de forma explícita e reportar a colisão.
6. Gerar `segments_map.json`, MAT, GEO, SDR, RDR e bancos somente depois que o catálogo final estiver fechado.
7. Validar que os IDs são únicos, contíguos, que toda face aponta para uma família existente e que nenhuma família está órfã.

### Fase 3 — Aplicar uma política rígida de origem de textura

1. Substituir `TextureRoot` e todos os fallbacks por uma lista fechada com as três pastas autorizadas.
2. Política de resolução:
   - banco alto: procurar primeiro em `lod_0\ARQ_TGA`, depois em `lod_1\ARQ_TGA`;
   - banco baixo: procurar somente em `lod_2\ARQ_TGA`;
   - variantes derivadas de 16 e 8, se ainda forem exigidas pelo pacote, devem ser geradas no staging a partir de uma dessas fontes autorizadas, nunca buscadas em `obj_16`, `obj_8`, pacote ou OneDrive.
3. Remover as buscas em `obj_*`, `*_ren`, `pacote_rancing`, `cd\data` e raiz legada.
4. Resolver `map_Kd` apenas pelo basename, impedindo que o caminho absoluto do Blender escape da lista autorizada.
5. Falhar o build quando uma textura não existir na pasta esperada; não emitir apenas `AVISO` e continuar com banco incompleto.
6. Gravar no manifesto de cada entrada: caminho fonte absoluto, pasta LOD, nome, SHA-256, largura, altura, profundidade, destino e eventual transformação aplicada.
7. Adicionar validação final que rejeite qualquer `sourcePath` fora das três raízes.

### Fase 4 — Corrigir e testar a orientação UV

1. Criar uma textura de calibração direcional 64x64 com os cantos identificados (`TL`, `TR`, `BR`, `BL`) e setas `+U/+V`.
2. Renderizar um quad controlado no Saturn para determinar de forma inequívoca a correspondência entre:
   - ordem dos índices no OBJ;
   - inversão do eixo V do Blender/TGA;
   - cantos A/B/C/D usados por `sprNoflip` no VDP1.
3. Substituir a heurística “aresta mais alinhada com +U” por uma transformação determinística baseada nessa calibração.
4. Avaliar as quatro rotações cíclicas preservando o winding. Usar os dois eixos UV e o canto de origem; não decidir apenas pela direção de uma aresta.
5. Gerar `uv_orientation_report.json` com segmento, face, material, TGA, UV bruto, rotação escolhida, winding, pontuação e nível de confiança.
6. Quando a ilha exigir espelhamento, recorte ou repetição não representável no RDR atual, falhar em modo estrito e listar a face. A correção então deve ser uma destas, escolhida explicitamente:
   - estender o formato/renderizador para carregar rotação/flip por face; ou
   - preparar uma textura específica para aquela face dentro do staging.
7. Criar regressões obrigatórias para:
   - segmento 63, faces 5 e 6, `F05464.TGA`;
   - segmento 69, face já usada como referência para `F05464.TGA`;
   - segmento 207, faces de `F02664.TGA`;
   - uma face sem rotação, uma com cada rotação de 90 graus e uma UV ambígua que deve falhar.

### Fase 5 — Resolver e diagnosticar 32x32 versus 64x64

1. Adicionar um validador de dimensão antes de criar os TEXBANKs.
2. Criar uma política explícita de dimensão por família/tier. Ela é necessária porque várias imagens 64 são propositalmente não quadradas; inferir apenas pelo sufixo geraria falsos erros.
3. Marcar `F05264.TGA` e `F06864.TGA` como pendências de fonte de alta resolução: a correção preferida é substituir os arquivos 32x32 por fontes 64x64 dentro de `lod_0\ARQ_TGA` e `lod_1\ARQ_TGA`.
4. Não ampliar silenciosamente 32x32 para 64x64. Se um upscale provisório for necessário, exigir uma opção explícita e registrar `sourceDimensions` e `outputDimensions` no manifesto.
5. Instrumentar temporariamente o runtime para registrar, nos segmentos 205 a 215:
   - ID do segmento e índice da face;
   - `familyId`;
   - posição lógica na janela e `faceRankOffset`;
   - LOD solicitado, banco realmente carregado e dimensão decodificada;
   - ocorrência de fallback ou slot reaproveitado.
6. Executar dois testes separados:
   - todas as faces próximas forçadas para LOD64, para validar exclusivamente o pacote;
   - política normal 10×64 + 6×32, para validar a transição da janela.
7. Se uma face próxima resolver `TBK32`, corrigir a seleção de LOD/offset ou o cache de slots. Se a face estiver na banda distante, o 32x32 é comportamento configurado e deve ser tratado como decisão de design, não como erro do `build_all`.

### Fase 6 — Validação final e publicação

1. Executar duas vezes o build integral em staging vazio e comparar hashes. Entradas idênticas devem produzir catálogo, mapas e binários idênticos, descontando apenas timestamps do relatório.
2. Confirmar:
   - nenhuma família reutilizada ou órfã;
   - nenhuma origem fora da whitelist;
   - nenhum TEXBANK incompleto;
   - dimensões conformes à política;
   - referências de família idênticas entre JSON, MAT, SDR/RDR e TEXBANK;
   - regressões UV aprovadas.
3. Rodar os validadores estáveis do projeto e gerar a ISO.
4. Testar no emulador os segmentos 63, 69, 207 e a passagem 205–215 com telemetria de LOD.
5. Somente depois desses testes substituir os artefatos publicados. Manter o backup e um relatório de diferenças para rollback.

## Critérios de aceite

- `build_all` não contém modo implícito de reaproveitamento.
- Duas execuções limpas geram a mesma tabela de famílias e os mesmos vínculos face/família.
- Todo item de TEXBANK comprova que sua fonte pertence a `lod_0`, `lod_1` ou `lod_2` autorizados.
- Ausência de textura, colisão de material, dimensão inesperada e UV ambígua interrompem o build com erro acionável.
- As duas faces `F05464.TGA` do segmento 63 coincidem visualmente com o Blender.
- As faces `F02664.TGA` do segmento 207 continuam corretas.
- Uma face situada na banda 64 nunca recebe silenciosamente uma entrada do banco 32.
- O relatório explica qualquer uso intencional de 32x32 pela posição LOD da janela.

## Ordem recomendada de implementação

1. Fases 0, 1 e 2: eliminar estado antigo e congelar um catálogo único.
2. Fase 3: fechar a whitelist e tornar a origem auditável.
3. Fase 5: separar erro de fonte, erro de banco e seleção LOD de runtime.
4. Fase 4: calibrar e corrigir a orientação, protegida pelas regressões.
5. Fase 6: repetir build, gerar ISO e validar no emulador.

Essa ordem evita validar a orientação contra famílias ou texturas que ainda podem mudar durante a própria execução.
