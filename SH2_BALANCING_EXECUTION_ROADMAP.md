# SH2 Balancing Execution Roadmap

## Objetivo

Fechar a refatoração de balanceamento entre Master SH2 e Slave SH2 sem perder boot estável.

O foco não é apenas “mover carga” para a Slave.
O foco real é:

1. reduzir `simMasterWaitTicks`;
2. reduzir waits de pista em lockstep;
3. manter Master restrito a render final, HUD, áudio e IO crítico;
4. preparar o projeto para overlap real de trabalho entre os dois SH2.


## Estado de partida

### Master SH2

- loop principal do frame
- input
- background
- câmera
- HUD
- submissão final VDP1/VDP2
- driver PCM/SGL

### Slave SH2

- simulação gameplay/física em lockstep
- producer/sort da pista em lockstep

### Gargalo dominante

- o Master já despacha trabalho para a Slave;
- porém ainda bloqueia no mesmo frame em dois pontos:
  - simulação do carro/gameplay;
  - producer/sort da pista.

Resultado:

- existe paralelismo de execução;
- ainda não existe ganho pleno de latência.


## Meta técnica final

### Master

Deve ficar responsável por:

- input
- montagem de pacotes
- consumo de snapshots já prontos
- câmera
- HUD
- render final
- áudio real/driver

### Slave

Deve ficar responsável por:

- gameplay/física do carro
- preparação de render do carro
- planning/producer/sort da pista
- preparação de dados consumidos no frame seguinte


## Ordem real de execução recomendada

### Etapa 1 — fechar `SimulationScheduler`

**Prioridade:** máxima  
**Risco:** médio/alto  
**Arquivos sensíveis:** `src/game_loop_system.hpp`

Falta:

- fechar a extração lógica de:
  - dispatch
  - drain
  - backoff
  - política lockstep vs async

Sem fazer:

- nova integração de helpers no caminho crítico;
- novos wrappers inline dentro de `GameLoopSystem`.

Estratégia:

- manter contratos e documentação fora do runtime;
- só mover lógica se houver redução líquida de código no arquivo crítico;
- cada microetapa deve ser revertível isoladamente.

Critério de aceite:

- build estável;
- ISO em `4134912` bytes;
- sem regressão de telemetria;
- boot estável no emulador.


### Etapa 2 — isolar `CarRenderSystem`

**Prioridade:** alta  
**Risco:** médio  
**Arquivos alvo:** `src/car_system.*`, `src/mesh_renderer.*`, `src/render_pipeline.*`

Falta:

- separar a parte visual do carro da parte de gameplay/comando;
- preparar um pacote de render do carro consumido pelo Master.

O que deve sair de `CarSystem`:

- preparação de posição de render;
- yaw visual/final;
- submit visual;
- estado transitório puramente visual.

O que deve permanecer no `CarSystem`:

- input e comando do carro;
- fachada de gameplay;
- snapshots de debug do carro.

Critério de aceite:

- carro visualmente idêntico;
- `GameLoopSystem` não cresce;
- `CarSystem` perde responsabilidade visual.


### Etapa 3 — isolar `TrackRenderScheduler`

**Prioridade:** alta  
**Risco:** médio/alto  
**Arquivos alvo:** `src/track_system.*`, `src/track_draw_producer.hpp`

Falta:

- transformar a saída da pista em pacote explícito;
- separar planning/producer/sort do consumo final pelo Master.

Objetivo real:

- permitir que o Master consuma um packet de pista válido do frame anterior;
- reduzir o barrier lockstep da pista.

Critério de aceite:

- fallback síncrono continua existindo;
- pista não perde estabilidade;
- safe mode continua funcional;
- draw list do frame anterior pode ser reutilizada conscientemente.


### Etapa 4 — criar `CdAssetSystem`

**Prioridade:** média  
**Risco:** baixo/médio  
**Arquivos alvo:** `src/main.cxx` e loaders auxiliares

Falta:

- scheduler central de leitura de CD;
- fila explícita de jobs;
- retries centralizados;
- staging previsível.

Benefício:

- tira acoplamento de bootstrap;
- prepara melhor distribuição de carga e previsibilidade temporal.

Critério de aceite:

- `main.cxx` deixa de orquestrar carregamento direto;
- jobs de CD ficam observáveis;
- boot continua estável.


### Etapa 5 — criar `MemoryBudgetSystem`

**Prioridade:** média  
**Risco:** médio  
**Arquivos alvo:** bootstrap, track, áudio e loaders

Falta:

- política central de CART/HWR/LWR;
- budget por categoria de asset.

Categorias mínimas:

- pista
- carro
- áudio
- HUD
- staging de CD

Critério de aceite:

- decisões de memória deixam de ficar espalhadas;
- footprint por categoria fica previsível;
- futuras mudanças param de quebrar por alocação implícita.


### Etapa 6 — remover lockstep de forma gradual

**Prioridade:** máxima  
**Risco:** alto  
**Pré-requisitos:** Etapas 1, 2 e 3

Meta:

- Master parar de esperar sempre pela Slave no mesmo frame.

Estratégia:

1. simulação usa snapshot `N-1`;
2. pista usa draw packet `N-1`;
3. Master só bloqueia se não houver packet consistente disponível.

Métricas de sucesso:

- queda visível em `simMasterWaitTicks`;
- queda de waits de pista;
- pacing melhor;
- sem drift perceptível entre carro, câmera e render.


## O que falta especificamente no código

### Ainda falta componente dedicado

- `SimulationScheduler`
- `CarRenderSystem`
- `TrackRenderScheduler`
- `CdAssetSystem`
- `MemoryBudgetSystem`

### Ainda falta desacoplamento estrutural

- bootstrap/carregamento em `src/main.cxx`
- política de memória central
- visual do carro fora da fachada `CarSystem`
- render da pista em packet explícito
- consumo assíncrono de snapshots `N-1`


## Regras de segurança por arquivo

### `src/game_loop_system.hpp`

- risco máximo;
- evitar wrappers novos inline;
- evitar extrações que aumentem o binário;
- só mexer se houver ganho claro e microetapa isolada.

### `src/car_system.cxx`

- risco alto;
- pequenas integrações “passivas” já quebraram boot;
- usar apenas documentação e contratos passivos até que a extração visual do carro aconteça fora do caminho crítico.

### `src/main.cxx`

- risco alto para boot;
- refatorar só após existir `CdAssetSystem` e contratos bem estáveis.

### `src/track_system.*`

- risco médio/alto;
- pode evoluir melhor que `GameLoopSystem`, desde que preserve safe mode e fallback síncrono.


## Próximo passo operacional recomendado

Próxima execução recomendada:

1. não tocar em `GameLoopSystem`;
2. não tocar em `CarSystem` runtime;
3. documentar o recorte exato de `CarRenderSystem`;
4. preparar contratos passivos de render do carro;
5. só depois mover preparo visual do carro para fora da fachada atual.

Essa é hoje a trilha com melhor relação entre:

- ganho arquitetural;
- risco de boot;
- chance real de avançar no balanceamento entre as SH2.
