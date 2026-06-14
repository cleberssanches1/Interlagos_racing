# Dual SH2 Componentization Audit

## Estado atual

- `src/main.cxx`
  - Faz a composição do projeto.
  - Instancia `TrackSystem`, `CarSystem`, `HudSystem`, `BackgroundManager`, `CarAudioSystem`.
  - Monta `GameLoopSystem::Context`.

- `src/game_loop_system.hpp`
  - É o orquestrador principal do frame.
  - Ordem atual: input → gameplay tick → física do carro → câmera → background → HUD → render → áudio.
  - Já possui pontos explícitos para uso do Slave SH2:
    - `EnableSlaveForSimulation`
    - `EnableSlaveForCarPrepare`
    - integração com `SRL::Slave::ExecuteOnSlave(...)`
  - Mantém áudio no Master SH2 por segurança do driver PCM/SGL.

- `src/car_system.cxx`
  - É a fronteira do objeto carro.
  - Agrupa:
    - entrada e comandos
    - estado de marcha/aceleração enviado ao gameplay
    - sincronização do estado de render do carro
    - wheel rig
    - telemetria de drivetrain para HUD/áudio

- `src/simple_car_physics.hpp`
  - Encapsula o passo de física do carro por meio da interface `ICarPhysics`.
  - Hoje isso já é uma boa unidade para mover carga ao Slave SH2.

- `src/simple_gameplay_tick.hpp`
  - Encapsula a lógica de gameplay por meio da interface `IGameplayTick`.
  - É outra unidade naturalmente separável.

- `src/track_system.hpp`
  - Já é o sistema mais preparado para dual SH2.
  - Possui:
    - planejamento
    - producer
    - depth sorting
    - coordenação de render
    - telemetria Master/Slave

- `src/car_audio_system.hpp`
  - Hoje é um subsistema separado por interface (`IAudioEvents`).
  - Processa apenas no Master SH2.
  - Agora usa roteamento externo de canais por `IAudioVoiceRouter`.

- `src/project_voice_router.hpp`
  - Centraliza a tabela de canais de áudio do projeto.
  - Remove IDs fixos espalhados do código de gameplay.

- `BackgroundManager`
  - É separado como subsistema visual.
  - Continua no Master SH2.

- `HudSystem`
  - É separado como subsistema de overlay/HUD.
  - Continua no Master SH2.


## Separação atual por responsabilidade

- **CD / carregamento**
  - Ainda está muito concentrado em `src/main.cxx` e helpers locais.
  - Existe separação funcional, mas ainda não existe um `CdStreamingSystem` dedicado.

- **Memória expandida / CART / HWR / LWR**
  - O uso existe e está espalhado entre bootstrap, loaders e áudio.
  - Ainda não existe um `MemoryOrchestrator` central.

- **Carro / gameplay / física**
  - Está razoavelmente separado:
    - `CarSystem` = fachada do carro
    - `IGameplayTick` = regras de gameplay
    - `ICarPhysics` = física
    - `CarAudioSystem` = áudio do carro

- **Pista / renderização da pista**
  - Já está mais modular.
  - `TrackSystem` e `TrackRenderCoordinator` são candidatos fortes para continuar no Slave SH2.

- **Render do carro**
  - Ainda está parcialmente dividido entre `CarSystem`, `MeshRenderer` e `RenderPipeline`.
  - Dá para evoluir para um `CarRenderSystem` dedicado.

- **Som**
  - Agora está dividido em:
    - contrato: `IAudioEvents`
    - roteamento de vozes: `IAudioVoiceRouter`
    - implementação do carro: `CarAudioSystem`
    - política do projeto: `ProjectVoiceRouter`


## O que já está apto para Slave SH2

- `TrackSystem` producer / planning / sort
- `ICarPhysics::Step(...)`
- `IGameplayTick::Tick(...)`
- preparação assíncrona de estado do carro para render


## O que deve permanecer no Master SH2

- chamadas ao driver de áudio PCM/SGL
- submissão final de render
- HUD e debug overlay
- integração principal do frame
- chamadas VDP1/VDP2


## O que ainda está acoplado demais

- bootstrap/carregamento em `src/main.cxx`
- política de memória espalhada por subsistemas
- render do carro ainda não isolado como componente próprio
- carregamento de CD sem um scheduler dedicado


## Próxima decomposição recomendada

1. `CdAssetSystem`
   - responsabilidade: leitura de CD, retries, staging e fila de jobs

2. `MemoryBudgetSystem`
   - responsabilidade: decidir CART/HWR/LWR por categoria de asset

3. `CarRenderSystem`
   - responsabilidade: preparar e submeter apenas a parte visual do carro

4. `AudioMixerPolicy`
   - responsabilidade: mapear subsistemas para grupos/canais

5. `SimulationScheduler`
   - responsabilidade: decidir o que vai para Master ou Slave a cada frame


## Resultado prático

- O projeto já tem uma base boa de componentização em pista, gameplay, física e áudio.
- O maior gargalo estrutural restante está no bootstrap e na ausência de um gerenciador central de streaming/memória.
- Para dual SH2, a direção correta é:
  - deixar Master = render final, HUD, áudio, IO crítico
  - deixar Slave = pista, planejamento, física/gameplay do carro, preparação de dados
