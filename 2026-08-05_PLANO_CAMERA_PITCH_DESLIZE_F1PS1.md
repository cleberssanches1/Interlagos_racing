# Plano de ação — Câmera + pitch no declive (alvo F1 PS1 1996)

**Data:** 2026-08-05  
**Projeto:** Interlagos_racing (`feature/sh2`)  
**Vídeo nosso (errado):** `Yabause v0.9.14 2026-08-05 23-05-18.mp4`  
**Vídeo referência (alvo):** `(327) Evolution of Senna Curve… Formula 1 (PS1, 1996)`  

**Escopo:** atitude do carro + chase camera no declive acentuado (S / descida longa).  
**Fora de escopo:** reverse belt, dual-mesh, densificar asset (exceto se gate provar laje).

---

## 1. O que os vídeos mostram

### 1.1 Nosso jogo (23-05-18)

| Fase | Observação |
|------|------------|
| **Início da descida** | Bom: carro começa a descer e a colar no asfalto (progresso do deslize S1–S3) |
| **Meio do declive íngreme** | Câmera **sobe** (boom alto / olha “por cima”); carro fica **embicado** (nariz baixo exagerado e longo) |
| **Saída do íngreme** | Pitch e câmera **só então** corrigem — atraso de recuperação |
| **Curva no fundo** | Framing mais baixo de novo, mas o trecho crítico já falhou visualmente |

Síntese: **heave melhorou**, mas **grade hold + pitch sintético + lift da câmera com pitch** geram o trio:  
(1) embicada longa, (2) câmera sobe, (3) recovery atrasada.

### 1.2 Referência F1 PS1 1996 (Alesi / Senna S)

| Fase | Observação |
|------|------------|
| Entrada | Nariz segue a rampa de forma **moderada**; câmera atrás **plantada**, não sobe no ar |
| Meio | Carro e câmera **juntos** no plano da pista; boom acompanha o pitch sem “torre” |
| Saída | Atitude e câmera **relaxam cedo** com a topologia; sem embicada residual |

Síntese do alvo de época: **plano do solo manda**, pitch **limitado e reativo**, câmera **acima do asfalto mas colada ao body** — não um second-order lag que escala clearance com pitch.

---

## 2. Cadeia atual (código)

```
MapHeight F/R/L/R
    → gradeTan (filtro) + gradeHold (até ~48 frames se F−R zera no slab)
    → F/R sintéticos se chord fraco (pitch visual)
    → bodyPitch (CarWheelRig) + gradeTanX100 (debug/cam)
    → SetRoadAttitude(grade, bodyPitch)
    → UpdateSmoothedCamPitch: chase usa **gradePitch = gradeTan*57/100**
    → ApplyLocalPitchYZ(boomPitch): +pitch nariz-baixo **eleva o boom traseiro**
    → BoomSafetyConfig: clearance += f(pitch)  // até +1° de clearance por grau
    → ResolveBoomGuard + vertical blend 0.25
```

### 2.1 Mapeamento sintoma → causa

| # | Sintoma no vídeo 23-05 | Causa provável no código |
|---|------------------------|---------------------------|
| **A** | Carro embicado no meio/fim do íngreme | `gradeHold` mantém `gradeTan` alto; F/R sintéticos forçam pitch mesmo com MapHeight “plano”; recovery só quando hold expira |
| **B** | Câmera sobe no declive | `ApplyLocalPitchYZ`: com +pitch, offset traseiro (Z&lt;0) **sobe em altitude**; empilhado com `baseBoomLift` + `offsetY −20/−12` |
| **C** | Clearance cresce com pitch | `BoomSafetyConfig`: `clearance += absPitch*2/3` e ainda `+ (pitch − deadzone)` no nose-down → em 20° pitch, +~30 u de altura mínima |
| **D** | Cam pitch “pesado” e lento a sair | Chase usa **só grade** (`targetPitch = gradePitch`), não o body; hold de grade atrasa o zero; `kMaxCamPitchStepDeg = 1` + blend ~0.25 |
| **E** | Descida inicial OK | Continuous slide + look-ahead (S1–S2) funcionam na entrada |

### 2.2 Peças-chave (arquivos)

| Arquivo | Papel |
|---------|--------|
| `car_ground_follower.hpp` | grade, hold, look-ahead, F/R sintéticos, heave slide |
| `car_wheel_rig.*` | pitch/roll visual do chassis |
| `camera_system.cxx` / `.hpp` | `UpdateSmoothedCamPitch`, boom offset, clearance |
| `camera_safety.hpp` | hard floor boom vs car |
| `game_loop_system.hpp` | `SetRoadAttitude(grade, bodyPitch)` |

