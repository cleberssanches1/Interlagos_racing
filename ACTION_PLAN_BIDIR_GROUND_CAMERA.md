# Plano de ação — solo 4 rodas, câmera no declive, carro em lod_0 e esteira bidirecional

**Data:** 2026-08-01  
**Objetivo:** Movimento fluido em **ambos os sentidos** da pista (estilo arcade Saturn), com o carro **sempre** nos 2 segmentos densos (lod_0), pitch por **4 contatos de roda**, e câmera que **sobe** no declive sem furar o asfalto.

**Princípio Saturn:** máximo inteligente — slide O(1), sem rebuild de 20 segs no flip; proibir wipe de janela; 1–2 slots por borda.

---

## 1. Sintomas e diagnóstico

| # | Sintoma | Causa provável (código atual) |
|---|---------|-------------------------------|
| A | Asfalto ainda sobrepõe um pouco o carro | Ride height baixo; corpo em Y médio das rodas + pitch visual separado; sort SGL; adesão/snap no declive |
| B | Declive 28–42 melhor, mas inclinação ainda fraca | Pitch do `CarWheelRig` filtra/rate-limita; Y do corpo é média F/R, não plano 4 rodas “rígido”; MapHeight ainda em lajes |
| C | Câmera entra no asfalto no declive | Boom + pitch body-frame incompleto; `ApplyGradeBehindClearance` / clearance fracos quando pitch sobe |
| D | Carro deve ficar no 1º–2º da esteira (lod_0) | Já há intenção `ResolveWindowStartFromCarSegment` (carro no 2º); slide/atraso de observed pode deixar carro em rank ≥2 (malha L) |
| E | **180°: cenário não se constrói no sentido oposto** | Flip de direção só muda `windowDirection_` + target (sem rebuild). Catch-up reverse pode estagnar; janela continua “apontando” para frente da pista; prefetch/bordas pensados no +dir |

**Referência de época (Ridge Racer / Daytona / SEGA Rally):**  
Não reconstrói o mundo inteiro. Mantém **anel de chunks** e **inverte o sentido do anel** (head/tail trocam papel). Slide de 1 segmento por frame; direção vem da **velocidade do carro**, não da câmera sozinha.

---

## 2. Modelo-alvo da esteira bidirecional (leve)

### 2.1 Invariante do carro (lod_0)

```
windowDirection = +1:
  ranks: [0]=atrás  [1]=CARRO  [2..19]=à frente
  start = carSegment - 1

windowDirection = -1:
  ranks: [0]=atrás (sentido de marcha)  [1]=CARRO  [2..19]=à frente
  start = carSegment + 1   (já em ResolveWindowStartFromCarSegment)
```

**Regra:** `logicalRank(car) ∈ {0,1}` sempre — preferir **rank 1** (2º slot) como hoje.  
Se `rank ≥ 2` por 2+ frames → forçar `targetWindowStartId_` e catch-up slides (sem rebuild full).

### 2.2 Flip 180° (sem apagar a pista)

```
1) Detectar reverse (confirmação 2–4 frames):
   - Primário: deslocamento planar do carro · trackForward (anchor→anchor+1)
   - Secundário: câmera (histerese) se quase parado
2) Ao confirmar:
   a) cameraWindowDirection_ = windowDirection_ = novo sentido
   b) targetStart = ResolveWindowStartFromCarSegment(car, dir)
   c) NÃO chamar RebuildActiveSegmentWindow(20)
   d) Catch-up: 1–3 slides/frame no sentido novo até backlog=0
3) Prefetch / bordas LOD usam windowDirection_ (já direction-aware em parte)
4) Só se backlog > metade da pista OU janela inválida:
   rebuild controlado (1x) com flush de texturas e abort se falhar
```

Isso é o que os jogos SEGA faziam: **reorientar o streaming**, não recarregar o CD inteiro.

### 2.3 Design LOD vs sentido

Independente da direção:

| Rank | Malha | Tex |
|------|-------|-----|
| 0–1 | lod_0 high (`TRKRDR`) | 64 |
| 2–9 | lod_1 low (`TRKRDRL`) | 64 |
| 10–19 | low | 32 |

No reverse, “à frente” continua ranks altos — só o mapeamento start/dir inverte.

---

## 3. Solo e pitch (4 rodas)

