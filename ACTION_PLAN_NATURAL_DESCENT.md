# Plano de ação — descida natural e acompanhamento de topologia

**Data:** 2026-07-22  
**Objetivo:** Quando a pista muda (início de declive, costura de segmento, rampa íngreme), o carro **desce e inclina com o asfalto** de forma estável — sem empinada/snap-back em aclives e sem “ficar reto no ar” em declives.

**Referências:** REDRIVER2 (`MapHeight` + 4 rodas + normal), estado atual do Interlagos (`car_ground_follower`, `car_wheel_rig`, `track_system` FindSurfaceY).

---

## 1. Sintomas observados

| Cenário | Comportamento atual | Esperado |
|---------|---------------------|----------|
| **Declive** | Não acompanha a geometria das faces; atraso ou fica alto | Assim que a topologia desce, corpo e pitch **seguem** o asfalto |
| **Aclive íngreme** | Às vezes **empina a frente** e **volta ao normal** | Nariz sobe de forma estável; sem oscilação/snap |
| **Mudança de topologia** | Detecção fraca / lag no Y | Detectar mudança (ΔY eixos, segmento, grade) e **descer prioritariamente** |

---

## 2. Diagnóstico (código atual)

### 2.1 Y do corpo separado do pitch visual
- **Y:** média/corda das rodas + `ApplyVerticalAdhesion` (snap 1.0, step 2.0 simétrico).
- **Pitch/roll:** só no `CarWheelRig` → `SetBodyAttitude` (cosmético).
- Consequência: o carro pode **inclinar** sem **descer o suficiente**, ou descer devagar enquanto o pitch reage — sensação de “não acompanha a face”.

### 2.2 Adesão simétrica atrasa a descida
```text
kMaxYStepDownPerFrame = 2.0   // igual ao up
kSnapDownThreshold    = 1.0
```
Em costura de declive o erro em Y costuma ser **> 1** e o corpo só aproxima **2 unidades/frame**. Em rampa longa o asfalto “escapa” sob as rodas até o pitch oscilar.

### 2.3 Pitch instável em aclive íngreme
Causas prováveis em conjunto:
1. **Probe frontal** colhe face mais alta (ou outro segmento) um frame e platô no seguinte → `Yf−Yr` inverte → **empinada**.
2. **Filtro 1/2 + hold** (`heldPitchDegX16_`) atrasa e depois **solta** de uma vez.
3. **Top-surface ranking** (`min yRaw`) em multi-face: no aclive pode alternar entre faces “topo” próximas e gerar salto de sample.
4. Clamp de `rideTarget` a `2× maxStep` **limita o target** antes da adesão → o corpo **nunca** pede a descida completa de uma vez.

### 2.4 Sem evento explícito de “topologia mudou”
Não há flag do tipo:
```text
topologyDrop = (surfaceYTarget − surfaceYFiltered) > limiar  // Y-down: target > cur = descer
```
Tudo é continuous follow genérico — sem **prioridade de descida** nem **amortecimento de subida** (anti-empino).

### 2.5 O que já está bom (não desfazer)
- 4 cantos MapHeight + top surface (não afundar sob o asfalto em multi-nível).
- Pitch/roll a partir de F/R e L/R (não só CM).
- XZ livre sem suporte.
- Cache + seed+1 (FPS).
- Sinais de pitch/roll validados pelo usuário (`kBodyPitchSign/RollSign = +1`).

---

## 3. Modelo-alvo: “descida natural”

Inspiração arcade (REDRIVER2 simplificado, sem rigid-body completo):

```
1) Cada frame: Yf, Yr (e opcionalmente 4 cantos) = MapHeight estável
2) grade = (Yf − Yr) / L
3) bodyTargetY = (Yf + Yr)/2 + ride     // ou avg 4 rodas
4) SE topologia DESCE (targetY > bodyY + ε):   // Y-down
      descer RÁPIDO (snap ou step grande)
   SE topologia SOBE (targetY < bodyY − ε):
      subir DEVAGAR (step menor) → evita empino/pop
5) pitch = f(Yf − Yr) filtrado com deadzone e rate-limit assimétrico
   (subida de nariz mais lenta que descida de nariz)
```

