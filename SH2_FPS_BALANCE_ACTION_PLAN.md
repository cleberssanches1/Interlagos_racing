# Plano de Ação — Balanceamento Dual SH2 para Ganho de FPS

**Projeto:** Interlagos_racing  
**Branch de referência:** `feature/sh2`  
**Atualizado:** 2026-07-14 (pós-pesquisa web + início de execução)  
**Objetivo:** reduzir **latência de frame** com Master ∥ Slave (não só offload com spin).

---

## Status de execução

| Fase | Estado | Notas |
|------|--------|-------|
| 0 Baseline | **ok** | Boot estável após rollback da regressão |
| 1 Instrumentação | adiado | sem strings novas (política remove-first / ISO) |
| 2.1 Barrier track off | **aplicado + OK emulador** | `kEnableTrackSlaveBarrierLockstep = false` |
| 2.2 Tunables safe mode | pendente | só se timeouts/safe mode subirem |
| 3 Overlap Master+lista N−1 | **aplicado + OK** | `FinalizeIfReady()` no `BeginFrame` |
| 3b Shift telemetry V2 | **aplicado** | copy `carShift*` em `car_physics_v2.hpp` |
| 4 Sim async | **revertido** | `false` travava ao acelerar (Yabause) |
| 4b Split estável | **aplicado + OK** | Sim **Master**; Slave **só track** async |
| 4c Exclusive Slave job | **aplicado** | Com barrier async: depth-sort na **Master**; producer na **Slave** (evita clobber `slSlaveFunc`) |
| 5–6 Scheduler / jobs extra | bloqueado | ordem obrigatória |

### 2026-07-14 — Boot regression post-mortem

Doc de política: `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md`

- SH2 opcodes são **sempre 16-bit** (largura fixa); o risco real é **quantas** instruções o compilador emite, stack e layout do binário.
- Sintomas listados na política: *silent close on startup*, *invalid opcode*, ISO `+2048`.
- Patches aditivos em headers críticos (`car_audio_system.hpp`, `track_draw_producer.hpp`, barrier async, overlay) violaram “remove-first” e correlacionaram com close no emulador.
- **Ação:** `git restore` de todos os fontes runtime alterados; reaplicado **apenas** 3 assigns de telemetria de marcha em `car_physics_v2.hpp` (sem mudança de áudio/SCSP/SH2 schedule).

**Rollback imediato da Fase 2.1:** em `src/track_system.cxx` repor  
`kEnableTrackSlaveBarrierLockstep = true`.

---

## 0. Lições da pesquisa (ajuste do plano)

Fontes: Dual CPU Guide ST-202, SGL/`slSlaveFunc`, SegaXtreme HOWTO 2cpu (RockinB), análise de catálogo (~56% usam Slave em 3D/FMV), relatos de dev (Suzuki/VF, Neversoft 1995), contenção de bus Master/Slave.

| Achado | Impacto no plano |
|--------|------------------|
| Não é dual-core SMP; bus compartilhado | Não esperar ~2× FPS; medir contenção via waits |
| `slSlaveFunc` = **um** job até done | Priorizar Master ∥ 1 job Slave, nunca 2 jobs Slave |
| Spin/wait anula o dual-CPU (Suzuki: “one waits for the other”) | **ROI #1 = matar barriers**, não adicionar jobs |
| SGL pode usar Slave no “buraco” de VBlank (`slPutPolygonS`) | Overlap com idle do Master, não só rebalance teórico |
| Dados disjuntos + cache-through/invalidate | Double-buffer já usado; manter publish só após `IsDone` |
| 3rd parties muitas vezes só usavam Master | Já estamos além; foco em **latência**, não “ligar Slave de novo” |
| Balanceamento de carga é o hard problem | Fases 0–1 obrigatórias antes de sim async |

**Ordem de ROI (ajustada, imutável nesta execução):**

1. Medir waits (overlay)  
2. **Track async** (barrier off) — prep visual N−1, risco médio  
3. Overlap Master com lista pronta  
4. Só depois sim async (feeling/câmera)  
5. Jobs extras só se telemetria mandar  

**Proibido nesta onda:** async sim + async track no mesmo patch; mover submit VDP1/input/PCM; reabrir refactor passivo sem meta de FPS.

---

## 1. Diagnóstico do código (estado real)

### 1.1 O que já roda em cada SH2

| Bloco | Onde | Como | Bloqueia Master? |
|-------|------|------|------------------|
| Orquestração, input, câmera, HUD, submit VDP1, áudio driver | Master | Sempre | — |
| Gameplay + física + eventos de sim | Slave (`SimulationTask`) | lockstep **ON** | **Sim** (spin) |
| Producer draw list pista | Slave | `useSlave` | **Não** após 2.1 (era Sim com barrier) |
| Depth sort pista | Slave | on | **Não** após 2.1 |
| Car prepare | Slave | **OFF** | N/A |
| Fallback síncrono | Master | safe mode / dispatch fail | Sim |

### 1.2 Fluxo (antes → depois da 2.1)

**Antes (serial com spins):**
```
sim lockstep wait → ... → track barrier wait → submit → sync
```

**Alvo após 2.1 (ainda com sim lockstep):**
```
sim lockstep wait → ... → track dispatch (no wait) → submit lista N ou N−1 → sync
                         └─ Slave pode terminar producer no frame seguinte
```

### 1.3 Gargalo dominante

Offload existe; **overlap útil** era nulo nas duas barreiras. Após 2.1 o wait de **track** deve cair; o wait de **sim** permanece até Fase 4.

### 1.4 Restrições (hardware + projeto)

