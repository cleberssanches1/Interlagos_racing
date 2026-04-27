# Plano de Otimização — Pipeline de Renderização de Segmentos

## Estado Atual do Pipeline

### Configuração ativa (track_system.cxx:244–291)

| Constante | Valor | Significado |
|---|---|---|
| `kEnableTrackRuntimeStabilization` | `true` | Caminho estabilizado ativo |
| `kEnableTrackLeakIsolationFixed64Pipeline` | `true` | Modo de isolamento ativo |
| `kTrackLeakIsolationWindowSegments` | `20` | Janela real de segmentos carregados |
| `kTrackSegmentLimit` | `30` | Limite máximo de arrays (maior que a janela real) |
| `kSlotPoolSize` | `21` | 20 ativos + 1 staging |
| `kEnableLeakIsolationMixedLodProfile` | `true` | Ranks 0–9 → 64×64, ranks 10–19 → 32×32 |
| `kEnableTrackLodBandsInStabilization` | `true` | Multi-banda LOD ativa no caminho estabilizado |
| `kEnableDeterministicStabilizedSlide` | `true` | Slide determinístico sem rebuild agressivo |
| `kEnableStabilizedProducerOnSlave` | `true` | Producer roda no Slave SH2 |
| `kEnableStabilizedDepthSortOnSlave` | `true` | Depth sort roda no Slave SH2 |
| `kEnableTrackSlaveBarrierLockstep` | `false` | Master não bloqueia esperando Slave |
| `kEnableStabilizedEndFramePaletteRecycle` | `false` | Reciclo de paleta no fim do frame **desativado** |
| `kEnableRuntimeTextureCompaction` | `false` | Compactação de heap de textura **desativada** |
| `SGL_MAX_POLYGONS` | `2200` | Budget de polígonos SGL (makefile) |
| `SGL_MAX_VERTICES` | `2800` | Budget de vértices SGL (makefile) |

### Fluxo por frame (RenderFrame — track_system.cxx:15846)

```
UpdateCameraDrivenWindowDirection()   ← direção da janela pela câmera
TickRuntimeFrameCooldowns()
TrackMaintenanceStage::RunInitial()   ← manutenção periódica de memória
TrackWindowStage::Run()               ← avança a janela conforme posição do carro
TrackPrefetchStage::RunCompaction()   ← compactação de textura (desativada)
TrackPrefetchStage::RunPrefetch()     ← pré-carrega segmento seguinte
TrackMaintenanceStage::RunPostSlide() ← manutenção pós-slide
TrackMaintenanceStage::RunLegacy()    ← manutenção legada (trim de LWR)
BuildAndApplyFramePlanStage()         ← plano de frame + depth sort (decimado)
  └─ BuildStabilizedSortedHandles()   ← depth sort no Slave SH2
TrackLodStage::RunRecovery()          ← recuperação de LOD pendente
BuildOrderedHandlesStage()            ← (no-op em modo estabilizado)
TrackWorkingSetStage::Run()           ← atualiza working set de famílias
RunDrawStage()
  └─ RenderVisibleSegmentOrderStabilized()
       ├─ [reusa framePlanSortedHandles_ se plano válido e sem slide]
       ├─ [producer no Slave] Build() + Consume()
       └─ Para cada segmento:
            ├─ IsRendererStateIntegral()        ← verifica integridade
            ├─ CountMissingOrDeadRequired...()  ← verifica slots de textura
            ├─ [se faltando] RebuildSegment...() ← reparo de slots
            ├─ coordinator_.Prepare()
            └─ coordinator_.Execute()
```

---

## Problemas Identificados

---

### Problema 1 — `initialSegments = 10` conflita com a janela real de 20 segmentos

