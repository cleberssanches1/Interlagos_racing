# AutoLapRouteSystem Plan

## Objetivo

Separar a construção, inicialização e avanço da rota de auto-lap do `GameLoopSystem`, sem alterar o comportamento atual.

O objetivo desta fase não é migrar execução.
O objetivo é preparar um recorte claro e verificável para futura extração com risco baixo.


## Estado atual

Hoje a lógica de auto-lap está concentrada em `src/game_loop_system.hpp`, principalmente em:

- construção e rebuild
  - `BuildAutoLapRoute`
  - `BuildAutoLapRouteFromPathGuide`
  - `BuildFallbackAutoLapRoute`
  - `RebuildAutoLapRouteYawData`
  - `NormalizeAutoLapRouteDirection`

- carga e parse da guia
  - `LoadAutoLapGuideLines`
  - `TryLoadAutoLapGuideBytes`
  - `TryParseAutoLapGuideBytes`
  - `CopyParsedAutoLapGuideLines`
  - `ResolveSelectedAutoLapRouteLine`

- inicialização e avanço
  - `EnsureAutoLapRouteReady`
  - `InitializeAutoLapRouteIfNeeded`
  - `UpdateAutoLapRoute`
  - `AdvanceAutoLapPlanarPosition`
  - `AdvanceAutoLapVerticalPosition`
  - `AdvanceAutoLapWaypointWindow`
  - `UpdateAutoLapHeading`

- reset/retenção
  - `HasRetainedAutoLapRouteStorage`
  - `ResetAutoLapRouteFlags`
  - `ResetAutoLapRouteScalars`
  - `ResetAutoLapRouteState`
  - `ClearAutoLapRouteBuffers`
  - `ReleaseAutoLapGuideLines`
  - `ReleaseAutoLapRouteStorage`


## Recorte alvo

### O que deve virar `AutoLapRouteSystem`

- carga do guia `PATH.NYA`;
- parse e cópia das guide lines;
- seleção da linha ativa;
- construção da rota final com mapeamento para segmentos;
- rebuild de yaw/base yaw/off yaw;
- inicialização da posição/yaw do carro na rota;
- avanço da posição do carro pela rota;
- manutenção do estado lógico da rota.

### O que deve continuar fora

**No `GameLoopSystem`:**

- decisão de habilitar/desabilitar auto-lap;
- orquestração do frame;
- aplicação final do resultado no estado do carro/câmera;
- logs e HUD do frame.

**No `TrackSystem`:**

- dados da pista;
- `SegmentCount`;
- `FindSegmentCenterById`;
- consultas de solo/segmento.

**No Master SH2:**

- integração final com câmera/render;
- lifecycle do frame.


## Fronteiras internas do futuro `AutoLapRouteSystem`

### 1. `AutoLapGuideLoader`

Responsável por:

- tentar candidatos de caminho do `PATH.NYA`;
- carregar bytes do CD;
- parsear o payload;
- materializar as guide lines em buffers de runtime.

Status atual:

- contratos passivos criados em `src/auto_lap_route_contracts.hpp`;
- operações passivas criadas em `src/auto_lap_route_transition_ops.hpp`;
- ainda sem integração ao runtime.

### 2. `AutoLapRouteBuilder`

Responsável por:

- selecionar a guide line ativa;
- simplificar a linha quando necessário;
- mapear pontos para segmentos da pista;
- normalizar direção;
- rebuildar yaw/off yaw/base yaw;
- validar consistência dos buffers.

Status atual:

- recorte documental preparado;
- contratos passivos preparados;
- ainda sem extração do código executável.

### 3. `AutoLapRouteStepper`

Responsável por:

- inicializar a posição na rota;
- avançar posição planar/vertical;
- avançar janela de waypoints;
- atualizar yaw do carro;
- refletir o segmento observado.

Status atual:

- somente documentação e contratos passivos;
- runtime preservado em `GameLoopSystem`.

### 4. `AutoLapRouteLifecycle`

Responsável por:

- resetar flags/escalares;
- liberar buffers/guide lines;
- reportar retenção de storage;
- preparar futura política de retenção e rebuild.


## Dependências de entrada

- `TrackSystem`
- `trackSegOffset`
- posição de referência do carro
- posição atual do carro
- yaw atual do carro
- `autoLapStepUnits`
- guide lines parseadas de `PATH.NYA`


## Dados de saída

- `AutoLapFrameContext`
- `AutoLapRouteStorageSnapshot`
- `AutoLapGuideLoadPacket`
- `AutoLapRouteBuildPacket`
- `AutoLapRouteStepPacket`


## Ordem futura de execução

1. `GameLoopSystem` decide se auto-lap está habilitado
2. `AutoLapGuideLoader` tenta carregar/parsear `PATH.NYA`
3. `AutoLapRouteBuilder` constrói ou reconstrói a rota
4. `AutoLapRouteStepper` inicializa/avança a rota
5. `GameLoopSystem` consome a saída e aplica ao frame


## Critério de aceite da futura extração

- nenhuma regressão no movimento do auto-lap;
- inicialização da rota preservada;
- fallback por centros de segmento preservado;
- rebuild de yaw/off yaw preservado;
- retenção/liberação de buffers preservada;
- build continua no envelope estável.


## Restrições

- não integrar o novo recorte ao runtime nesta etapa;
- não mover agora a lógica de movimento do carro para outro executor;
- não alterar as consultas ao `TrackSystem`;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- manter `GameLoopSystem` intacto no caminho crítico;
- usar os contratos passivos para enxergar as fronteiras de estado;
- só considerar integração real do `AutoLapRouteSystem` depois de reduzir o corpo do runtime por substituições textuais mínimas.