1. Cache: publish só após `IsDone`; buffers double / cache-through onde já usado.  
2. Um `ExecuteOnSlave` ativo.  
3. Master: VDP1 submit, `Synchronize`, input, PCM driver.  
4. ISO pin documentado **4134912** (não regredir footprint de CD).  
5. Micro-passo + fallback síncrono / safe mode sempre vivos.

---

## 2. Meta de sucesso

| Critério | Alvo | Aceitável |
|----------|------|-----------|
| FPS estável (20 segs) | **≥ 30** | ≥ 27 |
| FPS no slide | **≥ 25** | ≥ 22 |
| `simMasterWaitTicks` vs baseline (após Fase 4) | **−70%** | −40% |
| Track barrier no hot path (após 2.1) | **off** (`lk:0`) | só debug |
| Timeouts / safe mode track | ~0 em corrida longa | recovery OK |
| Visual | sem buracos/flicker de lista | reuse N−1 ok |
| LWR/HWR | plateau | = baseline |

### Overlay a anotar no emulador (Fase 0/1)

| Linha (aprox.) | Conteúdo |
|----------------|----------|
| FPS overlay | fps / frame ms |
| `S2 M%/S% mb:sw` | busy Master vs Slave (ticks) |
| `S2 sim: wait: % d: b:` | sim Slave, **Master wait**, wait% da sim, dispatch, skip track busy |
| `S2 t p:s pr:so` | producer in-flight, safe mode, ticks producer/sort |
| `TRK sh2 ... lk:` | `lk:0` confirma barrier off |

**Não** comparar timestamps FRT absolutos entre CPUs (FRT é local).

---

## 3. Arquitetura-alvo (end state)

**Master:** input, consumir snapshots, câmera, HUD, submit track/car, PCM, `Synchronize`.  
**Slave (1 job):** (A) sim · (B) producer+sort · (C futuro) car visual prep.

```
Frame N:
  Master consome sim/lista prontas (N ou N−1)
  Master dispara 1 job Slave quando slot livre
  Master trabalha em paralelo (submit, HUD, etc.)
  Sem spin longo no hot path
```

---

## 4. Fases (ordem obrigatória)

### Fase 0+1 — Baseline + instrumentação
- Overlay SH2 com labels de wait legíveis  
- Rodar 2 voltas no Mednafen/Kronos e anotar reta/curva/slide  
- **Saída:** números B0 com `lk:1` (se ainda tiver build antigo) ou B1 com `lk:0`

### Fase 2 — Track async (ROI #1)
1. `kEnableTrackSlaveBarrierLockstep = false`  
2. `SetBlockUntilDone(false)` no producer e sorter (já amarrado à flag)  
3. Manter reuse / safe mode / maxFramesInFlight  
4. **Não** mudar sim lockstep  

**Aceite:** FPS sobe ou track Master wait cai; `timeoutFallbacks` controlado; visual OK.

### Fase 3 — Overlap com lista N−1
- Submit com lista pronta; disparar próximo job sem wait  
- Não iniciar sim se `producer.jobInFlight` (regra já existe)

### Fase 4 — Sim async (alto risco)
- Só com 2–3 estáveis  
- Default `slaveSimulationLockstep=false`  
- Reuse estado commitado; hard-wait fora do hot path  
- Cuidado: slide de janela vs sim in-flight + cache

### Fase 5–6 — Scheduler + jobs medidos
- Prioridades: consumir → track stale → sim → car prep  
- Novos jobs só se Master ainda saturar com waits baixos

---

## 5. Checklist de patches

| # | Patch | Estado |
|---|-------|--------|
| 0.1/1.1 | Overlay SH2 waits explícitos | **este commit de trabalho** |
| 2.1 | Barrier track = false | **este commit de trabalho** |
| 2.2 | Tunar safe mode se timeouts | pendente |
| 3.1 | Overlap submit N−1 | pendente |
| 4.1 | Sim lockstep off | pendente |
| 5+ | Scheduler / jobs | pendente |

Validação por patch: build limpo → ISO se aplicável → emulador 2 voltas → comparar overlay.

---

## 6. Matriz de distribuição (onda atual)

| Sistema | Agora (pós 2.1) | Alvo final |
|---------|-----------------|------------|
| Input / HUD / submit / sync / PCM | M | M |
| Sim gameplay+física | S **wait M** | S async |
| Track producer/sort | **S async** | S async |
| Track submit | M (lista N ou N−1) | M (N−1 ok) |
| Car prepare visual | off | S se medido |

---

## 7. Riscos (Fase 2.1)

| Risco | Sintoma | Ação |
|-------|---------|------|
| Lista atrasada | pop-in / buraco | confiar em reuse; se falhar → safe mode; rollback `lk=true` |
| Timeouts | `s:1` safe frequente | subir `maxFramesInFlight` ou baixar carga; 2.2 |
| Bus contention | FPS não sobe | anotar; não empilhar jobs |
| ISO/boot | não sobe | rebuild limpo; diff mínimo |

---

## 8. Definição de “feito” (programa completo)

1. Master sem spin longo no hot path (sim nem track)  
2. FPS na meta §2  
3. Slave útil na maior parte dos frames com Master ocupado em paralelo  
4. Fallbacks raros e vivos  
5. Baseline + números pós-otimização no mesmo percurso  
6. Flags de rollback no código  

---

## 9. Próximo passo humano (emulador)

Após o build desta onda:

1. Confirmar overlay `lk:0` / `TRK sh2 ... lk:0`  
2. Anotar FPS + `S2 sim/wait` + producer in-flight em reta e slide  
3. Se estável e com ganho (ou wait de track morto) → seguir Fase 3  
4. Se visual ruim → rollback barrier e reportar sintoma  
