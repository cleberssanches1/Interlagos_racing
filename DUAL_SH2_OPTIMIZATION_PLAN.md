# Plano de Otimização Dual-SH2 — Interlagos Racing

## Contexto

O Sega Saturn possui duas CPUs SH2 idênticas a 28,6 MHz. A Slave SH2 pode executar uma tarefa
por vez, de forma assíncrona, enquanto a Master continua o trabalho serial. A sincronização
ocorre via `SRL::Slave::ExecuteOnSlave()` + `ITask::IsDone()`, com barreira final em
`SRL::Core::Synchronize()` no VBlank.

### Estado atual

| CPU | O que faz hoje |
|-----|----------------|
| **Master SH2** | 100% do loop: input → physics → câmera → HUD → track render dispatch → car render → sync |
| **Slave SH2** | Apenas `SlaveTrackDrawProducer` (depth-sort dos segmentos de pista, ~2ms) |

**Problema central**: a `SimulationTask` (physics + gameplay tick) e a `CarRenderPrepareTask`
estão **implementadas e funcionais**, mas desativadas em `main.cxx:649-650` com o comentário:

```
// Estabilidade: manter apenas um pipeline na Slave por frame (pista).
// Simulation/car prepare em Slave junto com producer da pista causa conflito de jobs.
```

O conflito é real: `SRL::Slave::ExecuteOnSlave()` aceita um job por vez. Se a simulação ainda
estiver rodando quando `TrackDrawProducer` tentar submeter, ocorre race condition. A solução
não é desativar tudo — é garantir que os jobs ocupem **janelas temporais distintas** dentro
do mesmo frame, com uma barreira mínima entre eles.

---

## Análise do Frame Atual

```
── Frame N (16,67ms @ 60Hz) ──────────────────────────────────────────────────
Master │ [Input] [Physics 2–5ms] [Camera] [HUD] [TrackDispatch] [CarRender] ··· [Sync] │
Slave  │                                         [DepthSort 1–2ms]                     │
       │                                                                                 │
       └── Physics serial no Master é o maior desperdiçador de ciclos da Slave ─────────┘
```

**Observações do profiling (logs `ms:` e `fr:`):**

- `FPS 17.6 ms:56.9` → frame de ~57ms indica que physics + render excedem 33ms (30Hz budget)
- `FPS 11.7 ms:85.3` → frame de slide: `BuildSegmentIntoRenderer` rodando sincronamente no Master
  absorve 50–60ms
- `df:0` entre slides confirmado: LWR estável entre os passos anteriores
- Slave track producer: ativo, com safe-mode configurado
  (`maxFramesInFlight_=2`, `safeModeCooldownFrames_=90`)

### Dependências críticas entre tarefas

```
Input (Master)
    │
    ├─→ Physics/Gameplay [Slave ← proposto]
    │       └─ Resultado necessário por: Camera, CarRender
    │
    ├─→ Camera (Master) ← independente de Physics no mesmo frame
    │
    ├─→ HUD (Master) ← independente de Physics
    │
    ├─→ TrackDrawProducer [Slave] ← precisa câmera pronta, NÃO precisa de Physics
    │
    └─→ CarRender (Master) ← precisa de Physics (posição do carro)
```

**Conclusão**: Camera, HUD e TrackDraw **não dependem** do resultado de Physics do frame
corrente. Physics e TrackDraw podem ocupar janelas separadas da Slave sem sobreposição
se organizados na ordem correta.

---

## Frame Alvo (Pós-Otimização)

```
── Frame N otimizado ─────────────────────────────────────────────────────────
Master │ [Input] [Camera] [HUD] [wait?] [CarRender N-1] [PrefetchBuild] ··· [Sync] │
Slave  │         [Physics N ──────]     [DepthSort N ──]  [PrefetchR N]            │
       │                                                                              │
       │  Physics overlap: Camera+HUD no Master (~2ms) esconde ~2ms de Physics       │
       └──────────────────────────────────────────────────────────────────────────────┘
```

**Ganho projetado**: 2–4ms de physics removidos do critical path do Master por frame.

---

## Plano de Ação

### Passo 1 — Re-habilitar `SimulationTask` com barreira segura antes do TrackDraw

**Arquivo**: `src/game_loop_system.hpp` + `src/main.cxx`

**Problema**: Physics e TrackDraw submetidos ao Slave na mesma "janela" sem sincronização
explícita entre eles.

**Solução**: Adicionar `DrainSimulationJobIfInFlight()` chamado imediatamente antes de
`trackSystem->RenderFrame()`. Isso garante que a Slave esteja livre quando o TrackDrawProducer
tentar submeter.

