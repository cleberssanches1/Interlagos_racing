# LOD Texture Swap History

## Objetivo

Implementar troca de texturas por distancia para a pista, com foco inicial no `SEG_001`, usando pipeline componentizado e evitando dependencia de `.NYA` em runtime.

Arquitetura alvo:

- geometria em `.GEO`
- bindings de material por LOD em `.MAT`
- mapa de faces para familias de textura em `S001FAM.BIN` e `SMAP.TXT`
- texturas empacotadas em `TBK8.BIN`, `TBK16.BIN`, `TBK32.BIN`, `TBK64.BIN`
- fonte bruta de TGA pre-carregada no cartucho de 4 MB

## Estrategia Atual

O runtime da pista foi movido para um pipeline componentizado:

- o segmento 1 e carregado a partir de `S001.GEO`
- os materiais sao lidos de `S001M8.MAT` e slots por LOD sao montados dinamicamente
- os mapas de texturas por face sao aplicados no renderer por meio de:
  1. cache vindo de `SMAP.TXT`
  2. fallback robusto por `S001FAM.BIN`
  3. fallback final por `SMAP.TXT` novamente, se necessario

As texturas agora sao resolvidas a partir de:

- `RTMAP.TXT` para descobrir nomes renomeados e pre-carregar TGAs no cartucho
- `TBK*.BIN` para decode/upload principal em VDP1
- fallback entre LODs menores da mesma familia quando um LOD especifico falha

## O Que Ja Foi Feito

### 1. Remocao da dependencia de `.NYA` em runtime

- o objetivo de runtime passou a ser `.GEO` + `.MAT` + bins auxiliares
- a renderizacao da pista foi redirecionada para a estrutura componentizada
- `.NYA` ficou como artefato de geracao/exportacao, nao como formato principal de uso em jogo

### 2. Leitura do mapa de texturas (`SMAP.TXT`)

Problema encontrado:

- arquivos `.json` e nomes longos causaram dificuldade no Saturn/ISO9660
- leitura inicial retornava falhas ou dados truncados

Ajustes feitos:

- o mapa principal foi movido para `SMAP.TXT`
- foram testados varios candidatos de caminho para leitura em runtime
- a leitura de arquivos de CD foi reescrita para ser feita em blocos, evitando truncamento
- foi adicionada normalizacao de encoding:
  - remove BOM UTF-8
  - tenta corrigir UTF-16 LE
  - remove `\\0` residuais

Resultado:

- o runtime conseguiu abrir `SMAP.TXT`
- logs confirmaram leitura de mapa no CD

### 3. Preload de TGAs para o cartucho de 4 MB

Problema encontrado:

- inicialmente o sistema nao encontrava ou nao listava os TGAs
- os logs mostravam `no_tga_tokens` e nenhuma tentativa real de carga

Ajustes feitos:

- o preload passou a usar primeiro `RTMAP.TXT` (mapa de renomeacao)
- os TGAs renomeados passaram a ser carregados para a memoria do cartucho
- foram adicionados logs persistentes para:
  - ultimo nome de TGA
  - ultimo caminho tentado
  - ultimo resultado
  - tamanho/assinatura do `SMAP`

Resultado validado em runtime:

- `TGA c:216 a:216 f:0 j:2`

Interpretacao:

- 216 TGAs carregados para o cartucho
- 216 tentativas
- 0 falhas
- caminho ativo via `RTMAP`

### 4. Mapeamento `face -> familyId` no renderer

Problema encontrado:

- o log mostrava `mp:0`
- isso significava que o renderer nao estava usando o mapa por face do `SMAP`

Causas identificadas:

- quando o preload entrava por `RTMAP`, ele retornava antes de preencher o cache do `SMAP`
- o parser do `SMAP` era fragil e dependia de padroes textuais muito especificos

Ajustes feitos:

- o preload por `RTMAP` agora tambem tenta carregar `SMAP.TXT` e preencher cache
- o parser de `faceTextureFamily` foi melhorado
- `ParseSegment1TextureJson(...)` foi relaxado para aceitar sucesso apenas com `faceTextureFamily`
- foi adicionado fallback por `S001FAM.BIN`, que e o formato mais apropriado para runtime

Resultado validado:

- log mudou de `mp:0` para `mp:1`

Isso confirma:

- o renderer agora usa o mapa correto de familia por face

### 5. Carga das texturas para cada LOD

Pipeline atual:

- `TBK8.BIN`, `TBK16.BIN`, `TBK32.BIN`, `TBK64.BIN` sao lidos do CD
- cada familia tem um slot por LOD em `seg1FamilySlots_`
- os slots sao aplicados nas faces pelo mapa do segmento

Problema atual:

- alguns TGAs falham em decode em LODs especificos

Exemplo observado:

- `S1 DEC fail f:2 l:32`

Ajuste feito:

- quando um LOD falha para uma familia, o sistema agora tenta LODs menores da mesma familia:
  - `64 -> 32 -> 16 -> 8`
  - `32 -> 16 -> 8`
  - `16 -> 8`

Objetivo:

- manter a face texturizada mesmo se um TGA de um LOD especifico estiver invalido

## Logs Importantes Ja Confirmados

### Leitura e preload

- `TGA map cd ok: SMAP.TXT`
- `TGA c:216 a:216 f:0 j:2`

### Estado do segmento 1

- `Track segments built 1/1`
- `S1 RDY:1 ...`
- `S1 AP rdr:...`

### Mapeamento por face

Estado anterior:

- `S1 M64:7 mp:0 ...`

Estado atual:

- `S1 M64:7 mp:1 ...`

Interpretacao:

- 7 faces do `SEG_001` receberam slots validos no LOD observado
- o mapeamento por face esta ativo

### Falhas ainda abertas

- `S1 DEC fail f:2 l:32`
- `dec:3`

Interpretacao:

- ainda existem 3 falhas de decode de TGA
- ao menos uma delas e da familia 2 no LOD 32

## Problemas Relevantes Que Tivemos

### 1. Invalid opcode / travamentos SH2

Ocorreram em varias fases anteriores, principalmente quando:

- havia mistura instavel entre carro + pista + iluminacao + troca de textura
- o pipeline ainda tentava usar caminhos antigos
- havia ponteiros/estado inconsistentes no renderer

Medidas que ajudaram:

- reduzir a ativacao para apenas `SEG_001`
- desativar caminhos antigos/fallbacks agressivos
- trabalhar primeiro com a pista isolada
- mover o pipeline para dados pre-processados (`.GEO/.MAT/.BIN`)

### 2. Leitura de arquivo do CD inconsistente

Problemas:

- nome de arquivo grande
- extensao `.json`
- leitura truncada de arquivos grandes

Medidas:

- mover para nomes curtos e `SMAP.TXT`
- leitura em blocos
- logs persistentes de diagnostico

### 3. `segments_map` vazio em alguns passos de build

Problema:

- o build gerava, em certos caminhos, um `segments_map` sem `textureFamilies` e sem `segments`

Causa:

- script errado/fonte errada de geracao
- dependencia de `.map/.meshtex` que ja nao existiam

Direcao correta consolidada:

- reconstruir o mapa a partir de `obj_8`, `obj_16`, `obj_32`, `obj_64`
- gerar artefatos curtos para runtime

### 4. Ambiguidade entre `RTMAP` e `SMAP`

Problema:

- `RTMAP` resolve nomes de textura
- `SMAP` resolve familia por face
- misturar os dois como se fossem a mesma fonte quebrava o cache

Estado correto:

- `RTMAP` serve para preload de TGA
- `SMAP`/`S001FAM.BIN` servem para mapeamento de faces

## Estado Atual do Projeto

No momento:

- `SEG_001` renderiza
- o cartucho de 4 MB esta recebendo os TGAs
- o renderer ja usa mapeamento por face (`mp:1`)
- a aplicacao de slots no renderer esta ativa
- ainda restam falhas de decode em alguns TGAs/LODs

O principal bloqueio restante nao e mais arquitetura. E qualidade/compatibilidade de alguns assets ou do decode desses assets.

## Proximos Passos Recomendados

### Curto prazo

1. Logar todas as familias/LODs que ainda falham em decode, nao apenas a ultima.
2. Validar os TGAs problematicos (principalmente familia 2 / LOD 32).
3. Confirmar visualmente se o fallback entre LODs menores elimina faces escuras.

### Medio prazo

1. Expandir o mesmo pipeline de `SEG_001` para mais segmentos.
2. Substituir parse textual em runtime por bins compactos equivalentes ao `S001FAM.BIN` para todos os segmentos.
3. Centralizar troca de LOD por distancia de forma deterministica.

### Estrutura de runtime desejada

Para o motor final, o caminho mais solido e:

- `GEO.BIN` ou `Sxxx.GEO`
- `MAT8.BIN`, `MAT16.BIN`, `MAT32.BIN`, `MAT64.BIN` ou equivalentes por segmento
- `FAM.BIN` por segmento
- `TBK8.BIN`, `TBK16.BIN`, `TBK32.BIN`, `TBK64.BIN`
- catalogo de nomes compactos somente para debug/ferramentas, nao como dependencia critica de runtime

## Arquivos-Chave

Codigo:

- `src/track_system.cxx`

Documentos/artefatos:

- `cd/data/SMAP.TXT`
- `cd/data/RTMAP.TXT`
- `cd/data/S001FAM.BIN`
- `cd/data/TBK8.BIN`
- `cd/data/TBK16.BIN`
- `cd/data/TBK32.BIN`
- `cd/data/TBK64.BIN`

## Resumo Executivo

O pipeline saiu de um estado instavel e dependente de `.NYA` para um pipeline componentizado funcional para `SEG_001`.

O maior avanço confirmado foi:

- texturas carregadas no cartucho
- renderer usando mapeamento correto por face (`mp:1`)

O problema restante e localizado:

- algumas texturas ainda falham em decode para certos LODs

Isso significa que o proximo trabalho e de refinamento de asset/decode, nao de reestruturacao total do motor.