### 3.1 Estado atual
- Probes FL/FR/RL/RR MapHeight  
- Y corpo ≈ média F/R + ride  
- Pitch visual ≈ (Yf − Yr) filtrado no `CarWheelRig`  
- Declive: snap de topologia ainda pode “degrau”

### 3.2 Alvo arcade (REDRIVER2-style simplificado)

```
1) Cada frame: Yfl,Yfr,Yrl,Yrr = MapHeight (só asfalto)
2) Plano das 4 rodas (ou 3 se 1 faltar):
   - pitch  = atan2(Yf − Yr, wheelbase)   [Y-down: sinais atuais]
   - roll   = atan2(Yr_right − Yl, track)
   - bodyY  = ponto do plano no centro do chassi (+ ride)
3) Adesão: bodyY e pitch/roll com o MESMO target (não Y médio + pitch cosmético só)
4) Declive: step suave de Y (não snap a cada face); rate pitch nariz-baixo moderado
5) Carro em rank 0–1 ⇒ probes batem em faces densas lod_0
```

### 3.3 Sobreposição asfalto × carro (pouca)
1. Aumentar ride/clearance residual (unidades Y-down)  
2. Garantir bodyY do **plano** (não só média)  
3. Sort/depth do carro (já há bias zero) — se preciso, micro-bias **só** no declive  
4. Asset: costuras soldadas no asfalto (já em subdivisão)

---

## 4. Câmera no declive

### 4.1 Estado atual
- Pitch suave do body + `ResolveBoomGuard` + `ApplyGradeBehindClearance`  
- Ainda insuficiente quando o nariz desce forte

### 4.2 Ajuste (leve, sem probe de pista)
```
extraLiftY = f(smoothedCamPitch)   // nariz baixo → mais lift (Y mais negativo)
// ou: cam.Y = min(cam.Y, car.Y - baseClearance - k*|pitch|)
lookTarget sobe levemente com o mesmo pitch
```
Só muls/clamps; **sem** MapHeight na câmera.

---

## 5. Fases de implementação

### F0 — Instrumentação (curta)
- Overlay: `carRank`, `winDir`, `startId`, `designGeo`, `bodyPitch`, `camY−carY`, `topoDrop`  
- Confirmar se no 180° `winDir` vira e se backlog de slide sobe/zera  

### F1 — Esteira bidirecional (prioridade máxima)
1. Direção pela **velocidade do carro** (threshold + confirm 2–4 frames + cooldown)  
2. Flip = **só** dir + targetStart + catch-up slides (nunca rebuild 20 sob estabilização)  
3. Garantir `rank(car) ∈ {0,1}` com correção se atrasar  
4. Prefetch/bordas no sentido de `windowDirection_`  
5. Validar volta inteira em reverse sem sumir cenário  

### F2 — Carro preso ao lod_0
1. Após cada slide, assert rank carro  
2. Se rank ≥ 2: slides prioritários até realinhar (budget 2–3/frame)  
3. Ranks 0–1 sempre `designGeoTier=high` (já planejado)  

### F3 — Plano 4 rodas + adesão declive
1. bodyY do plano (centro) + ride  
2. Pitch/roll do plano → `SetBodyAttitude` (mesmo sinal validado)  
3. Declive: step Y suave; snap só se “voando” (|dY| grande)  
4. Tune ride para zerar overlap residual  

### F4 — Câmera lift por pitch
1. Lift extra ∝ pitch nariz-baixo  
2. Clearance mínimo maior no declive  
3. Validar 28–42 sem câmera dentro da face  

### F5 — Validação
- [ ] Ida e volta 180°: esteira reconstrói por slides  
- [ ] Carro sempre em 1º–2º slot (lod_0)  
- [ ] Declive 28–42: 4 rodas “coladas”, pitch natural  
- [ ] Câmera acima do asfalto no declive  
- [ ] FPS estável (sem rebuild full no flip)  

---

## 6. O que **não** fazer (Saturn)

| Evitar | Por quê |
|--------|---------|
| `RebuildActiveSegmentWindow(20)` a cada 180° | Já apagou o cenário sob HWR |
| Raycast MapHeight na câmera | Orçamento / histórico de crash |
| 4× FindSurfaceY extras por frame | Preferir reusar os 4 cantos já amostrados |
| Dual rebuild de pack no flip | Slide + dir basta |

---

## 7. Arquivos prováveis

