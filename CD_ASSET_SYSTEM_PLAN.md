# CdAssetSystem Plan

## Objetivo

Separar a resolução de caminhos, leitura binária em chunks e parsing leve de assets de CD do restante do runtime.

O objetivo desta fase não é alterar o comportamento atual.
O objetivo é preparar um recorte claro para futura extração com risco baixo.


## Estado atual

Hoje o domínio de assets de CD está espalhado entre:

- `src/cd_asset_system.hpp`
  - `FindExistingPath`
  - `ReadBinaryFileSimple`
  - `LoadCarAnchorPoints`
  - `FindSbaShadowModelPath`

- `src/main.cxx`
  - duplicação de `FindExistingPath`
  - duplicação de `ReadCdBinaryFileSimple`
  - duplicação de parsing de `CAR1_ANCHORS.JSON`
  - seleção de path para carga do carro e shadow

- `src/game_loop_system.hpp`
  - `ReadCdBinaryFile`
  - `AutoLapGuidePathCandidates`
  - `TryLoadAutoLapGuideBytes`

- `src/track_system.cxx`
  - resolução de caminhos por variantes
  - leituras diretas de binários/textos de mapa
  - carga de texturas e catálogos por CD


## Problema atual

- há duplicação de política de leitura em mais de um ponto;
- a montagem de listas de candidatos está misturada com lógica de gameplay/render;
- parsing leve de assets fica acoplado ao bootstrap do jogo;
- a futura distribuição de carga entre SH2 fica sem fronteiras explícitas.


## Recorte alvo

### O que deve virar `CdAssetSystem`

- resolução explícita de candidatos por asset lógico;
- leitura binária/texto em chunks;
- parsing leve de assets simples;
- telemetria de resolução/leitura/parsing;
- contratos para bootstrap e consumo por outros domínios.

### O que deve continuar fora

**No `main.cxx`:**

- decisão de bootstrap;
- composição do pipeline de carro;
- decisões de fallback alto nível.

**No `GameLoopSystem`:**

- decisão de quando carregar assets auxiliares de runtime;
- consumo do resultado já lido/parseado.

**No `TrackSystem`:**

- semântica de streaming da pista;
- working set e cache;
- política de prefetch e descarte.


## Fronteiras internas do futuro `CdAssetSystem`

### 1. `CdAssetRequestAssembler`

Responsável por:

- materializar requests por asset lógico;
- carregar lista de caminhos candidatos;
- explicitar se o asset é binário, texto ou parse leve.

### 2. `CdAssetReadAssembler`

Responsável por:

- preparar o packet de leitura;
- explicitar path resolvido;
- explicitar política de chunk;
- preparar terreno para leitura síncrona controlada.

### 3. `CdAssetParseAssembler`

Responsável por:

- isolar parsing leve de `CAR1_ANCHORS.JSON`;
- separar payload lido de resultado parseado;
- preparar contratos para outros assets simples.

### 4. `CdAssetTelemetryAssembler`

Responsável por:

- consolidar resolução de path;
- consolidar bytes lidos;
- transportar sucesso/falha de parse.


## Dependências de entrada

- caminhos candidatos de asset
- nome lógico do asset
- política de chunk de leitura
- resultado bruto de leitura


## Dados de saída

- `CdAssetCandidateSet`
- `CdAssetRequestPacket`
- `CdAssetReadPacket`
- `CdAssetParsePacket`
- `CdAssetTelemetry`


## Ordem futura de execução

1. domínio chamador monta o asset lógico desejado
2. `CdAssetRequestAssembler` monta o request
3. `CdAssetReadAssembler` materializa path e política de leitura
4. `CdAssetSystem` executa a leitura
5. `CdAssetParseAssembler` aplica parse leve quando necessário
6. `CdAssetTelemetryAssembler` consolida o resultado


## Critério de aceite da futura extração

- nenhuma regressão de bootstrap;
- nenhuma regressão na carga de anchors do carro;
- nenhuma regressão na resolução de `SBA.NYA`;
- caminho de AutoLap continua compatível;
- build continua no envelope estável.


## Restrições

- não alterar agora `main.cxx` runtime;
- não alterar agora `game_loop_system.hpp` runtime;
- não integrar contratos novos ao runtime nesta etapa;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- criar contratos passivos de request/read/parse/telemetria;
- manter todas as leituras reais como estão;
- só considerar integração real quando houver redução líquida no caminho crítico.
