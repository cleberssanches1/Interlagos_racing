# Plano de ação — câmera chase com pitch no declive (altura + inclinação)

**Data:** 2026-07-24  
**Objetivo:** No declive, a câmera **não entra nas faces do solo** e **inclina junto com o carro** (attitude follow), de forma estável e barata no Saturn (sem probes de terreno na câmera).

**Relacionado:** `ACTION_PLAN_CAMERA_GRADE_REVERSE_STREAM.md` (já elevou Y parcialmente; **não** inclinou a câmera com o corpo).

---

## 1. Sintomas (relato atual)

| Sintoma | Efeito visual |
|---------|----------------|
| Câmera **muito baixa** no declive | Boom “enterra” no asfalto; faces do chão cobrem a tela (Z-sort / coplanar) |
| Câmera **não inclina** com o carro | Carro pitcha (wheel plane OK); câmera permanece “nível mundo” → olhar rasante no solo à frente / atrás |

---

## 2. Pesquisa — o que jogos de corrida fazem

### 2.1 Padrão arcade / third-person chase

Referências típicas (Ridge Racer / GT-like chase, devlogs de chase em loops, discussões de terrain auto-incline):

1. **Offset no espaço do carro (body frame), não no mundo plano**  
   Posição = `carPos + R(yaw, pitch[, roll]) * localOffset(behind, up, side)`.  
   No declive, o vetor “atrás + cima” **acompanha o pitch**: a câmera sobe/desce com a traseira e o “up” deixa de ser o eixo Y mundo.

2. **Pitch de look alinhado (ou semi-alinhado) ao pitch do veículo**  
   Look-at não é só “ponto à frente no plano XZ”. O alvo tem componente vertical coerente com a rampa, ou o look vector usa o forward do corpo (pitchado).

3. **Suavização assimétrica / lag controlado**  
   Attitude da câmera **interpola** em direção ao pitch do carro (spring/lerp), não copia 100% a cada frame (evita trepidação em costuras). Em loops/bancos extremos o follow de pitch/roll é quase rígido; em rampas leves, fraction 0.5–0.9.

4. **Clearance vertical separado do pitch**  
   Mesmo com pitch correto, mantém **altura mínima acima do asfalto / acima do carro** (boom guard). Subir só o Y mundo sem pitch **não basta**: o offset “atrás” em XZ fica “dentro” da face inclinada.

5. **Problema clássico se a câmera NÃO inclina**  
   Em aclive: câmera “olha para o chão” e não vê a pista à frente (relatos de auto-incline quebrado / panning down).  
   Em declive: câmera fica alta no mundo relativo à traseira ou **baixa demais** na face se o boom for só offsetY mundo.

### 2.2 O que **não** fazer no Saturn (histórico local)

- Probes de MapHeight na posição da câmera / look.Y por raycast a cada frame → já **fecharam o emulador** / orçamento SH2.  
- Solução obrigatória: usar **attitude já calculada no carro** (`bodyPitchDeg`, `gradeTan`, opcionalmente roll) — custo ~0.

### 2.3 Modelo-alvo (resumo)

```
localOffset = (side, up, behind)     // preset chase
R = RotYaw(carYaw) * RotPitch(camPitch) [* RotRoll(camRoll)]
camPos = carPos + R * localOffset
lookDir = R * forwardLocal  (ou carPos + R * lookLocal)
camPitch = Smooth(camPitch, carPitch * kFollowGain)
// Y-down: "up" no local offset já é negativo em Y mundo quando pitch=0
// Boom guard: cam.Y nunca "abaixo" do asfalto virtual (carY - minClearance)
```

---

## 3. Diagnóstico no Interlagos (código atual)

### 3.1 O que já existe
- `SetRoadAttitude(gradeTanX100, bodyPitchDeg)` alimentado no `ResolveCameraFrameState`.
- `ResolvePresetOffsetWorld`: lift de **Y mundo** por grade/pitch (cap ~28+16); clamp yMin chase ~−96.
- Look: `lookYUnits` sobe um pouco no declive; look-ahead **só XZ** (`forward.Y = 0`).
- `CameraSafety::ResolveBoomGuard`: clearance mínimo vs **carY** (não vs face real).