| Área | Arquivos |
|------|----------|
| Bidirecional | `track_system.cxx` (`UpdateCameraDrivenWindowDirection`, `UpdateActiveSegmentWindowForPosition`, `BeginFrame` catch-up) |
| Rank carro | `ResolveWindowStartFromCarSegment`, tracking `observedCarSegmentId_` |
| Solo/pitch | `car_ground_follower.hpp`, `car_wheel_rig.*`, `car_physics_shared.hpp` |
| Câmera | `camera_system.cxx`, `camera_safety.hpp` |
| Telemetria | overlay debug rows |

---

## 8. Ordem sugerida e esforço

| Fase | Foco | Esforço |
|------|------|---------|
| **F1** | Reverse 180° funcional e leve | 1–2 sessões |
| **F2** | Carro sempre ranks 0–1 | acoplado a F1 |
| **F3** | Plano 4 rodas + declive | 1 sessão |
| **F4** | Câmera lift | curto |
| **F5** | Validação em pista real | usuário |

---

## 9. Critério de sucesso (bidirecional)

> Virar 180°, acelerar no sentido contrário: em ≤ ~20 frames a janela aponta para a frente da marcha, segmentos entram por **slide**, sem tela preta, e o carro permanece nos **dois primeiros** slots densos.

---

## 10. Status

- [x] Análise da lógica atual  
- [x] Plano escrito  
- [x] F1–F2 bidirecional (Travel/Enforce) **removidos**  
- [x] Estado restaurado = **3 LOD + dual RDR/design geo (pré-F1)**, não o HEAD limpo  
- [x] Tentativa de “blank fix” (DualFlag) e `git checkout HEAD` **desfeitos** como excesso de revert  
- Solo/câmera (F3/F4 em `car_*` / `camera_*`) mantidos  
- [ ] Bidirecional: retomar só com modelo de anel (§11)  

---

## 11. Por que não avançamos no reverse (lição)

### O que quebrou a renderização
1. **Dual mesh (TRKRDR + TRKRDRL)** com rebuild **in-place** no renderer vivo: falha parcial → slot vazio; re-fila por frame → janela inteira preta (só o carro).  
2. **Flip de direção + retarget agressivo** (`EnforceCarWindowLod0Rank`, target a meia pista): `windowDirection_` inverte, mas o conteúdo físico dos 20 slots ainda é o da direção antiga; backlog ≥ half é **zerado** → esteira trava inconsistente.  
3. Misturar “direção da câmera”, “velocidade do carro” e “start = car±1” no mesmo frame sem **invariante de IDs nos slots**.

### Por que o reverse é difícil neste engine
| Peça | Realidade Saturn / este código |
|------|--------------------------------|
| Janela | 20 slots **fixos**; slide O(1) assume ordem **seqüencial** start→start±1→… |
| Flip 180° | Não basta `dir = -1`. Os IDs nos slots têm de continuar um **arco contíguo** no novo sentido. |
| Catch-up | Se `\|target−start\| ≥ total/2`, backlog vira 0 → **não** reconstrói. |
| Rebuild(20) | Já apagou a pista sob HWR; proibido no flip. |
| Dual RDR | Segundo pack + swap live = risco de blank; só com scratch + commit atômico. |
| Câmera vs carro | Câmera pode olhar “contra” a numeração da pista sem o carro ter invertido viagem. |

### Modelo mínimo que ainda não foi implementado (SEGA/RR)
```
// Anel: slots[0..19] = IDs contíguos ao longo de dir
// Flip confirmado (2–4 frames, velocidade do carro):
//   1) dir = -dir
//   2) reinterpretar head: rank0 = “atrás da marcha”, rank1 = carro
//   3) start = ResolveWindowStartFromCarSegment(car, dir)
//   4) NÃO esvaziar slots; se o conjunto de IDs ainda for o arco certo
//      (mesmos 20 segs, ordem espelhada), só inverter logical rank map
//   5) se o arco estiver errado: slides 1/frame até cobrir, NUNCA wipe
```
Hoje o código no HEAD já faz flip **por câmera** com target+slide sob estabilização — isso é o caminho “seguro”. Reverse **fluido** exige provar invariante de cobertura de IDs em instrumentação **antes** de dual-mesh ou enforce rank.

### Próximo passo (quando retomar)
1. Overlay: `start`, `dir`, `carId`, `rank`, lista de 20 IDs, `backlog` — validar ida/volta **sem** dual mesh.  
2. Só então: espelho de ranks no flip (sem rebuild).  
3. Dual mesh só com build em scratch + swap de ponteiro se parse OK.