#### 1a. `main.cxx` — reativar flag

```cpp
// ANTES (linha 649-650):
loopContext.enableSlaveForCarPrepare = false;
loopContext.enableSlaveForSimulation = false;

// DEPOIS:
loopContext.enableSlaveForCarPrepare = false;   // ainda trivial demais (~1 ciclo)
loopContext.enableSlaveForSimulation = true;    // habilitar Slave para Physics
```

#### 1b. `game_loop_system.hpp` — novo helper `DrainSimulationJobIfInFlight()`

Adicionar após `ConsumeCompletedJobs()` (linha 883):

```cpp
// Drenar job de simulação se ainda em voo (antes de submeter TrackDrawProducer).
// No melhor caso: Slave já terminou → sem espera, ganho puro.
// No pior caso: Slave ainda rodando → spin-wait inline → sem regressão vs. síncronoo.
void DrainSimulationJobIfInFlight()
{
    if (!simJobInFlight_) return;
    if (simulationTask_.IsDone())
    {
        // Já terminou durante Camera/HUD → consumir resultado sem stall
        simJobInFlight_ = false;
        simHasCompleted_ = true;
        simCompletedIdx_ = simInFlightIdx_;
        return;
    }
    // Ainda rodando — spin-wait (idêntico a não ter paralelizado, sem regressão)
    constexpr uint32_t kSpinLimit = 4u * 1024u * 1024u;
    uint32_t spins = 0;
    while (!simulationTask_.IsDone())
    {
        if (++spins > kSpinLimit) break;
    }
    simJobInFlight_ = false;
    simHasCompleted_ = true;
    simCompletedIdx_ = simInFlightIdx_;
}
```

#### 1c. `RenderFrame()` — chamar barreira antes do TrackDraw

```cpp
void RenderFrame(const CameraFrameState& camera)
{
    // ...
    if (context_.trackSystemReady && context_.renderTrack)
    {
        DrainSimulationJobIfInFlight();  // ← ADICIONAR AQUI, antes de RenderFrame
        context_.trackSystem->RenderFrame(...);
    }
}
```

#### 1d. `ConsumeCompletedJobs()` — consumir resultado de Physics no próximo frame

O código existente em `ConsumeCompletedJobs()` (linha 883) já trata `simJobInFlight_` e
lê `simOutput_[simCompletedIdx_]`. Verificar que `context_.carWorldPosition` e
`carYawDeg_` são atualizados corretamente no consumo.

**Nota de latência (1-frame pipelining)**:
O modelo atual já é pipeline por design: `ExecuteGameplayFrame()` submete mas não espera.
`ConsumeCompletedJobs()` lê o resultado do frame anterior. Isso significa que o carro
renderizado usa a posição calculada **no frame anterior**. Para corrida a 30–60Hz isso é
imperceptível (<17ms de lag de entrada).

Se o 1-frame de lag for inaceitável (ex: modo replay), reverter para síncronoo via
`context_.enableSlaveForSimulation = false`.

**Telemetria para validar**:
- Monitorar `ms:` e `df:` nos logs após ativar
- `df:0` entre frames sem slide deve ser mantido
- FPS deve subir de 17.6 → próximo de 30Hz se Physics = gargalo

---

### Passo 2 — Mover `BuildSegmentIntoPrefetch` (renderer build) para a Slave

**Contexto**: O passo anterior (`DUAL_SH2_OPTIMIZATION_PLAN.md`) moveu o `BuildSegmentIntoRenderer`
para o frame de prefetch. Mas ele ainda roda no **Master** durante o frame de prefetch, causando
um spike de ~60ms nesse frame.

**Solução**: Criar uma `RendererPrefetchTask : public SRL::Types::ITask` que executa
`BuildSegmentIntoRenderer` na Slave durante a janela entre TrackDraw e o próximo VBlank.

**Janela disponível**:
```
[TrackDrawProducer finaliza] → [VBlank / Synchronize]
         ↑
    Janela livre na Slave (~5–8ms típico)
    → usar para construir geometria do segmento incoming
```

#### 2a. Nova classe `RendererPrefetchTask` em `track_system.hpp`

```cpp
class RendererPrefetchTask final : public SRL::Types::ITask
{
public:
    struct Input
    {
        int32_t segmentId = -1;
        TrackRenderer* targetRenderer = nullptr;
        // ponteiro para dados de segmento (leitura apenas na Slave)
        const TrackRuntimePackView* packView = nullptr;
    };
    struct Output
    {
        bool built = false;
        SRL::Math::Types::Vector3D center{};
        // family IDs não podem ser escritos em LWR de forma segura na Slave
        // sem sincronização adicional — serializar via scratch persistente
    };

    void Configure(const Input* in, Output* out) { input_ = in; output_ = out; }

private:
    void Do() override;  // Chama BuildSegmentIntoRenderer(input_->segmentId, ...)
    const Input* input_ = nullptr;
    Output* output_ = nullptr;
};
```

