# CarRenderSystem Plan

## Objetivo

Separar a preparação visual do carro da fachada `CarSystem` e do `GameLoopSystem`, sem mover ainda a submissão final para fora do Master SH2.

O alvo não é mudar o comportamento agora.
O alvo é preparar um recorte claro para futura extração com baixo risco.


## Estado atual

Hoje o render do carro está dividido entre:

- `GameLoopSystem`
  - `BuildCarRenderFrameState`
  - `ResolveCarRenderPosition`
  - `ApplyCarCameraDepthBias`
  - `ApplyCarVisualLift`
  - `RenderCarShadowIfEnabled`
  - `SubmitCarRender`
  - `RenderCar`

- `CarSystem`
  - `SyncRenderState`
  - `SubmitRender`
  - `RenderYawDegrees`
  - `Renderer`
  - `VisualYawOffsetDegrees`
  - `wheelRig_` e `wheelInput_`

- `RenderPipeline`
  - fila/submissão visual final no Master


## Recorte alvo

### O que deve virar `CarRenderSystem`

- montagem de estado visual do frame do carro;
- cálculo de posição de render;
- cálculo de yaw visual/final;
- preparação de shadow blob/model;
- sincronização do estado visual antes do submit;
- emissão da submissão visual para `RenderPipeline`.

### O que deve continuar fora

**No `CarSystem`:**

- input e comando;
- gameplay assembly;
- aplicação do estado autoritativo de simulação;
- snapshots de debug do carro.

**No `GameLoopSystem`:**

- orquestração do frame;
- decisão de quando renderizar o carro;
- composição com câmera/HUD/pista.

**No Master SH2:**

- `RenderPipeline`
- VDP1/VDP2
- shadow draw efetivo


## Fronteiras internas do futuro `CarRenderSystem`

### 1. `CarRenderStateAssembler`

Responsável por:

- receber câmera + estado autoritativo do carro;
- produzir `CarRenderPacket`;
- resolver posição de render;
- resolver yaw visual;
- carregar snapshots de debug necessários.

Status atual:

- contratos passivos criados em `src/car_render_contracts.hpp`;
- assembler passivo criado em `src/car_render_state_assembler.hpp`;
- ainda sem integração ao runtime.

### 2. `CarShadowAssembler`

Responsável por:

- decidir shadow blob/model;
- preparar posição e yaw de sombra;
- separar dados de sombra da emissão.

Status atual:

- assembler passivo criado em `src/car_shadow_assembler.hpp`;
- ainda sem integração ao runtime.

### 3. `CarRenderSubmitter`

Responsável por:

- aplicar `SyncRenderState`;
- atualizar `RenderPipeline`;
- coletar telemetria de faces renderizadas.

Status atual:

- contrato passivo expandido em `src/car_render_contracts.hpp`;
- submitter passivo criado em `src/car_render_submitter.hpp`;
- ainda sem integração ao runtime.


## Dependências de entrada

- posição autoritativa do carro
- yaw de gameplay
- offset visual do carro
- câmera resolvida do frame
- `RuntimeDebugSnapshot`
- `RenderPipeline`
- renderer de sombra opcional


## Dados de saída

- `CarRenderPacket`
- `CarShadowPacket`
- `CarSubmitPacket`
- `CarRenderTelemetry`


## Ordem futura de execução

1. `GameLoopSystem` decide se o carro será renderizado
2. `CarRenderStateAssembler` monta o packet
3. `CarShadowAssembler` prepara dados de sombra
4. `CarRenderSubmitter` faz a submissão final no Master


## Critério de aceite da futura extração

- nenhuma regressão visual do carro;
- mesma contagem de faces renderizadas;
- `GameLoopSystem` perde código visual do carro;
- `CarSystem` perde responsabilidade de submit visual;
- build continua no envelope estável.


## Restrições

- não mover chamadas de `RenderPipeline` para a Slave;
- não alterar agora `GameLoopSystem`;
- não integrar contratos novos ao runtime nesta etapa;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- manter `GameLoopSystem` e `CarSystem` intactos;
- o recorte passivo do `CarRenderSystem` está completo;
- só considerar integração real quando houver estratégia de redução líquida no código crítico.