---

## 3. Alvo de comportamento (critérios de sucesso)

No mesmo trecho do vídeo 23-05 (descida íngreme → fundo):

1. **Carro:** pitch acompanha a rampa **sem embicada residual** &gt; ~2–3 frames após o MapHeight afrouxar; rodas coladas; sem escada.  
2. **Câmera:** boom **atrás e um pouco acima** do carro (estilo F1 96), **sem subir** quando o pitch cresce; horizonte da pista permanece legível.  
3. **Recovery:** ao sair do íngreme, pitch e cam **caem juntos** com a grade real (não esperam fim do hold de 48 frames).  
4. **FPS / budget:** sem MapHeight na câmera; no máximo reusar grade/body já calculados; look-ahead de solo já existe no follower.

---

## 4. Princípios de design (como o F1 96 “sente”)

1. **Heave e pitch são o plano da estrada** — a câmera **copia** esse plano com ganho &lt; 1, não inventa lift.  
2. **Grade hold serve o heave**, não deve **congelar** o pitch visual nem o pitch da cam por dezenas de frames.  
3. **Clearance de boom ≠ pitch:** anti-asfalto usa altura relativa ao **car.Y**, não “+1 unit por grau de embicada”.  
4. **Assimetria de recovery:** sair do nariz-baixo (zerar pitch positivo) deve ser **mais rápido** que entrar (anti-empino já existe no body; espelhar na cam).  
5. **Uma fonte de verdade para cam chase:** preferir `min(|gradePitch|, |bodyPitch|)` ou blend limitado — não grade hold “puro” se o body já recuperou.

---

## 5. Plano de implementação por fases

### C0 — Instrumentação (1 sessão curta)

**Objetivo:** provar no Yabause se o problema é grade hold, boom pitch ou clearance.

Overlay / log já parcial (`CAM2 y/pit/car/g` em ChaseNear). Expandir se preciso:

| Campo | Uso |
|-------|-----|
| `g` | `gradeTanX100` |
| `pB` | `bodyPitchDeg` |
| `pC` | `smoothedCamPitch` / boom pitch |
| `hold` | `gradeHoldFrames` (expor no debug) |
| `camY−carY` | gap vertical boom |

**Gate:** no meio do embicado, se `hold>0` e `g` alto com MapHeight flat → priorizar C1; se `pC` alto e `camY` muito acima de `carY` → C2/C3.

---

### C1 — Grade hold só para heave (não “trava” pitch)

**Arquivos:** `car_ground_follower.hpp`, `car_physics_shared.hpp`

1. Separar **grade para heave** vs **grade para atitude/cam**:  
   - `gradeTanHeaveRaw` — pode ter hold/look-ahead (deslize).  
   - `gradeTanAttitudeRaw` — **decay rápido** quando F−R real some (ex. hold 8–12 frames, não 48).  
2. F/R sintéticos e `debugGradeTanX100` usam **attitude**, não heave hold longo.  
3. Look-ahead continua alimentando heave; attitude só se look-ahead + F−R reais confirmarem.

**Gate:** no meio do slab, heave ainda desce; pitch **não fica embicado** se a face atual não pede.

---

### C2 — Pitch do body: recovery rápido e cap no declive

**Arquivos:** `car_wheel_rig.*`

1. Rate assimétrico já existe conceitualmente: **acelerar recovery** (nariz-baixo → zero) vs entrada.  
2. Cap de pitch em declive alinhado ao tan real (~S Senna tan≈0.4 → ~22°), sem sintético empurrar além do F−R medido + margem.  
3. Não regenerar target pitch só a partir de hold sem amostra F−R fresca.

**Gate:** embicada residual some em &lt; ~10 frames ao afrouxar a rampa.

---

### C3 — Câmera chase estilo F1 96 (anti-“torre”)

**Arquivos:** `camera_system.cxx` / `.hpp`

1. **Fonte do pitch de cam (chase):**  
   - `targetPitch = clamp(blend(gradeAttitude, bodyPitch), ±kMax)` com ganho `pitchFollowX100` (~35–45% já no Far/Near).  
   - **Não** seguir grade heave/hold.  