### 3.2 Por que ainda entra no solo
1. **Offset é planar + Y fixo no mundo** — o vetor “atrás do carro” **não gira com o pitch**. No declive, “atrás” em XZ fica **mais baixo** em altitude relativa à rampa → boom colide com a face.
2. Lift por grade é **insuficiente / saturado** e não substitui rotação.
3. **Look horizontal** (Y=0 no forward) + carro pitchado → ângulo de visão “corta” o asfalto.
4. Boom guard só compara com `carY`, não com a geometria da rampa atrás do carro.

### 3.3 Gap principal
| Peça | Estado |
|------|--------|
| Elevação Y por grade | Parcial (F1 anterior) |
| **Rotação de offset por pitch do carro** | **Ausente** |
| **Look pitch / forward 3D** | **Ausente** (só XZ) |
| Smooth pitch da câmera | Ausente |
| Roll da estrada (opcional) | Ausente (fase 2) |

---

## 4. Objetivos de implementação

1. No declive, câmera **acima das faces** (sem enter no asfalto).  
2. Quando o carro **inclina**, a câmera **inclina** (pitch follow), com lag suave.  
3. Aclive: não “olhar só para o chão”; manter horizonte de pista legível.  
4. Zero probes de terreno na câmera; FPS estável.  
5. CAM1 (1P) / CAM2 / CAM3 com gains por preset (1P mais rígido; far mais suave).

---

## 5. Fases de implementação

### F0 — Instrumentação (30–60 min)
- Overlay debug: `camPitch`, `carPitch`, `grade`, `camY−carY`, preset.
- Confirmar sinais Y-down e `bodyPitchDeg` (nariz baixo = positivo após `kBodyPitchSign`).

### F1 — Pitch follow na posição (core) — **prioridade**
**Arquivos:** `camera_system.hpp` / `.cxx`

1. Manter estado `smoothedCamPitchDeg_` (e opcional `smoothedCamRollDeg_`).
2. Cada frame:
   ```
   targetPitch = bodyPitchDeg * kPitchFollowGain[preset]   // ex. CAM2: 0.85–1.0
   smoothedCamPitch = Lerp(smoothed, target, kPitchBlend) // ~0.35–0.55
   clamp ±kMaxCamPitch (ex. ±28°)
   ```
3. Reescrever `ResolvePresetOffsetWorld` (ou novo `ResolveChaseOffsetWorld`):
   - Montar offset local `(x, y, z)` do preset (z negativo = atrás).
   - Aplicar **pitch** no plano longitudinal (rotação em torno do eixo right do carro):
     ```
     // Y-down: pitch positivo (nariz baixo) rotaciona "atrás+up" de forma
     // que o boom sobe em altitude ao descer a rampa (validar sinal em runtime).
     off' = RotPitch(smoothedCamPitch) * offLocal
     world = RotYaw(heading) * off'
     ```
   - **Não** depender só de `offsetYUnits -= gradeLift` (manter lift residual pequeno como safety).

4. Validar: no declive, distância vertical câmera→asfalto aumenta; no plano, framing igual ao atual.

### F2 — Look / “inclinar a câmera” de verdade
1. Look-ahead em **3D** no corpo:
   ```
   lookLocal = (0, lookHeight, lookAhead)
   lookWorld = carPos + RotYaw * RotPitch(smoothedCamPitch) * lookLocal
   ```
   ou: `lookTarget = camPos + RotYaw*RotPitch * forwardUnit * dist`.
2. Opcional: `state_.viewPitchDeg` / matrix de view se o pipeline SGL usar pitch de view separado do look-at — alinhar ao que `RenderPipeline` / `slLookAt` espera hoje (só look-at ponto).
3. Em declive: look ligeiramente “para cima” da face (já parcialmente no lookHeight; unificar com F1).

### F3 — Clearance e anti-clip
1. Aumentar baseline de height chase (CAM2/CAM3) se ainda raspar em rampa suave.
2. Boom guard reforçado:  
   `minClearance = base + k * |smoothedCamPitch|` (mais pitch → mais folga).
3. **Proibido** nesta fase: raycast/MapHeight na câmera.
4. Se ainda clipar em rampas extremas: extrapolar Y do carro com `gradeTan * behindDistance` (custo 1 mul, sem probe):
   ```
   // Y-down: atrás na rampa de declive → asfalto mais alto (Y menor)
   estimatedGroundYBehind = carY - gradeTan * |behind|
   camY = min(camY, estimatedGroundYBehind - clearance)  // Y-down: min = mais alto
   ```