**Arquivo:** [src/main.cxx:852](src/main.cxx#L852)

```cpp
trackConfig.initialSegments = kFixed64Mode ? 10u : 20u;  // ← 10, mas janela real = 20
trackConfig.minSegments     = kFixed64Mode ? 10u : 20u;
```

`ResolveInitialLoadLimit` (track_system.cxx:12550) **ignora** `config.initialSegments` em modo fixed64 e retorna diretamente `kTrackLeakIsolationWindowSegments = 20`. O valor `10` é passado para o coordinator e para os limites adaptativos (`coordinatorConfig.budget.maxTrackSegments`, linha 12605), gerando inconsistência entre o budget do coordinator (configurado para 10) e a janela real (20 slots carregados). O coordinator pode cortar segmentos que deveriam ser renderizados.

**Impacto:** ALTO — pode causar segmentos "invisíveis" mesmo com slots carregados.

---

### Problema 2 — `kTrackSegmentLimit = 30` superaloca arrays para janela de 20

**Arquivo:** [src/track_system.hpp:70](src/track_system.hpp#L70)

`kTrackSegmentLimit = 30` dimensiona todos os arrays fixos do sistema:

```cpp
std::array<SegmentRenderEntry*, kTrackSegmentLimit>  preparedEntries   // 30 slots
std::array<uint8_t, kTrackSegmentLimit + 1>          preparedCountById // 31 slots
std::array<uint8_t, kTrackSegmentLimit>              desiredLod...     // 30 slots
std::array<int16_t, kTrackSegmentLimit>              desiredBaseRank   // 30 slots
```

Com janela real de 20, os índices 20–29 nunca são usados. Além disso, os loops
em `BuildAndApplyFramePlanStage` e `UpdateDesiredStabilizedWindowLodTargets` iteram
até `segmentRenderers_.size()` (= 20), então o overhead de CPU é mínimo. O
desperdício é principalmente de stack/LWR em arrays estáticos inlinados.

**Impacto:** BAIXO — arrays em stack local, não em LWR persistente. Sem urgência.

---

### Problema 3 — Budget de polígonos SGL não calibrado para o perfil atual

**Arquivo:** [makefile:16–17](makefile#L16), [src/main.cxx:856](src/main.cxx#L856)

```makefile
SGL_MAX_POLYGONS = 2200
SGL_MAX_VERTICES = 2800
```

```cpp
trackConfig.initialFaces = SGL_MAX_POLYGONS - 64;  // 2136 — reserva fixa de 64 para o carro
```

O carro é carregado **depois** da pista (`loadCarAfterTrack = true`), então `faceCount = 0`
quando `initialFaces` é calculado. A reserva de 64 faces para o carro é arbitrária e
provavelmente insuficiente (carro tem mais faces que 64).

Para 20 segmentos com perfil mixed-LOD (10×64×64 + 10×32×32), o pico real de
polígonos por frame não foi medido. Valores acima do necessário desperdiçam a
TransList do SGL; valores abaixo causam corrupção de stack.

O campo `sh2ProducerListUsedThisFrame_` indica se o producer Slave foi aproveitado
(`1`) ou se houve fallback para a lista mestre. O número real de faces submetidas
está em `coordinator_.Telemetry().submittedTrackFaces`.

**Impacto:** MÉDIO — calibração errada pode subaproveitar o budget do coordinator
ou deixar espaço desperdiçado que poderia ir para mais segmentos ou resolução.

---

### Problema 4 — Verificação de slots ausentes executada para todos os segmentos a cada frame

**Arquivo:** [src/track_system.cxx:14757](src/track_system.cxx#L14757)

```cpp
// Chamado para cada um dos 20 segmentos, em todo frame
const uint32_t missingSlots = CountMissingOrDeadRequiredFaceTextureSlots(
    entry->lodState.currentFaceSlots,
    &entry->lodState.faceFamilyIds);
```

`CountMissingOrDeadRequiredFaceTextureSlots` itera sobre todos os pares (face, família)
de cada segmento para checar se algum slot de textura VDP1 foi eviccionado ou morreu.
Em steady state (sem slide, sem LOD change, sem compactação), o resultado desta
checagem é sempre `0` — o trabalho é repetido inutilmente.

Slots só ficam inválidos quando:
1. Um slide ocorre (novo segmento entra, textura antiga pode ser eviccionada)
2. Um LOD change é aplicado
3. A compactação de textura roda (atualmente desativada)
4. Um slot VDP1 é reciclado por pressão de VRAM

**Impacto:** MÉDIO — 20 iterações O(faces) por frame a cada frame, mesmo sem mudanças.

---

### Problema 5 — Decimação do frame planner pode ser mais agressiva

**Arquivo:** [src/track_system.cxx:15952](src/track_system.cxx#L15952)

```cpp
const uint8_t steadyPlanSkipFrames =
    kEnableTrackLeakIsolationFixed64Pipeline
        ? (prefetchResident ? 20u : 12u)   // ← skip atual
        : (prefetchResident ? 8u : 4u);
```

`BuildAndApplyFramePlanStage` inclui:
- `BuildFrameSnapshot` — captura estado de câmera e carro
- `BuildStabilizedSortedHandles` — depth sort dos 20 segmentos no Slave SH2
- Montagem do `framePlanCurrent_` — copia IDs e LOD alvos

Em corrida estável (linha reta, camera estática, sem slide), a ordem relativa dos
20 segmentos não muda entre frames. O plano de 20 frames pode ser estendido com
segurança para 30–40 frames, reduzindo a frequência do sort Slave.

O risco é: se o carro faz uma curva brusca durante o skip, o depth sort desatualizado
renderiza segmentos em ordem errada (artefatos de transparência/sobreposição).
A velocidade do carro no Saturn é baixa o suficiente para que 40 frames (~1.3s a 30fps)
seja seguro em curvas normais.

**Impacto:** BAIXO-MÉDIO — reduz carga do Slave SH2 em corrida estável.

---

### Problema 6 — `kEnableStabilizedEndFramePaletteRecycle = false` acumula CRAM

**Arquivo:** [src/track_system.cxx:264](src/track_system.cxx#L264)

```cpp
// No estado atual, reciclar palette no fim de frame causa flashes roxos
// intermitentes em alguns emuladores/hardware timing. Mantemos desligado.
static constexpr bool kEnableStabilizedEndFramePaletteRecycle = false;
```

Paletas de segmentos que saíram da janela continuam ocupando CRAM indefinidamente.
Em uma sessão de corrida de 10+ minutos (300+ slides), o acúmulo de paletas mortas
pode esgotar CRAM e causar corrupção de cor.

O flash roxo mencionado no comentário provavelmente ocorre porque a reciclagem está
acontecendo durante o draw do VDP1 (janela errada de VDP1 double-buffer). A reciclagem
precisa ocorrer **depois** do flip de frame, não antes.

**Impacto:** MÉDIO a LONGO PRAZO — não causa crash imediato, mas degrada corridas longas.

---

### Problema 7 — Rebuild de janela inteira na troca de direção de câmera

**Arquivo:** [src/track_system.cxx:12222](src/track_system.cxx#L12222)

```cpp
if (!RebuildActiveSegmentWindow(desiredStartId, windowCount, desiredDirection)) return;
```

`UpdateCameraDrivenWindowDirection` chama `RebuildActiveSegmentWindow` quando a câmera
inverte a direção. Isso reconstrói todos os 20 segmentos simultaneamente — cada um
requer leitura de CD, alocação de LWR, e upload de textura para VDP1. O resultado é
um spike de frame visível no momento da inversão.

A hysteresis de cos(100°)/cos(80°) mitiga inversões acidentais, mas o spike ao
inverter intencionalmente (botão Y + L/R até 180°) é inerente à implementação atual.

**Impacto:** BAIXO-MÉDIO — spike único na inversão, não afeta steady state.

---

### Problema 8 — `TRACK_LWR_STAGE_TRACE` ativo por padrão corrompe baseline de LWR

**Arquivo:** [makefile:27](makefile#L27)

```makefile
SRL_CUSTOM_CCFLAGS = -DTRACK_LWR_STAGE_TRACE  # ativo em builds não-perf
```

O trace injeta chamadas a `SRL::Memory::LowWorkRam::GetReport()` a cada stage por
frame, e acumula strings em `g_lwrStageAccum`. Isso cria pressão de LWR no próprio
código de diagnóstico — o "leak" medido inclui o overhead do probe. Medições de
baseline de LWR devem ser feitas com `make BUILD_PROFILE=perf`.

**Impacto:** BAIXO — apenas em builds de debug; build perf já disponível.

---

## Sequência de Implementação Recomendada

| # | Ação | Arquivo | Linhas | Impacto | Complexidade |
|---|---|---|---|---|---|
| 1 | Corrigir `initialSegments` de `10` para `20` | [main.cxx](src/main.cxx) | 852–853 | ALTO | BAIXO |
| 2 | Instrumentar `submittedTrackFaces` e medir pico real antes de calibrar SGL | [main.cxx](src/main.cxx) | 856 | MÉDIO | BAIXO |
| 3 | Calibrar `SGL_MAX_POLYGONS`, `SGL_MAX_VERTICES` e `initialFaces` com dados reais | [makefile](makefile), [main.cxx](src/main.cxx) | 16–17, 856 | MÉDIO | BAIXO |
| 4 | Adicionar dirty flag de slots por entrada para evitar `CountMissing...` toda frame | [track_system.cxx](src/track_system.cxx) | 14757 | MÉDIO | MÉDIO |
| 5 | Aumentar `steadyPlanSkipFrames` de `20/12` para `40/24` | [track_system.cxx](src/track_system.cxx) | 15952–15955 | BAIXO | BAIXO |
| 6 | Investigar causa raiz do flash roxo e reativar `kEnableStabilizedEndFramePaletteRecycle` | [track_system.cxx](src/track_system.cxx) | 264 | MÉDIO | MÉDIO |
| 7 | Usar `make BUILD_PROFILE=perf` para medir baseline limpo de LWR/FPS | [makefile](makefile) | — | BAIXO | BAIXO |

---

## Critérios de Aceitação por Ação

### Ação 1 — `initialSegments = 20`
- `TRK band 64:10 32:10` no overlay (10 + 10 = 20 segmentos renderizados)
- Sem segmentos "pulados" pelo coordinator (`runtimeSafeSkippedThisFrame_ == 0`)

### Ações 2 e 3 — Calibração SGL
- `submittedTrackFaces` medido em 5 voltas completas, pico registrado
- `SGL_MAX_POLYGONS ≥ pico_track + pico_carro + 10%` com margem de segurança
- Sem overflow de TransList (sem corrupção de tela)

### Ação 4 — Dirty flag de slots
- `runtimeSafeReappliedThisFrame_` = 0 em steady state (sem slide, sem LOD change)
- Zero regressão em frames com slide ou LOD promotion

### Ação 5 — Decimação de frame plan
- FPS estável ou melhor em linha reta
- Sem artefatos de sobreposição em curvas a velocidade máxima

### Ação 6 — Reciclo de paleta
- Sem flashes de cor após 10+ minutos de corrida contínua
- CRAM livre não decresce monotonicamente durante a corrida

---

## Métricas de Referência (baseline atual estimado)

| Métrica | Valor observado |
|---|---|
| Segmentos renderizados | 20 (10×64×64 + 10×32×32) |
| `TRK band` esperado | `64:10 32:10` |
| Budget coordinator | ~10 segs (inconsistente com janela de 20) |
| FPS alvo | 30 fps |
| `SWLWR free` durante corrida | a medir com `BUILD_PROFILE=perf` |
