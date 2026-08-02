# Plano de ação — câmera por grade + streaming em marcha contrária

**Data:** 2026-07-23  
**Objetivo:** (1) Câmera chase se eleva/ajusta com a inclinação da pista no declive; (2) janela de segmentos inverte e **continua** construindo ao percorrer no sentido contrário.

---

## 1. Sintomas

| Cenário | Comportamento | Esperado |
|---------|---------------|----------|
| Declive | Pitch do carro OK; câmera “raspa” faces do chão | Câmera sobe / look se afasta da face |
| Virar 180° | Delay até o cenário aparecer | Flip mais rápido + rebuild confiável |
| Andar no sentido contrário | Cenário para de construir após um trecho | Slides reverse contínuos com backlog |

---

## 2. Diagnóstico

### 2.1 Câmera
- Offset chase é **só planar + Y fixo** (`ResolvePresetOffsetWorld`); ignora grade/pitch.
- Comentário em `ResolveCameraFrameState`: pitch de terreno desligado (probes de look.Y fecharam emulador).
- Solução segura: **sem probes** — usar `gradeTan` / `bodyPitch` já calculados no carro.

### 2.2 Reverse / streaming
1. **`UpdateActiveSegmentWindowForPosition` usa `cameraWindowDirection_`** enquanto o catch-up em `BeginFrame` usa `windowDirection_`. Se rebuild de flip falha ou atrasa, target e slides **divergem** → backlog vira “metade da pista” e zera (`backlog >= total/2 → 0`) → **para de construir**.
2. Ramo “observed behind” faz `queueDeferredSlide(-desiredDirection, …)` — **slide anti-direção** que luta com reverse.
3. Confirm/cooldown de flip (2+6 frames) + rebuild pesado = delay perceptível.
4. Prefetch/prewarm de LOD de boundary **desliga em reverse** (`windowDirection_ < 0 return`).

---

## 3. Plano (fases)

### F0 — Instrumentação (opcional runtime)
- Telemetria já tem WIN/dir; manter logs de dir flip.

### F1 — Câmera por attitude do carro (sem probe de pista)
1. `CameraSystem::SetRoadAttitude(gradeTanX100, bodyPitchDeg)`.
2. Em `ResolveCameraFrameState`, alimentar a partir de `CarSystem::RuntimeDebugSnapshot`.
3. Em `ResolvePresetOffsetWorld`: no **declive** (grade>0 / pitch nariz-baixo), **elevar** offsetY (mais negativo no Y-down), clamp.
4. Em `LookTarget`: elevar `lookHeight` levemente no declive.
5. `CameraSafety::ResolveBoomGuard` no resolve final (clearance mínimo acima do carro).

### F2 — Streaming reverse consistente
1. Slides / target usam **`windowDirection_`** (fonte da verdade da janela carregada).
2. Se `cameraWindowDirection_ != windowDirection_`: **não** atualizar target por observed; só `UpdateCameraDrivenWindowDirection` tenta rebuild.
3. Remover slides anti-direção no ramo “behind”.
4. Flip: confirm 1 com travel forte, cooldown menor; retry rebuild todo frame enquanto mismatch.
5. Após flip OK: catch-up reverse com backlog correto (já direction-aware).
6. Prefetch reverse: não early-return total em boundary prewarm (opcional mínimo: permitir 1 target no lado reverse).

### F3 — Validação
- Declive: câmera acima do asfalto, carro ainda enquadrado.
- 180° + trecho longo no sentido contrário: segmentos continuam entrando.
- FPS estável (sem probes extras de câmera).

---

## 4. Arquivos
- `camera_system.hpp` / `.cxx`
- `camera_safety.hpp` (já existe boom guard)
- `game_loop_system.hpp` (`ResolveCameraFrameState`)
- `track_system.cxx` (flip + window update + prewarm)

---

## 5. Status
- [x] Plano escrito  
- [x] Implementação F1+F2 (esta entrega)  
- [ ] Validação em hardware/emulador (usuário)
