# Daytona CE - COURSE1/Segment Rendering Reverse Notes

## Objetivo
Documentar como abrir/editar `COURSE1.MDL` e os arquivos de suporte para extrair a logica de segmentos da pista no Saturn.

## Arquivos chave (Course 1)
- `C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\COURSE1.MDL`
- `C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\COURSE1.TEX`
- `C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\CS1_BLK.BIN`
- `C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\CS1_COL.BIN`

## Ferramenta criada para analise/edicao
Script:
- `C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\daytona_course_tools.py`

Artefatos gerados:
- `C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_COURSE1_MDL.json`
- `C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_COURSE1_TABLE0_VERTICES.csv`
- `C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_CS1_BLK.json`

## Estrutura confirmada - COURSE1.MDL
Header big-endian (bytes 0x00..0x13):
- `count_a = 250`
- `count_b = 171`
- `table0_off = 0x14`
- `section1_off = 0x5F0`
- `tail_off = 0x4A988`

### Table0 (confirmado)
- Comeca em `0x14`
- Tamanho: `count_a * 6`
- Formato por registro: `s16 x, s16 y, s16 z` (big-endian)
- Em COURSE1: 250 registros
- Estatisticas observadas:
  - `x: [-12462 .. -6574]`
  - `y: [0 .. 2432]`
  - `z: [-7340 .. -6144]`

Interpretacao pratica:
- Esta tabela e a base geometrica/espacial do curso (pontos base usados pelo pipeline).
- E o ponto mais seguro para editar com previsibilidade.

### Section1/Tail (parcialmente confirmado)
- `section1_len = 304024`
- `tail_len = 5868`
- `tail` contem pares de 8 bytes (733 pares + 4 bytes residuais), com forte sinal de tabela de descritores/comandos para draw lists.
- Primeiros registros de `section1` parecem comandos + indices (ex.: blocos de 6x u16 com padrao de indices 0,16,32,...).

## Estrutura confirmada - CS1_BLK.BIN (logica de segmentacao)
Header (4 offsets BE u32):
- `section1_off = 44`
- `section2_off = 10668`
- `section3_off = 27052`
- `section4_off = 39428`

Tamanhos:
- `section1_len = 10624`
- `section2_len = 16384`
- `section3_len = 12376`
- `section4_len = 524`

### section2 (forte evidencia de grid de lookup)
- 8192 entradas `u16` (`16384 / 2`)
- 682 entradas nao-zero
- cada valor nao-zero aponta para offsets dentro de `section1`

### section1 (listas indexadas por section2)
- Regiao referenciada por `section2` com sequencias `s16`
- Muitos `-1` como separador/sentinela
- Valores inteiros pequenos e recorrentes (ids)

### section4 -> section3 (tabela de listas de IDs)
- `section4` e uma lista de offsets (`u32`) para `section3`
- `section3` referenciado contem sequencias `s16` com ids e `-1`
- Universo de ids extraido do pool referenciado: `0..255` (256 ids)

Interpretacao pratica:
- `CS1_BLK.BIN` contem a estrutura espacial de segmentacao/culling.
- O runtime provavelmente resolve celula/bloco pela posicao do carro/camera e recupera listas de IDs candidatos para render.

## CS1_COL.BIN
- Nao e um arquivo vazio (apesar de cabecalho inicial zerado).
- Tem muitos dados nao-zero apos o offset inicial.
- Provavel camada de colisao/altura/superficie, complementar ao BLK.

## Como abrir/editar na pratica

## 1) Resumo rapido do curso
```powershell
python C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\daytona_course_tools.py summary --daytona-dir "C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA" --course 1
```

## 2) Exportar COURSE1.MDL para JSON/CSV
```powershell
python C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\daytona_course_tools.py dump-mdl --mdl "C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\COURSE1.MDL" --json-out "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_COURSE1_MDL.json" --csv-out "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_COURSE1_TABLE0_VERTICES.csv"
```

## 3) Editar um vertice da table0 (seguro)
```powershell
python C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\daytona_course_tools.py patch-mdl-vertex --mdl "C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\COURSE1.MDL" --index 0 --x -11200 --y 32 --z -6400 --output "C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\COURSE1_EDIT.MDL"
```

## 4) Exportar estrutura de blocos
```powershell
python C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\daytona_course_tools.py dump-blk --blk "C:\saturn\Saturn_game\DAYTONA_USA_CE\DAYTONA_EXTRACT_TEST\DAYTONA\CS1_BLK.BIN" --json-out "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\reference\DAYTONA_CS1_BLK.json"
```

## Pipeline inferido de render de segmentos (hipotese forte)
1. Carrega geometria/comandos base de `COURSE1.MDL`.
2. Usa `CS1_BLK.BIN` para mapear posicao -> celula/bloco -> lista de IDs ativos/proximos.
3. Com esses IDs, seleciona draw lists/partes do curso para submeter no frame.
4. `COURSE1.TEX` fornece os dados de textura usados pelos comandos do modelo.
5. `CS1_COL.BIN` alimenta a camada de colisao/altura e possivelmente filtros de segmento valido.

## O que ja esta confirmado vs pendente
Confirmado:
- Table0 de `COURSE1.MDL` (250 registros `s16 x,y,z`).
- Estrutura por offsets de `CS1_BLK.BIN`.
- Existencia de universo de IDs `0..255` nas listas de bloco.

Pendente (proxima etapa de engenharia reversa):
- Decodificacao completa de `section1/tail` do `COURSE1.MDL` para mapear exatamente `ID -> draw command range`.
- Vinculo exato entre IDs de `CS1_BLK.BIN` e cada descritor no tail do `COURSE1.MDL`.

## Recomendacao tecnica
Para extrair a logica de segmentos com alta confianca, avance nesta ordem:
1. Resolver o mapeamento `ID (BLK) -> draw descriptor (MDL tail)`.
2. Instrumentar no runtime (log) quais IDs sao ativados por frame com a camera em movimento.
3. Comparar esse log com a janela de segmentos no seu engine (`SEG_xxx`) para reproduzir a politica original da Sega.
