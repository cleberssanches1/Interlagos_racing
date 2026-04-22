# Plano de Ação: Otimização de Pipeline e Estabilização de WorkRAM

## 1) Diagnóstico a partir dos logs

Sinais recorrentes observados:

- `LWT2 tx` (TrackTexture) e `LTX2 tr` (track total) sobem gradualmente ao longo do tempo.
- `LFO1 py` (payload) também cresce em conjunto.
- `LTK1 st` (stream ticks) tem picos periódicos, causando queda de FPS.
- `PB b:0/1` em muitos frames: orçamento de prefetch pouco aproveitado.
- `free` cai lentamente, sem colapso imediato, indicando retenção/crescimento gradual (não apenas pico momentâneo).

Leitura prática:

- Há custo crescente por frame no pipeline de streaming/deslizamento.
- Parte da memória de textura/estado de pista não está retornando ao patamar inicial com a velocidade necessária.
- O problema é tanto de **retenção de estado** quanto de **custo de manutenção em runtime**.

---

## 2) Hipóteses técnicas mais prováveis

1. **Crescimento progressivo de recursos de textura de pista**
   - Slots/paletas aposentados não retornam ao pool no ritmo esperado.
   - Reuso ineficiente de slots pode aumentar `tx/tr` continuamente.

2. **Caminho de manutenção caro no steady-state**
   - `RunWorkRamMaintenance` e tarefas de slide/prefetch em cadência alta elevam `st`.
   - Picos de manutenção coincidem com queda visível de FPS.

3. **Prefetch subótimo**
   - Baixa taxa de uso de build budget (`PB`), aumentando carga síncrona no slide.
   - Quando o prefetch falha, trabalho cai no frame crítico e custa mais.

4. **Estruturas com retenção acumulada**
   - Vetores/scratch buffers e cache de famílias podem estabilizar acima do necessário para a janela ativa.

---

## 3) Objetivo de engenharia

Manter FPS estável em sessão longa (>= 60 min), com:

- `LWT2 tx` e `LTX2 tr` oscilando em banda estreita após aquecimento.
- `LFO1 py` sem tendência de alta contínua.
- `LTK1 st` sem picos recorrentes acima do orçamento de frame.

---

## 4) Plano em fases

### Fase A: Instrumentação obrigatória (sem alterar comportamento)

1. Adicionar contadores por frame:
   - `tex_count_before/after`
   - `reused_slots`, `retired_queued`, `retired_flushed`
   - `new_uploads`, `reuse_uploads`
   - `family_slots_size` e `active_window_family_count`
2. Logar deltas por slide:
   - `delta_tx`, `delta_tr`, `delta_payload`
3. Registrar histograma simples de picos:
   - frames com `LTK1 st > threshold` (ex.: 1000 ticks)

Critério de saída da fase:
- conseguir apontar exatamente quais eventos aumentam `tx/tr/py`.

---

### Fase B: Contenção de crescimento (mudança de comportamento controlada)

1. **Hard cap de residência de textura de pista**
   - Definir teto por modo (fix64) para `trackTexUsed`.
   - Ao atingir teto, forçar flush/reuse antes de novos uploads.

2. **Política de aposentadoria determinística**
   - Em slide concluído, garantir fila de aposentados drenada dentro de N frames.
   - Falha em drenar => prioridade máxima no frame seguinte.

3. **Bound de cache por janela**
   - Garantir que cache de famílias mantenha apenas:
     - famílias da janela ativa,
     - famílias de prefetch imediato.
   - Remoção explícita das famílias fora desse conjunto.

4. **Cadência adaptativa de manutenção**
   - Em memória estável: reduzir frequência.
   - Em pressão real: aumentar agressividade temporária.

Critério de saída da fase:
- após aquecimento, `tx/tr/py` entram em platô (sem tendência crescente de longo prazo).

---

### Fase C: Refatoração estrutural (fila/ring de seguimentos e recursos)

1. Tratar a pista como **ring buffer fixo de slots** (janela + staging), não como crescimento implícito.
2. Vincular recursos por `slot_id`:
   - ao desalocar slot, recursos associados voltam ao pool imediatamente.
3. Separar pools por responsabilidade:
   - pool de textura,
   - pool de família,
   - pool de buffers transitórios.
4. Unificar invariantes de ciclo de vida:
   - `allocate -> active -> retire -> reusable` com transições explícitas.

Critério de saída da fase:
- custo por frame aproximadamente constante no steady-state, independente do tempo de execução.

---

## 5) Otimizações de CPU para FPS estável

1. Reuso de plano/sort em frames estáveis (com invalidação por slide real).
2. Limitar trabalho síncrono na borda de slide:
   - evitar rebuild completo no frame crítico.
3. Orçamento de prefetch mais agressivo quando backlog > 0.
4. Evitar varreduras O(n) desnecessárias em estruturas que podem crescer.

---

## 6) Testes de validação

### Soak test padrão

- 60 minutos, telemetria ativada.
- Cenário: corrida contínua sem pausa, múltiplas voltas.

### Métricas de aprovação

1. FPS médio não degradar progressivamente após aquecimento.
2. `LWT2 tx` e `LTX2 tr` sem crescimento monotônico por longas janelas.
3. `LFO1 py` estabilizado.
4. Picos de `LTK1 st` raros e dentro de limite aceitável.

---

## 7) Ordem recomendada de execução

1. Fase A completa (instrumentação).
2. Fase B itens 1 e 2 (contenção imediata).
3. Revalidar soak.
4. Fase B itens 3 e 4.
5. Revalidar soak.
6. Fase C (refatoração do ciclo de vida em ring/pool).

