# SH2 Balancing Plan

## Objetivo

Usar os dois SH2 de forma controlada no runtime da pista:

- `master` segura o frame loop, streaming, manutencao e composicao final
- `slave` absorve trabalho paralelo de preparacao de lista de desenho e, depois, outras tarefas de preparacao de baixo risco

O codigo atual ja tem a abstracao para isso:

- `SlaveTrackDrawProducer` suporta trabalho assincrono com fallback seguro
- `TrackRenderCoordinator` mede budget e aplica a fila preparada
- `TrackDrawProducerStats` expoe os sinais de saude da pipeline

## Status atual (implementado em 2026-04-23)

Foi aplicada a estrategia de paralelismo sem bloqueio no frame critico:

1. `kEnableTrackSlaveBarrierLockstep = false` no `TrackSystem`:
   - a Master nao bloqueia para esperar job da Slave no mesmo frame.
2. `SlaveTrackDrawProducer` tunado para corrida longa:
   - `maxFramesInFlight = 3`
   - `safeModeStallThreshold = 6`
   - `safeModeCooldownFrames = 45`
3. `SlaveTrackDepthSorter` tunado para corrida longa:
   - `maxFramesInFlight = 2`
   - `safeModeStallThreshold = 5`
   - `safeModeCooldownFrames = 45`
4. `BuildAndApplyFramePlanStage(...)` manteve aplicacao de plano por frame.
5. Foi extraido `ApplyFramePlanLodTargets(...)` para aplicar os targets de LOD de forma centralizada.
6. Foi adicionado `PromoteLastValidFramePlanForCurrentFrame(...)`:
   - quando o planner e pulado por decimator, o sistema reaproveita o ultimo plano valido no frame atual.
   - evita perder targets de LOD e evita fallback agressivo desnecessario.
7. No caminho `runFramePlan == false`, o frame:
   - reaplica plano valido (quando existe),
   - so cai para `UpdateDesiredStabilizedWindowLodTargets()` quando realmente nao ha plano valido.
8. Resultado esperado desta fase:
   - menos stalls da Master por espera da Slave,
   - menor jitter em longa duracao,
   - menor custo de planejamento em frames decimados sem quebrar estabilidade da janela.
9. Validacao host executada:
   - `track_streaming_policy_tests`: 10/10 PASS
   - `run_lwr_tests.exe`: 8/8 PASS
10. Build Saturn completo nao validado neste ambiente local por ausencia do toolchain `sh2eb-elf-gcc`.

Contrato de dados desta etapa continua em:

- [SH2_FRAME_PLAN_CONTRACT.md](./SH2_FRAME_PLAN_CONTRACT.md)

## Estado atual observado

No estado atual do codigo:

- estabilizacao usa Slave para producer e depth-sort quando `config.useSlave = true`
- lockstep esta desativado por padrao (`lk:0` no overlay)
- fallback sincronico continua habilitado (safe mode/timeout)
- planejamento de frame agora pode ser reutilizado de forma controlada nos frames decimados

Portanto, a meta nao e mais "reativar o dual SH2", e sim estabilizar throughput/latencia em corrida longa com metricas objetivas.

## Principios de execucao

1. O master nunca pode ficar bloqueado esperando o slave alem do limite acordado
2. O slave pode atrasar, mas nao pode corromper a lista visivel anterior
3. O fallback sincronico precisa continuar funcional em qualquer fase
4. Toda ativacao do slave precisa ter criterio de rollback automatico
5. A comparacao deve ser sempre feita contra baseline do mesmo trecho de pista e da mesma carga

## Fases do plano

### Fase 0. Baseline com slave desligado

Objetivo:

- medir o custo atual do caminho sincronico
- fixar uma referencia para comparar qualquer ganho futuro

Passos executaveis:

1. Rodar a pista com a politica atual, mantendo o slave desligado
2. Registrar `TrackDrawProducerStats` por frame
3. Registrar `sh2MasterStreamTicksThisFrame_`, `sh2MasterDrawTicksThisFrame_` e `sh2MasterFrameTicksThisFrame_`
4. Registrar a taxa de slides, stalls e reuse da lista anterior
5. Guardar uma janela longa de comparacao, nao apenas alguns frames

Metricas:

- `jobsSubmitted`
- `jobsCompleted`
- `synchronousBuilds`
- `reusedPreviousList`
- `timeoutFallbacks`
- `lastLatencyFrames`
- `maxLatencyFrames`

Criterio de saida:

- baseline estavel documentado
- nenhum dado de referencia faltando para o mesmo percurso

### Fase 1. Habilitar slave apenas para a lista de desenho

Objetivo:

