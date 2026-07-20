# Plano de ação — declives transitáveis vs muros (70–90°)

**Objetivo:** o carro deve **seguir qualquer face “drivable”** com inclinação **&lt; ~70°** em relação ao plano horizontal, sem empacar e sem tratar rampa como parede. Faces **~70–90°** são **muros**.

**Fora de escopo deste plano:** reabrir o patch de rampas que derrubou FPS (scan caro / early-accept restrito). Soluções aqui devem ser **baratas em SH2**.

---

## 1. Definição geométrica (fonte da verdade)

Convenção do motor (comentários atuais): **maior Y = altitude menor** (eixo “up” ≈ −Y).

Para uma face com normal **n** (unitária ou aproximada em fixed-point):

| Grandeza | Fórmula |
|----------|---------|
| Ângulo da **normal** com o “up” | `θ = acos(\|n·up\|)` |
| **Inclinação da superfície** com o horizontal | `α ≈ θ` |
| Flat | `α ≈ 0°`, `\|ny\| ≈ 1` |
| Rampa 45° | `α ≈ 45°`, `\|ny\| ≈ 0.707` |
| Limite muro (70°) | `α ≥ 70°`, `\|ny\| ≤ cos(70°) ≈ **0.342** |
| Parede vertical | `α ≈ 90°`, `\|ny\| ≈ 0` |

**Regra de jogo (proposta):**

```text
if |ny_normalized| >= cos(kMaxDriveableSlopeDeg)  →  GROUND (transitar + adesão Y + pitch)
else                                               →  WALL  (push planar, sem “chão”)
```

Com `kMaxDriveableSlopeDeg = 70` (ajustável 65–75).

Em fixed 16.16 unitário: `kMinGroundNyAbs ≈ 0.342 * 65536 ≈ 22400` (`0x57A0`).  
Se a normal **não** estiver normalizada, comparar razão:

```text
|ny| / max(|nx|,|ny|,|nz|)  >= cos(70°)
// ou: |ny| * |ny|  >=  cos²(70°) * (nx²+ny²+nz²)
```

---

## 2. Como o código trata isso hoje

### 2.1 Solo (`FindSurfaceYByFamilySet`)
- Resolve Y de faces sob o probe em XZ.
- Ranking atual: prefere face “suporte” com `yRaw >= py - 0.25` e **menor gap com a Y do carro**.
- **Não classifica** face por ângulo de inclinação.
- Early-accept e cache de última face podem **segurar platô** até o XZ sair do triângulo → **escadinha**.

### 2.2 Parede (`FindPlanarWallPush` ~linha 7847–7849)
```cpp
planarNormalAbs = max(|nx|, |nz|);
if ((planarNormalAbs * 2) < |ny|) continue;  // descarta faces “muito de chão”
```
- Equivale a algo como “só considera parede se o componente planar for grande o bastante vs Y”.
- **Não** usa o limiar 70° do design; o critério é grosso e pode:
  - aceitar curbs/rampas íngremes como parede, ou
  - falhar em paredes com normal ruída.

### 2.3 Adesão (`ApplyVerticalAdhesion`)
- Com suporte: `Y = surfaceYTarget` (snap).
- Sem suporte: hold curto de Y; **não** trava mais XZ (bom).
- Se o **Y amostrado** for errado/platô (ranking/cache), o snap ainda **parece degrau**.

### 2.4 Pitch visual (`CarWheelRig`)
- Usa `frontY − rearY` dos probes.
- Independente do normal da face; se front/rear erram, o nariz erra.

### 2.5 Wall runtime
- `ResolveWallPushMultiProbe` + damping de velocidade em impacto.
- Em aclive, se a rampa for classificada como wall → **“bate como muro”**.

---

## 3. O que os exemplos e a indústria fazem

### REDRIVER2 (`wheelforces.c` / `MapHeight`)
- Altura por roda **todo frame** (`FindSurfaceD2` + normal).
- Compressão de suspensão com **normal da superfície**:
  - `newCompression ∝ (surfaceY − wheelY) * surfaceNormal.y`
- Atrito/força **modulado por `surfaceNormal[1]`** (menos grip em “quase parede”):
  - se `surfaceNormal[1] < 3276` (~0.1 em 1.0=4096 escala PSX) reduz friction.
- Separação solo/parede vem do **pipeline de superfície** + tipo (`SURF_*`), não de “congelar XZ”.

### Unreal / Godot / StackExchange (indústria)
- **Walkable slope angle** (default ~45°, configurável até &lt;90°).
- Classificação: `angle(normal, up) ≤ maxWalkable` → floor; senão wall/slide.
- Movimento: projetar velocidade no plano do chão (`v' = v − n(n·v)`).