#### 2b. Modificar `BuildSegmentIntoPrefetch()` para agendar na Slave

```cpp
// Em BuildSegmentIntoPrefetch(), Fase 2:
if (!slidePrefetchRendererReady_ && !rendererPrefetchJobInFlight_)
{
    // Agendar build na Slave em vez de executar no Master
    if (IsSlaveFreeForPrefetch())  // não conflitar com TrackDraw ou SimTask
    {
        rendererPrefetchInput_.segmentId = segmentId;
        rendererPrefetchInput_.targetRenderer = slideScratchRenderer_.get();
        rendererPrefetchInput_.packView = &g_runtimePackView;
        rendererPrefetchTask_.Configure(&rendererPrefetchInput_, &rendererPrefetchOutput_);
        SRL::Slave::ExecuteOnSlave(rendererPrefetchTask_);
        rendererPrefetchJobInFlight_ = true;
    }
}
```

#### 2c. Consumir resultado do prefetch Slave em `BeginFrame()`

```cpp
void TrackSystem::BeginFrame(uint32_t frameId)
{
    if (rendererPrefetchJobInFlight_ && rendererPrefetchTask_.IsDone())
    {
        rendererPrefetchJobInFlight_ = false;
        if (rendererPrefetchOutput_.built)
        {
            slidePrefetchCenter_ = rendererPrefetchOutput_.center;
            slidePrefetchRendererReady_ = true;
        }
    }
    // ...
}
```

**Restrição importante**: A Slave SH2 não tem acesso direto à LWR do Master de forma segura
sem cache flush. `BuildSegmentIntoRenderer` escreve em `slideScratchRenderer_` (LWR) e em
vetores de família. Para Slave acessar LWR do Master:
- Usar `CacheThroughPtr()` ao passar ponteiros para a Slave (já feito no TrackDrawProducer)
- Ou mover o renderer scratch para a área de cache-through (não-cached LWRAM)

**Implementar Passo 2 somente após Passo 1 estar estável em hardware.**

---

### Passo 3 — Identificar e refatorar trabalho pesado do Master no frame de slide

**Contexto**: Mesmo com Passo 1 ativo, o frame de slide tem overhead do Master em:
- `RebuildActiveWindowLookupTables()` — reconstrói lookup table de handles
- `MergeCurrentWindowFamilies()` — mescla famílias de textura após slide
- `BuildSegmentHandleTable()` — reconstrói tabela de handles de segmento

Esses são chamados em `ExecuteDeterministicStabilizedSlide()` após o commit.

**Análise de custo**: Medir com `fr:` nos logs antes e depois do slide para isolar.

**Ação**: Se qualquer um desses ultrapassar 1ms, avaliar se pode ser movido para Slave
ou diferido para o próximo frame (lazy rebuild com flag `dirty`).

```cpp
// Em ExecuteDeterministicStabilizedSlide(), após commit:
segmentHandles_ = BuildSegmentHandleTable();  // potencial candidato para Slave
// MergeCurrentWindowFamilies() já tem cooldown de 1-6 frames — ok
```

**Passo 3 é diagnóstico**: Coletar logs com `enableRuntimeStatsLogs=true` e `fr:` por frame
de slide. Só implementar se diferença for ≥ 2ms.

---

### Passo 4 — Orquestração de Slave com múltiplos tipos de job

À medida que mais trabalho migra para a Slave, o scheduling ad-hoc de
`SRL::Slave::ExecuteOnSlave()` direto espalhado por vários sistemas torna-se frágil.

**Proposta**: Adicionar um `SlaveJobScheduler` centralizado no `GameLoopSystem` que:
1. Registra jobs com prioridade e janela de execução (pre-render, post-render, idle)
2. Garante que apenas um job esteja em voo por vez
3. Provê telemetria unificada (`jobsSubmitted`, `jobsCompleted`, `stalls`)

```cpp
// Esboço (apenas referência — implementar quando Passos 1-3 estiverem validados)
class SlaveJobScheduler
{
public:
    enum class Window { PreRender, PostRender, Idle };

    void Schedule(SRL::Types::ITask& task, Window w, uint8_t priority = 0);
    void Drain(Window upToWindow);  // bloqueia até jobs da janela estarem prontos
    bool IsFree() const;
    void Tick();  // chamar no início de cada frame
};
```

**Este passo é arquitetural** e deve vir após os anteriores estarem funcionando em hardware.