- mover a construcao da draw list para o slave sem mexer no resto do frame

Passos executaveis:

1. Permitir `useSlave = true` fora da politica de estabilizacao mais restrita
2. Manter `maxFramesInFlight = 3` para producer e `2` para depth-sort
3. Manter `recoveryFrames = 120`
4. Manter `safeModeStallThreshold = 6` (producer) e `5` (depth-sort)
5. Manter `safeModeCooldownFrames = 45`
6. Validar que o master continua desenhando com a ultima lista completa quando o slave ainda esta em voo

Metricas:

- `jobInFlight`
- `lastLatencyFrames`
- `maxLatencyFrames`
- `timeoutFallbacks`
- `consecutiveTimeouts`
- `safeModeTriggers`
- `safeModeFrames`

Criterio de saida:

- `timeoutFallbacks == 0` em corrida longa
- `consecutiveTimeouts == 0`
- `lastLatencyFrames` permanece dentro do limite acordado

### Fase 2. Medir ganho real no master

Objetivo:

- confirmar que o trabalho movido para o slave realmente reduz custo no master

Passos executaveis:

1. Comparar o baseline com a fase 1 no mesmo trecho de pista
2. Medir `sh2MasterStreamTicksThisFrame_` e `sh2MasterDrawTicksThisFrame_`
3. Medir a variacao de `coordinator_.Telemetry().submittedTrackSegments`
4. Medir quantas vezes a lista anterior foi reutilizada
5. Verificar se o frame continua dentro do budget de FPS alvo

Metricas:

- `sh2MasterStreamTicksThisFrame_`
- `sh2MasterDrawTicksThisFrame_`
- `sh2MasterFrameTicksThisFrame_`
- `submittedTrackSegments`
- `trackSegmentsSkippedByBudget`

Criterio de saida:

- queda mensuravel de custo no master, sem aumentar falhas
- nada de regressao de estabilidade visual

### Fase 3. Expandir o uso do slave para preparacao de baixo risco

Objetivo:

- usar o slave tambem para tarefas de preparacao que nao precisem do estado final do frame

Passos executaveis:

1. Selecionar uma segunda tarefa de preparacao que possa ser cacheada ou repetida
2. Garantir que a tarefa tenha rollback simples
3. Garantir que o master consiga seguir com o estado anterior se o slave atrasar
4. Manter o timeout curto o bastante para nao introduzir jitter de frame

Metricas:

- tempo medio por tarefa no slave
- numero de reexecucoes
- quantidade de rollbacks
- permanencia em safe mode

Criterio de saida:

- ganho adicional sem degradar a previsibilidade do frame

## Gates de seguranca

### Gate 1. Timeout

Se o trabalho do slave ultrapassar o limite de frames em voo:

- a lista anterior e reutilizada
- `timeoutFallbacks` aumenta
- o sistema entra em safe mode

### Gate 2. Regressao de estabilidade

Se `consecutiveTimeouts` crescer de forma repetida:

- o slave e desabilitado por cooldown
- o sistema volta para fallback sincronico

### Gate 3. Jitter do frame

Se `sh2MasterFrameTicksThisFrame_` aumentar sem ganho de qualidade visivel:

- a fase deve ser revertida
- o proximo teste deve reduzir escopo

### Gate 4. Pressao de memoria

Se a memoria livre ficar instavel durante a ativacao do slave:

- manter o slave desligado na corrida
- corrigir primeiro o custo de memoria antes de reativar paralelismo

## Metricas finais de aceite

O uso dual do SH2 so pode ser considerado aprovado quando estes sinais estiverem estaveis:

- `jobsCompleted / jobsSubmitted` proximo de `1.0`
- `timeoutFallbacks == 0` em varias voltas
- `lastLatencyFrames <= 3` na maior parte do tempo
- `maxLatencyFrames` dentro do teto acordado
- `safeModeTriggers == 0` ou extremamente raro em corrida normal
- `sh2MasterDrawTicksThisFrame_` menor do que o baseline sincronico
- `sh2MasterFrameTicksThisFrame_` sem regressao de estabilidade

## Ordem recomendada de implementacao futura

1. Baseline sincronico longo
2. Ativar slave so para draw list
3. Validar ganho no master
4. Expandir para uma segunda tarefa de preparacao
5. Manter o fallback sincronico sempre disponivel
6. So entao considerar aumentar a taxa de paralelismo

## Observacao pratica

Se a fase 1 nao entregar ganho mensuravel, nao vale a pena acelerar para a fase 3.
O custo do slave so se justifica se ele reduzir custo do master ou suavizar picos de frame sem aumentar risco operacional.