### VDrift / TORCS
- Contato por roda com **normal de contato**; forças no plano da superfície.
- Não confundem wall collider com ground contact.

### Lição aplicável ao Saturn (barata)
1. **Um teste de normal** (limiar cos) separa GROUND vs WALL.  
2. Movimento em GROUND: **projetar velocidade no plano** + colar Y no plano.  
3. WALL: push planar **sem** usar a face como chão.  
4. Evitar rescans caros: reutilizar normal da face vencedora do probe já feito.

---

## 4. Arquitetura alvo (leve)

```text
Probe XZ (já existe)
    │
    ▼
Para cada face candidata:
  classificar: GROUND if |n·up| >= cos(70°)
               WALL   otherwise
    │
    ├─ GROUND candidates → escolher melhor (seed + gap Y razoável)
    │       → surfaceY, groundNormal
    │
    └─ WALL candidates  → FindPlanarWallPush só nestas
            → wallPush (sem adesão Y)
    │
    ▼
Movimento:
  - Colar Y no plano GROUND (snap ou 1-frame blend barato)
  - Velocidade planar: project onto plane (remove componente “para dentro” do chão)
  - Pitch visual: a partir de normal do chão OU front/rear Y (mesma face se possível)
  - Wall push só se class == WALL
```

**Importante para FPS:** não reabrir o full-scan multi-segmento sem early-accept. O classificador usa **só a normal da face já avaliada** no loop atual.

---

## 5. Plano de implementação em fases

### Fase 0 — Instrumentação (1 patch, baixo risco)
**Arquivos:** `car_ground_follower.hpp`, debug HUD opcional.

- Log por frame (throttle):  
  `slopeDeg` ou `|ny|`, `wallHit`, `groundY`, `frontY/rearY`, `fwdSpeed`.
- Confirmar no S do Senna e num aclive:
  - rampa real → `|ny|` alto (chão);
  - muro → `|ny|` baixo.

**Critério de saída:** números batem com o visual da malha.

---

### Fase 1 — Classificador único GROUND/WALL (núcleo)
**Arquivos:** `track_system.cxx` (`FindPlanarWallPush` + helper compartilhado), preferencialmente um util:

```cpp
// fixed-point, sem sqrt se possível
bool IsGroundFace(nx, ny, nz, kMinNyAbsOrCos2);
bool IsWallFace(...) { return !IsGroundFace(...); }
```

**Mudanças:**
1. Extrair helper `ClassifyFaceByNormal`.
2. Em **wall push**: só faces `IsWallFace` (substituir o teste `planar*2 < |ny|`).
3. Em **surface Y**: ao avaliar face, se `IsWallFace` → **não** usar como chão (ou peso zero).

**Tunable inicial:**
- `kMaxDriveableSlopeDeg = 70`
- `kMinGroundNyCos = cos(70°) ≈ 0.342`  
- Ajuste fino 65–75 após playtest.

**Critério de saída:** muro de asfalto vertical empurra; rampa &lt;70° **nunca** gera `debugWallHit` estável.

---

### Fase 2 — Movimento sobre GROUND (anti-empacar)
**Arquivos:** `car_ground_follower.hpp`, `car_dynamics_model.hpp` / `car_physics_v2.hpp`.

1. **Guardar `groundNormal`** no `GroundState` quando o probe de chão vencer.
2. **Projetar velocidade** no plano do chão (1× por frame, ~10 muls):
   - remove componente que “fura” o asfalto em aclive;
   - em declive, deixa o carro **escorregar ao longo da rampa** em vez de bater de frente no plano.
3. **Adesão Y:** manter snap ao `surfaceY` do plano GROUND (já existe); se miss 1–2 frames, hold Y (já existe) **sem** wall.
4. **Não** aplicar wall damping se o hit vier de face reclassificada como GROUND.

**Critério de saída:** sobe/desce rampa sem “parede”; não fica preso em faces &lt;70°.

---

### Fase 3 — Pitch alinhado à pista (visual)
**Arquivos:** `car_wheel_rig.cxx`, opcionalmente normal do `GroundState`.