2. **Assimetria cam:** step para **sair** de nose-down ≥ 2× step para entrar (hoje 1°/frame simétrico).  
3. **BoomSafetyConfig:** remover ou reduzir drasticamente `clearance += pitch`; manter floor fixo `minBoomClearance` + pequeno termo em `|pitch|` (ex. max +4 u).  
4. **ApplyLocalPitchYZ:** para chase, usar **fração** do pitch no boom (ex. 50% do `smoothedBoomPitch`) **ou** pitch só no look-at, offset Y/Z com pitch reduzido — evita “rear sobe no ar”.  
5. Opcional: hard cap `camera.Y − car.Y` (altitude) em declive para não divergir.

**Gate:** no vídeo, boom não sobe no íngreme; enquadramento próximo ao F1 96 (carro grande no frame, pista à frente).

---

### C4 — Look-at e vertical follow

**Arquivos:** `camera_system.cxx` `LookTarget`, blend Y em `CameraLocation`

1. Look-at: altura de mira acompanha `car.Y + lookHeight` com o **mesmo** pitch suave da cam (já parcialmente).  
2. Vertical blend: em grade válida, **aumentar** um pouco o blend Y (menos atraso vertical da cam vs car) sem herdar planar 0.7.  
3. Garantir que `ResolveCameraSurfaceGuard` (mesh) não empurre a cam para cima de forma agressiva no declive (revisar se o gap vertical cresce só no guard).

**Gate:** cam e carro sobem/descem **juntos** no S.

---

### C5 — Validação

| Teste | Pass |
|-------|------|
| Descida íngreme (mesmo path 23-05) | Sem torre de cam; pitch moderado |
| Meio do S | Carro plantado; cam atrás estável |
| Saída do íngreme | Recovery pitch+cam &lt; ~0,3 s |
| Overlay | `hold` attitude baixo; `pC` ≈ f(`pB`); `camY−carY` estável |
| FPS | sem regressão de probes |

---

## 6. Ordem de ataque recomendada

```
C0 (overlay hold/pC) 
  → C1 (separar grade heave vs attitude)   // remove embicada residual
  → C3 (cam pitch/clearance/boom)         // remove câmera subindo
  → C2 (body recovery fine-tune)
  → C4 (look-at / vertical)
  → C5 validação vs F1 96
```

**Não reabrir** dual-mesh / reverse nesta linha.  
**Não** desligar continuous slide de heave (S1) — só desacoplar do pitch/cam.

---

## 7. Riscos e mitigações

| Risco | Mitigação |
|-------|-----------|
| Voltar escada de heave ao encurtar hold | Hold longo **só** em `gradeTanHeave`; attitude curta |
| Cam fura asfalto sem clearance por pitch | Floor fixo + mesh guard; não pitch-scaling agressivo |
| Pitch fraco demais no S | Cap/rate em C2; validar tan medido no overlay `g` |
| Oscilação se dual-source grade | Uma amostra F−R + look-ahead filtrado; sem 3 fontes competindo |

---

## 8. Entregáveis

1. Este plano (doc).  
2. PR lógico em 2 commits:  
   - **physics attitude:** C1 + C2  
   - **camera arcade:** C3 + C4  
3. Vídeo de validação no mesmo trecho + 1 frame-side-by-side com F1 96.

---

## 9. Status

| Fase | Estado |
|------|--------|
| C0 | parcial (overlay g/p existente; hold attitude implícito em g) |
| **C1** | **feito** — `gradeTanAttitudeRaw` + hold 10f; heave hold 48f; cam usa attitude |
| **C2** | **feito** — pitch cap 22°; enter 2°/f, recover 4°/f; sintético só attitude |
| **C3** | **feito** — cam milder blend; boom 45% pitch; clearance max +4; out 3°/f |
| C4–C5 | pendente (look-at fino + validação vídeo) |
| **J1** | **feito** — cap chord F−R (10u / 5u na junção); sem F/R sintético |
| **J2** | **feito** — pitch LPF forte; jump 4u; 1°/f dive, 4°/f recover; cap 16° |
| **J3** | **feito** — ride lift ~chord/8 anti-nariz no asfalto |

Heave continuous slide mantido.

### Nota PS1/arcade (junção de segmentos)

Jogos da era (Ridge, F1 96, Daytona-style) **não** aplicam pitch = atan(ΔY bruto) entre faces em degrau. Usam:
1. plano de rodas com **ΔY limitado** + low-pass;
2. heave no centro do carro separado da atitude;
3. recovery rápida de pitch ao sair do degrau.

O raw MapHeight com F e R em lajes diferentes é o que gerava a embicada em `23-22-15`.
