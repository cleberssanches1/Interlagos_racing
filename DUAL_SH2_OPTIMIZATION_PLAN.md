# DUAL SH2 Optimization Plan (Revisado)

## Objetivo

Garantir 30 FPS com 20 segmentos ativos, eliminando conflito de jobs na Slave SH2 e evitando queda progressiva de memoria/performance ao longo das voltas.

## Diagnostico consolidado

- A implementacao ja tinha `SimulationTask` assinc na Slave, mas estava desativada por conflito com `SlaveTrackDrawProducer`.
- O producer da pista ja usa `SRL::Slave::ExecuteOnSlave()` e pode manter job em voo por mais de 1 frame.
- Sem arbitragem minima, habilitar simulacao na Slave causa disputa de submissao.
- Mover o build completo de prefetch para Slave agora (estado atual) e alto risco, por depender de estado compartilhado e buffers LWR com ownership sensivel.

## Estrategia revisada (ordem obrigatoria)

1. **Passo A (implementado)**: reativar simulacao na Slave com barreira segura antes do render da pista.
2. **Passo B (implementado)**: arbitragem minima de jobs (scheduler lite) no `GameLoopSystem`:
   - evitar submit de simulacao quando producer da pista ainda esta em voo;
   - aplicar backoff curto apos timeout de drenagem.
3. **Passo C (proximo)**: diagnosticar custo real de pos-slide no Master (`BuildSegmentHandleTable`, merges, lookup rebuild).
4. **Passo D (depois de estabilidade)**: avaliar migracao parcial de prefetch para Slave por subtarefas pequenas e read-only antes de mover build completo.
5. **Passo E (arquitetural)**: scheduler central completo para todos os tipos de job da Slave.

---

## Melhorias implementadas no codigo

### 1) Reativacao da simulacao na Slave

- Arquivo: `src/main.cxx`
- Alteracao:
  - `enableSlaveForSimulation = true`
  - `enableSlaveForCarPrepare = false` (mantido)

### 2) Drenagem segura da SimulationTask antes da janela da pista

- Arquivo: `src/game_loop_system.hpp`
- Novo fluxo:
  - `DrainSimulationJobIfInFlight(true)` executa antes do track render.
  - Nunca marca job como concluido sem `IsDone()` real.
  - Possui soft timeout (contabiliza e ativa backoff) e hard wait para garantir que a Slave esteja livre.

### 3) Scheduler lite de arbitragem (sem refactor pesado)

- Arquivo: `src/game_loop_system.hpp`
- Regras novas:
  - Simulacao na Slave so e submetida se:
    - nao houver job de simulacao/car prepare em voo;
    - `TrackSystem::Telemetry().producer.jobInFlight == false`.
  - Se producer estiver em voo, simulacao cai para caminho sincrono no Master naquele frame.
  - Backoff curto evita thrash de submissao apos timeout de drenagem.

### 4) Caminho sincrono consolidado

- Arquivo: `src/game_loop_system.hpp`
- Foi extraido `RunGameplayFrameSynchronously()` para fallback deterministico quando Slave estiver ocupada.

---

## Criterios de aceite (atualizados)

### Performance

- FPS sem slide: **>= 30** (meta), **>= 27** (aceitavel temporario).
- FPS no slide: **>= 25**.

### Estabilidade SH2

- Sem travas por conflito de job da Slave.
- `producer.jobInFlight` pode existir, mas sem cascata de timeout/fallback infinito.

### Memoria

- `SWLWR free` deve oscilar em faixa e **nao cair monotonicamente** em serra descendente por volta.
- Apos aquecimento da janela (primeiros slides), memoria deve entrar em **plateau operacional**.

---

## Proximos ajustes recomendados (sem quebrar o que foi feito)

1. Instrumentar tempos por fase do slide (build/merge/lookup) para atacar o maior custo no Master.
2. Aplicar limite de trabalho por frame no pos-slide (lazy rebuild por dirty flags) para evitar picos.
3. Somente apos estabilizar passos 1 e 2, testar migracao parcial de prefetch para Slave com payload pequeno.

---

## Arquivos impactados nesta revisao

- `src/main.cxx`
- `src/game_loop_system.hpp`
