# MemoryBudgetSystem Plan

## Objetivo

Separar orçamento, classificação de pressão e telemetria de memória do restante do runtime.

O objetivo desta fase não é alterar o comportamento atual.
O objetivo é preparar um recorte claro para futura extração com risco baixo.


## Estado atual

Hoje o domínio de memória está dividido entre:

- `src/memory_budget_system.hpp`
  - snapshot simples de HWR/LWR/Cart;
  - consulta de bloco livre;
  - política de alocação PCM.

- `src/game_loop_system.hpp`
  - snapshots por estágio;
  - overlay de HWR/LWR;
  - agrupamento por tags;
  - logging de validação e fragmentação.

- `src/track_system.cxx`
  - classificação de pressão;
  - floors de segurança;
  - emergency reserve;
  - trim/recycle/manutenção;
  - telemetria de streaming sob pressão.


## Problema atual

- orçamento, observabilidade e reação à pressão estão misturados;
- `GameLoopSystem` e `TrackSystem` calculam partes do mesmo domínio;
- faltam contratos explícitos para:
  - snapshot consolidado,
  - classificação de pressão,
  - política de budget,
  - telemetria do frame.


## Recorte alvo

### O que deve virar `MemoryBudgetSystem`

- captura consolidada de snapshot de memória;
- classificação explícita de pressão;
- montagem de políticas de orçamento por subsistema;
- telemetria consolidada de budget do frame.

### O que deve continuar fora

**No `GameLoopSystem`:**

- decisão de quando exibir overlays e logs;
- composição do HUD/diagnóstico do frame.

**No `TrackSystem`:**

- ações concretas de trim, slide, recycle e reserve;
- política específica de streaming da pista.

**Nos subsistemas consumidores:**

- aplicação do orçamento já resolvido;
- fallback operacional específico.


## Fronteiras internas do futuro `MemoryBudgetSystem`

### 1. `MemorySnapshotAssembler`

Responsável por:

- consolidar snapshot de HWR/LWR/Cart;
- expor maior bloco livre;
- expor espaço livre por banco.

### 2. `MemoryPressureAssembler`

Responsável por:

- classificar pressão de memória de forma explícita;
- transportar thresholds e floors;
- isolar regras de budget de alto nível.

### 3. `MemoryBudgetPolicyAssembler`

Responsável por:

- materializar budget por domínio;
- explicitar quando PCM prefere HWR ou Cart;
- preparar contratos para track/audio/render.

### 4. `MemoryTelemetryAssembler`

Responsável por:

- consolidar números do frame;
- separar snapshot de allocator de decisão de UI/log;
- preparar terreno para telemetria Master/Slave futura.


## Dependências de entrada

- reports de `HighWorkRam`
- reports de `LowWorkRam`
- reports de `CartRam`
- maior bloco livre
- thresholds de floors/soft pressure


## Dados de saída

- `MemorySnapshotPacket`
- `MemoryPressurePacket`
- `MemoryBudgetPolicyPacket`
- `MemoryTelemetryPacket`


## Ordem futura de execução

1. domínio chamador captura snapshot consolidado
2. `MemoryPressureAssembler` classifica a pressão
3. `MemoryBudgetPolicyAssembler` monta a política por subsistema
4. subsistemas consomem a política
5. `MemoryTelemetryAssembler` consolida a telemetria do frame


## Critério de aceite da futura extração

- nenhuma regressão de bootstrap;
- nenhuma regressão de streaming da pista;
- nenhuma regressão de budget PCM;
- overlays atuais continuam possíveis;
- build continua no envelope estável.


## Restrições

- não alterar agora `GameLoopSystem` runtime;
- não alterar agora `TrackSystem` runtime;
- não integrar contratos novos ao runtime nesta etapa;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- criar contratos passivos de snapshot/pressure/policy/telemetria;
- não mover ainda as regras concretas de trim;
- só considerar integração real quando houver redução líquida no caminho crítico.
