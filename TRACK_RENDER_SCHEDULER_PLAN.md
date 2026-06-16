# TrackRenderScheduler Plan

## Objetivo

Separar o pipeline de planejamento/producer/sort da pista da etapa de consumo final no Master SH2.

O objetivo desta fase não é alterar o comportamento atual.
O objetivo é preparar um recorte claro para futura extração com risco baixo.


## Estado atual

Hoje a pista está dividida entre:

- `GameLoopSystem`
  - decide se a pista será renderizada;
  - chama `SetObservedCarSegmentId`;
  - abre/fecha janela de frame (`BeginFrame` / `EndFrame`);
  - chama `TrackSystem::RenderFrame(...)`.

- `TrackSystem`
  - mantém planejamento;
  - mantém producer/sort;
  - coordena render;
  - mantém telemetria Master/Slave;
  - mantém fallback síncrono e safe mode.

- `TrackDrawProducer`
  - encapsula producer na Slave;
  - controla stall/safe mode/recovery;
  - expõe estatísticas de producer.


## Recorte alvo

### O que deve virar `TrackRenderScheduler`

- montagem do contexto de render da pista por frame;
- decisão explícita de consumo de packet válido;
- coordenação entre:
  - observed segment;
  - begin/end frame;
  - render frame;
  - telemetria de producer/sort;
- preparação para uso de packet `N-1`.

### O que deve continuar fora

**No `TrackSystem`:**

- dados da pista;
- consultas de superfície/parede;
- working set e recursos;
- implementação interna do producer/sort;
- safe mode/fallback.

**No `GameLoopSystem`:**

- decisão de quando chamar o scheduler;
- composição com carro, HUD, câmera e render final.

**No Master SH2:**

- consumo final da draw list;
- submissão visual final.


## Fronteiras internas do futuro `TrackRenderScheduler`

### 1. `TrackFrameContextAssembler`

Responsável por:

- montar o contexto do frame da pista;
- transportar câmera, luz, offset e posição do carro;
- explicitar o `observedCarSegmentId`.

### 2. `TrackRenderPacketAssembler`

Responsável por:

- materializar um `TrackRenderPacket`;
- carregar sinais de validade;
- preparar terreno para consumo `N-1`.

### 3. `TrackRenderTelemetryAssembler`

Responsável por:

- coletar métricas Master/Slave relevantes;
- isolar `producer stats`, `sort ticks`, `plan ticks`, `frame ticks`.


## Dependências de entrada

- `trackOffset`
- `lightDirection`
- `cameraLocation`
- `cameraLookTarget`
- `carWorldPosition`
- `observedCarSegmentId`
- telemetria atual do `TrackSystem`


## Dados de saída

- `TrackFrameContext`
- `TrackRenderPacket`
- `TrackRenderTelemetry`


## Ordem futura de execução

1. `GameLoopSystem` decide se a pista será renderizada
2. `TrackFrameContextAssembler` monta o contexto
3. `TrackRenderPacketAssembler` monta o packet
4. `TrackRenderScheduler` coordena `BeginFrame` / `RenderFrame` / `EndFrame`
5. `TrackRenderTelemetryAssembler` consolida a telemetria do frame


## Critério de aceite da futura extração

- nenhuma regressão visual da pista;
- fallback síncrono preservado;
- safe mode preservado;
- telemetria Master/Slave preservada;
- build continua no envelope estável.


## Restrições

- não alterar agora `GameLoopSystem`;
- não alterar agora `TrackSystem` runtime;
- não mover submissão final para a Slave;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- criar contratos passivos do scheduler de pista;
- criar assembler passivo de contexto e telemetria;
- só considerar integração real quando o recorte estiver completo fora do caminho crítico.
