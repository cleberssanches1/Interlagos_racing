# Plano de ação — mapa espacial de faces e tipo de solo

## Objetivo

Localizar a face sob o carro sem repetir uma varredura geométrica completa por
roda e sem duplicar os vértices já armazenados no `GEO.BIN`. O índice precisa
ser criado offline, durante o pipeline de assets, e ter impacto desprezível em
Low/High Work RAM.

## Diagnóstico

- `segments_map.json` contém o catálogo `familyId → tipo de solo`, mas seu vetor
  legado por face pode ficar defasado após composição e deduplicação do GEO.
- `SCMAP.BIN` resume tipos por segmento; no runtime ele elimina segmentos
  incompatíveis, mas ainda obriga a percorrer faces do segmento escolhido.
- `FindSurfaceYByFamilySet` calculava ponto-no-triângulo e plano para cada face
  candidata. Quatro sondas do carro multiplicam esse custo.
- NYA e o vetor por face do JSON não podem ser autoridades do índice final. O
  mapa deve seguir exatamente o par `S###.GEO`/`S###M64.MAT` consumido pelo
  runtime.

## Plano executado

1. Usar `S###.GEO` como fonte de vértices/índices, `S###M64.MAT` como fonte da
   família por face e `segments_map.json` apenas como catálogo de tipo do solo.
2. Validar obrigatoriamente `faceCount GEO == faceCount MAT`; divergências do
   vetor legado do JSON são registradas no relatório, sem invalidar o asset.
3. Excluir offline faces desconhecidas, paredes e faces não dirigíveis.
4. Gerar `FSMAP.BIN` com um diretório ordenado por segmento e registros de 12
   bytes por face útil.
5. Quantizar AABB XZ conservadora em passos de 1/16 de unidade, relativa ao
   centro espacial de cada segmento.
6. Carregar o arquivo diretamente na Cart RAM, sem vetor persistente em Work
   RAM e sem copiar registros.
7. No runtime, filtrar por tipo e AABB antes do teste ponto-no-triângulo e do
   cálculo do plano.
8. Manter fallback automático para a busca anterior quando o arquivo estiver
   ausente, inválido, desativado ou com `faceCount` incompatível.
9. Validar parser em teste host, compilar com o toolchain SH-2 e medir o ISO.

## Formato FSM1

### Cabeçalho (20 bytes)

- magic `FSM1`, versão, tamanho de cabeçalho;
- quantidade de segmentos;
- tamanho fixo do registro (12 bytes);
- total de registros e offset do diretório.

### Diretório por segmento (24 bytes)

- `segmentId`, `faceCount` original e quantidade de faces indexadas;
- deslocamento de quantização;
- origem X/Z em 16.16;
- offset dos registros.

### Registro por face (12 bytes)

- `faceIndex` original do GEO;
- `minX`, `maxX`, `minZ`, `maxZ` quantizados;
- `surfaceType` e flags (`driveable`, `asphalt`, `offroad`, `triangle`).

Nenhum XYZ ou polígono é duplicado.

## Resultado medido

- 290 segmentos e 18.278 faces de origem na geração atual;
- 12.827 faces de piso dirigível indexadas;
- 5.451 faces descartadas offline;
- 126 segmentos com JSON ausente/defasado, todos resolvidos pelo GEO/MAT;
- segmento 35 ausente no JSON atual, mas preservado no índice por existir no GEO;
- `FSMAP.BIN`: 160.904 bytes na Cart RAM;
- Work RAM persistente adicional: um ponteiro e um tamanho (8 bytes);
- ELF: 1.455.037 bytes;
- ISO: 16.187.392 bytes;
- compilação SH-2 e teste host concluídos com sucesso.

## Critérios de segurança

- O mapa nunca decide a altura final: ele apenas reduz candidatos. A altura
  continua sendo calculada no plano geométrico real do GEO.
- AABB usa arredondamento para fora; portanto não remove uma face válida por
  erro de quantização.
- Qualquer inconsistência volta à varredura existente.
- A ativação pode ser revertida em compilação com
  `PHYS_FACE_SURFACE_MAP_RUNTIME=0`, mantendo o asset para auditoria offline.

## Próxima etapa física

Depois de validar em emulador que renderização e streaming mantêm margem, o
`(segmentId, faceIndex)` retornado por cada roda deve ganhar cache próprio. A
busca espacial resolve o custo e a identificação; caches separados evitam que
as quatro rodas invalidem uma única face compartilhada e permitirão transição
contínua entre faces vizinhas sem simular degraus.