---

## Resumo de Prioridades

| Passo | Ação | Risco | Impacto | Pré-requisito |
|-------|------|-------|---------|---------------|
| **1** | Reativar SimulationTask + barreira `DrainSimulationJobIfInFlight()` antes do TrackDraw | Baixo | +2–4ms de física paralela | Nenhum |
| **2** | RendererPrefetchTask na Slave (geometria de segmento em background) | Médio | Elimina spike de prefetch no Master | Passo 1 estável |
| **3** | Diagnóstico e adiamento de trabalho pós-slide (lookup tables, merge families) | Baixo | +1–2ms se gargalo confirmado | Métricas `fr:` coletadas |
| **4** | SlaveJobScheduler centralizado | Alto (refactor) | Sustentabilidade arquitetural | Passos 1-3 validados |

---

## Sequência de Implementação Recomendada

```
[ Passo 1a ] main.cxx: enableSlaveForSimulation = true
      ↓
[ Passo 1b ] Adicionar DrainSimulationJobIfInFlight()
      ↓
[ Passo 1c ] Chamar barreira antes de trackSystem->RenderFrame()
      ↓
[ Build + Hardware test ]
      ↓ Monitorar: ms:, df:, fr: nos logs
      ↓ Critério: FPS >= 25 sem safe-mode triggers no track producer
      ↓
[ Passo 3  ] Medir frame de slide com fr: detalhado
      ↓ Só continuar se slide overhead > 2ms confirmado
      ↓
[ Passo 2  ] RendererPrefetchTask na Slave
      ↓
[ Passo 4  ] SlaveJobScheduler (quando Passos 1-3 estáveis em 10+ voltas)
```

---

## Critérios de Aceitação

| Métrica | Meta | Como medir |
|---------|------|-----------|
| FPS entre slides | ≥ 30 Hz | `FPS:` nos logs de hardware |
| FPS no frame de slide | ≥ 25 Hz (tolerável) | `FPS:` logo após `sl:1` |
| `df:` entre frames normais | `0` | Overlay `SWLWR free:` |
| `df:` no frame de slide | ≤ −500 bytes | Picos durante `sl:1` |
| `safeModeTriggers` (track producer) | `0` por volta | `TRK pf md:` nos logs |
| `timeoutFallbacks` (track producer) | `0` por volta | Log de telemetria da pista |
| `ms:` por frame (sem slide) | ≤ 33ms (30Hz) | Overlay `ms:` |

---

## Restrições do Hardware Sega Saturn

- **SH2 Slave**: executa uma `ITask` por vez via `SRL::Slave::ExecuteOnSlave()`
- **LWR (Low Work RAM)**: 1 MB compartilhado; Slave acessa via cache-through — usar
  `CacheThroughPtr()` em todos os ponteiros passados para tasks Slave
- **Cache line**: 16 bytes; estruturas passadas para Slave devem ser alinhadas para evitar
  false sharing (já garantido pelo `CompilerFence()` existente no TrackDrawProducer)
- **Stack da Slave**: limitado (~2 KB); tasks Slave não devem alocar arrays grandes no stack
- **Reentrada**: `SRL::Slave::ExecuteOnSlave()` é não-reentrante; só um job em voo por vez

---

## Apêndice — Estado Atual dos Flags em `main.cxx`

```cpp
// main.cxx linhas 647-650 (comentário original do desativamento):
// Estabilidade: manter apenas um pipeline na Slave por frame (pista).
// Simulation/car prepare em Slave junto com producer da pista causa conflito de jobs.
loopContext.enableSlaveForCarPrepare = false;  // trivial, não reativar
loopContext.enableSlaveForSimulation = false;  // reativar no Passo 1
```

```cpp
// game_loop_system.hpp — flags de telemetria da SimulationTask:
bool simJobInFlight_ = false;    // true se job Slave em voo
bool simHasCompleted_ = false;   // true após IsDone() consumido
uint8_t simWriteIdx_ = 0;        // double-buffer index de escrita
uint8_t simInFlightIdx_ = 0;     // double-buffer index em voo
```

---

## Apêndice — Diagrama de Dependências de Jobs por Frame

```
Frame N:
  Master: [Input]──→[Physics→Slave]   [Camera][HUD]──wait?──[CarRender]──[Sync]──┐
  Slave:                [Physics N ──────────]     [DepthSort N ──]               │
                                                                              VBlank N

Frame N+1:
  Master: [Input]──→[ConsumePhysics N]──→[Physics N+1→Slave] ...
  Master usa outWorldPosition/carYawDeg do frame N para renderizar o frame N
  (1-frame pipeline lag — imperceptível a 30/60Hz)
```