**Regra de ouro:**  
**Descer e “colar” no asfalto é prioritário; subir o chassis é conservador.**

---

## 4. Fases de implementação

### F0 — Instrumentação (1–2 h) — validar hipóteses

**Objetivo:** Ver no overlay se o bug é sample, adesão ou pitch.

| Telemetria | Significado |
|------------|-------------|
| `yF yR yT yCur` | Eixos, target, Y filtrado do corpo |
| `dY = yT − yCur` | >0 = precisa **descer** (Y-down) |
| `grade` / `pitchDeg` | Corda e atitude aplicada |
| `mask` FL/FR/RL/RR | Quais cantos acertaram |
| `seg` seed | Mudança de segmento |

**Arquivos:** `game_loop_overlay_debug_presenter_ops.hpp`, campos já em `GameplayFrameState` / `RuntimeDebug`.

**Critério de saída F0:** Em declive, `dY` fica positivo por vários frames e `yF > yR`; no empino, `pitchDeg` ou `yF` salta e volta.

---

### F1 — Adesão assimétrica + prioridade de descida (núcleo)

**Arquivos:** `car_physics_shared.hpp`, `car_ground_follower.hpp` (`ApplyVerticalAdhesion` + clamp de `rideTarget`).

| Parâmetro | Hoje | Proposto |
|-----------|------|----------|
| `kMaxYStepDownPerFrame` | 2.0 | **3.5–4.0** (descer com a face) |
| `kMaxYStepUpPerFrame` | 2.0 | **1.0–1.25** (anti-empino / anti-pop) |
| `kSnapDownThreshold` | 1.0 | **2.0–2.5** (cola rápida em declive) |
| `kSnapUpThreshold` | 1.0 | **0.5–0.75** (sobe só se erro pequeno) |

**Lógica:**
```text
d = target − current   // Y-down: d>0 = descer
if d > 0:  // DESCIDA
  if d <= snapDown: full snap
  else: step = min(d, maxDown)   // generoso
else:      // SUBIDA (d<0)
  if |d| <= snapUp: full snap
  else: step = max(d, -maxUp)    // conservador
```

**Remover ou alargar** o clamp de `rideTarget` a `2× maxStep` na descida (hoje impede o target de refletir a face real).

**Critério de saída F1:** No início do declive, o corpo **desce em ≤ ~3–5 frames** até o ride height; sem flutuação longa.

---

### F2 — Detector de mudança de topologia

**Arquivos:** `GroundState` + `ProbeSurfaceTarget` / `ApplyVerticalAdhesion`.

Estado:
```cpp
int32_t lastSurfaceYRaw;      // último target aceito
int16_t lastGradeTanX100;     // ou gradeTanRaw
int16_t lastSegId;
uint8_t topologyDropFrames;   // janela de “modo descida”
```

**Disparos de “topology drop”** (qualquer um):
1. `targetY − filteredY > kTopoDropY` (ex.: 0.75–1.0)  
2. `|grade| aumenta` e sinal de declive (`Yf > Yr`)  
3. `segmentId` mudou e `targetY > lastY + ε`  
4. Média dos 4 cantos desceu vs frame anterior  

**Enquanto `topologyDropFrames > 0`:**
- Forçar path de adesão de **descida rápida** (F1).
- Opcional: um frame de **snap** se drop > limiar forte (costura).

**Critério de saída F2:** Entrar no declive gera flag de topologia e o Y reage no mesmo trecho (não só no fim do segmento).

---

### F3 — Pitch estável (anti-empino / anti-snap-back)

**Arquivos:** `car_wheel_rig.cxx` / `.hpp`.

1. **Rate-limit assimétrico no pitch:**
   - Nariz **descendo** (declive): step/frame maior (ex. 8–10°).
   - Nariz **subindo** (aclive): step/frame menor (ex. 3–4°) → evita empinada violenta.