**Opção A (barata, preferida):** pitch a partir de **normal do chão** no centro do carro:
```text
pitch ≈ atan2(n_forward, n_up)  // ou approx fixed-point
```
**Opção B:** manter front/rear Y, mas ambos devem usar **só faces GROUND** e o mesmo critério de Fase 1.

**Sinal:** validar com `RotX(180)` do modelo; um flag `kPitchVisualSign = ±1` para calibrar em 1 build.

**Critério de saída:** nariz **desce** no declive e **sobe** no aclive, sem oscilação forte.

---

### Fase 4 — Degraus (só se ainda existirem após 1–2)
**Sem** reabrir scan pesado:

1. Desligar early-accept se `|ΔY|` face-a-face do seed &gt; limiar (barato, local).
2. Blend Y de **1 frame** só quando `|ΔY| &lt; 2` (costura); snap se maior (buraco).
3. Garantir 2 probes front/rear **sempre em grade** (`lastSlopeAbsY` ou `|ny|` do chão).

**Critério de saída:** S do Senna sem escadinha óbvia a 30 FPS estável.

---

### Fase 5 — Validação e tunables
| Teste | Esperado |
|-------|----------|
| Declive Senna S | Desce contínuo; sem wall hit; pitch nariz baixo |
| Aclive | Sobe sem freio de parede; pitch nariz alto |
| Muro vertical / curb ~80° | Wall push; não “sobe como rampa” |
| FPS | ≥ baseline pós-revert do patch caro |

HUD de debug (opcional build flag):
`ny`, `slopeDeg`, `G/W`, `wall`, `gY`.

---

## 6. O que **não** fazer (lições recentes)

| Evitar | Por quê |
|--------|---------|
| Remover early-accept global / full scan multi-seg todo frame | FPS colapsou |
| Tratar “tudo &lt;90°” sem limiar de muro | Curbs viram rampa escalável |
| Congelar XZ em miss de solo | Empaca no declive |
| Wall push com raio grande + faces de rampa | Aclive = muro |
| Patch “tudo de uma vez” sem Fase 0 | Difícil isolar regressão |

---

## 7. Mapa de arquivos

| Peça | Arquivo |
|------|---------|
| Classificar normal | `track_system.cxx` (+ helper em `car_physics_shared.hpp` ou `surface_classify.hpp`) |
| Wall só em WALL | `FindPlanarWallPush` |
| Chão só em GROUND | `FindSurfaceYByFamilySet` / `evaluateFaceCandidate` |
| Estado normal | `GroundState` em `car_physics_shared.hpp` |
| Probe + wall apply | `car_ground_follower.hpp` |
| Projetar velocidade | `car_physics_v2.hpp` / `car_dynamics_model.hpp` |
| Pitch | `car_wheel_rig.cxx` |
| Flags/tunables | `car_physics_shared.hpp`, `physics_feature_flags.hpp` |

---

## 8. Ordem de merge recomendada

1. **Fase 1** sozinha → playtest muro vs rampa (maior ROI, pouco custo).  
2. **Fase 2** → some “parede no aclive” e empacar.  
3. **Fase 3** → pitch correto.  
4. **Fase 4** só se degrau sobrar.  

Cada fase: 1 build + teste Senna S + 1 aclive + 1 muro + FPS.

---

## 9. Resumo executivo

O motor hoje **não** usa o conceito “inclinação &lt; 70° = chão, 70–90° = muro”. Mistura ranking de altura com um filtro de parede grosso, e o wall push pode frear rampas. A correção correta e barata é:

1. **Classificar cada face pela normal** (limiar 70°).  
2. **Só GROUND** alimenta Y/pitch/movimento.  
3. **Só WALL** gera push.  
4. **Projetar velocidade no plano do chão** para não “bater” na rampa.  

Isso alinha com REDRIVER2/Unreal/prática comum e evita o caminho de FPS alto custo já revertido.

---

## Status de implementação (2026-07-16)

| Fase | Status |
|------|--------|
| 1 Classificador 70° | **Feito** — `surface_classify.hpp`; wall + ground filters |
| 2 Velocidade no plano | **Feito** — projeção em `car_physics_v2.hpp` + normal approx dos probes |
| 3 Pitch | Normal approx + front/rear Y (já existentes); limiar de face GROUND afrouxado de 60° para 70° |
| 4 Anti-degrau pesado | **Não** (evitar regressão de FPS) |

Arquivos: `surface_classify.hpp`, `track_system.cxx`, `car_physics_shared.hpp`, `car_ground_follower.hpp`, `car_physics_v2.hpp`.