### F4 — Polimento por preset e antijitter
| Preset | Pitch follow gain | Pitch blend | Extra height |
|--------|-------------------|-------------|--------------|
| FirstPerson | 1.0 | alto (quase rígido) | já alto (cockpit) |
| ChaseNear | 0.9 | médio | +base se preciso |
| ChaseFar | 0.7–0.85 | mais lento | mais height |

- Deadzone de pitch (~1–2°) para não tremer em reta.
- Rate-limit de pitch da câmera (espelhar assimetria do carro se necessário).

### F5 — Validação
- [ ] Declive médio: câmera **acima** do asfalto; sem faces cobrindo o carro.
- [ ] Declive íngreme: pitch da câmera **acompanha** o do carro (visual “colado”).
- [ ] Aclive: horizonte de pista à frente legível (não só capô/chão).
- [ ] Reta: sem oscilação de pitch.
- [ ] FPS sem regressão (só muls trig/fix).

---

## 6. Detalhe de API sugerida

```cpp
// camera_system.hpp
void SetRoadAttitude(int16_t gradeTanX100, int16_t bodyPitchDeg); // já existe
// Interno:
//   int16_t smoothedCamPitchDeg_ = 0;
//   int16_t smoothedCamRollDeg_ = 0;  // F2+ se houver body roll
// Tunables por preset em ChasePresetConfig:
//   int16_t pitchFollowX100;  // 100 = 1.0
//   int16_t pitchBlendRaw;    // 16.16
//   int16_t minBoomClearance;
```

**Sinais (Y-down + bodyPitch atual):**
- `bodyPitchDeg > 0` ≈ nariz baixo ≈ declive sob o carro.  
- Validar no primeiro build se `RotPitch(+)` eleva ou afunda o boom; **um flip de sinal** em `kCamPitchSign` se invertido (mesmo processo do wheel rig).

---

## 7. Arquivos a tocar

| Arquivo | Mudança |
|---------|---------|
| `camera_system.hpp` | Estado pitch suavizado; tunables por preset |
| `camera_system.cxx` | Offset body-frame; look 3D; smooth pitch |
| `camera_safety.hpp` | Clearance dinâmico com pitch |
| `game_loop_system.hpp` | Já chama `SetRoadAttitude`; sem probes |
| (opcional) overlay debug | `camPitch` / clearance |

**Não tocar:** `FindSurfaceY` / probes de pista para a câmera.

---

## 8. Riscos e mitigações

| Risco | Mitigação |
|-------|-----------|
| Sinal de pitch invertido | Flag `kCamPitchSign` ±1; validar em 1 rampa |
| Trepidação em costuras | Smooth + deadzone + rate limit |
| Framing “longe” do carro | `pitchFollowGain < 1` no CAM3; look ainda ancorado no carro |
| Aclive “cega” | Clamp de pitch de look; lookAhead 3D não só “para o chão” |
| Boot/emulador | Zero probes; só attitude do carro |

---

## 9. Ordem de entrega recomendada

1. **F0** debug overlay  
2. **F1** offset body-frame com pitch suavizado + clearance  
3. **F2** look 3D / inclinação visual  
4. **F3** grade extrapolate atrás (se F1/F2 não bastarem)  
5. **F4–F5** tune presets + validação  

**Estimativa:** F1+F2 ~2–4 h de implementação + 1 sessão de tune em rampa real da pista.

---

## 10. Status

- [x] Pesquisa (comportamento chase / terrain incline / anti-padrão probes)  
- [x] Diagnóstico do código atual  
- [x] Plano F0–F5 escrito  
- [x] Implementação F0–F3 (2026-07-24): body-frame pitch, look 3D, boom + grade-behind clearance  
- [ ] Validação em hardware/emulador (usuário)

### Implementado
- `UpdateSmoothedCamPitch` + `ApplyLocalPitchYZ` (Y-down, +pitch = nose down)
- `ResolvePresetOffsetWorld` rotaciona (Y,Z) com pitch do carro
- `LookTarget` look-ahead em 3D com mesmo pitch
- Boom clearance dinâmico com |pitch|
- `ApplyGradeBehindClearance` (sem probe): sobe cam no declive
- Tunables por preset (`pitchFollowX100`, blend, baseBoomLift)
- `kCamPitchSign` para inverter se necessário
- Debug CAM2: `y / pit / car / g`