2. **Não reiniciar filtro** se um canto falhar 1–2 frames (já parcialmente com `heldPitch`).
3. **Hold de grade:** se `|Yf−Yr|` oscilar > limiar em 1 frame, clamp jump (já existe `kMaxDeltaJumpRaw`) — reduzir se ainda oscilar.
4. **Aclive íngreme:** se `Yf < Yr` (frente mais alta) e `|ΔY|` grande, limitar `targetPitch` a um máximo de subida por frame **e** não deixar o body Y “pular” para o probe frontal sozinho (body = média, não min dos cantos).

**Critério de saída F3:** Em aclive íngreme, pitch sobe de forma monotônica ou com pouca oscilação; sem “empina e volta”.

---

### F4 — Coerência Y + pitch (opcional, alto impacto visual)

Hoje pitch é só visual; Y é física.

**Melhoria barata:** após adesão, se grade válida e `topologyDrop`:
```text
// Pequeno bias: bodyY tende a (Yr + ride) em declive forte
// para o eixo traseiro “ancorar” e o pitch baixar o nariz no asfalto
// NÃO usar min(Yf,Yr) cego (afunda / empina).
```

Ou (fase posterior): aplicar a mesma corda F/R no cálculo de `surfaceYTarget` que o wheel_rig usa (mesmos raw filtrados) — **uma única fonte de verdade**.

---

### F5 — Performance (manter SH2)

| Item | Política |
|------|----------|
| Probes | Manter **4 cantos** se FPS ok; se apertar, **2 eixos** (F/R) e roll só por L/R a cada 2 frames |
| FindSurfaceY | Cache + seed+1 (já feito) |
| Topologia | Só inteiros / comparações, zero queries extras |
| Overlay F0 | Desligar em release se pesar |

---

## 5. Ordem de trabalho sugerida

```
F0 telemetria  →  F1 adesão assimétrica  →  F2 detector topologia
       →  F3 pitch assimétrico  →  F4 (se ainda falhar visual)  →  F5 só se FPS cair
```

**Não** reabrir ranking “face mais baixa” (afunda em subidas).  
**Não** projetar velocidade planar (já matou subidas no passado).

---

## 6. Arquivos a tocar

| Arquivo | Mudança |
|---------|---------|
| `src/car_physics_shared.hpp` | Tunables down/up assimétricos; limiares topologia |
| `src/car_ground_follower.hpp` | Adesão assimétrica; detector drop; clamp rideTarget |
| `src/car_wheel_rig.hpp/.cxx` | Rate-limit pitch assimétrico; hold estável |
| `src/game_loop_overlay_debug_presenter_ops.hpp` | Overlay F0 (`dY`, grade, pitch) |
| `ACTION_PLAN_NATURAL_DESCENT.md` | Este documento |

---

## 7. Critérios de aceite (jogo)

1. **Declive (S do Senna / início de curva):** ao entrar na rampa, o carro **desce e o nariz acompanha** as faces em no máximo poucos frames; parado no declive, **permanece inclinado** e colado.
2. **Aclive íngreme:** sobe sem **empinar e voltar**; pitch estável.
3. **Costura de segmento:** sem degrau longo no ar; sem afundar sob o asfalto.
4. **FPS:** sem regressão grave vs baseline com 4 probes + cache.

---

## 8. Riscos e mitigação

| Risco | Mitigação |
|-------|-----------|
| Descida rápida perfura face | Manter top-surface ranking; maxDown alto mas não infinito; ride height |
| Empino por sample frontal errado | maxUp baixo + pitch rate-limit na subida do nariz |
| Oscilação F/R | Hold + clamp jump; não fallback body Y nos eixos |
| FPS | 2 probes se necessário; topologia só em CPU barata |

---

## 9. Status — **implementado (F0–F3)**

| Fase | Status |
|------|--------|
| F0 Overlay `nd yF/yR/yB/yT`, `dY`, `g`, `td`, `p` | feito |
| F1 Adesão assimétrica (down 3.75 / up 1.125, snap 2.25 / 0.625) | feito |
| F2 `topologyDropFrames` + hard snap em drop forte | feito |
| F3 Pitch: step down 10° / up 4° (anti-empino) | feito |

### Validar
1. Declive parado no início da curva — `dY>0`, `td=1`, corpo desce, pitch estável  
2. Aclive íngreme — sem empinar e voltar  
3. Transição plana → rampa  

Não reabrir ranking “face mais baixa”.
