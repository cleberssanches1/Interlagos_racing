# Análise do runtime — carro, física, segmentos e esteira

**Projeto:** Interlagos_racing (SaturnRingLib)  
**Branch / baseline:** `feature/sh2` · commit de referência `bec59f6` (“Atualizações”)  
**Data do documento:** 2026-08-02  
**Escopo:** estado **atual do código** (após reset das experimentações recentes). Sem implementação neste arquivo — só análise e problemática aberta.

**Convenção de mundo:** eixos SGL com **Y-down** (maior `Y` = mais baixo no espaço). Ride height negativo = corpo um pouco **acima** do asfalto.

---

## 1. Movimentação do carro

### 1.1 Fluxo por frame (visão de sistema)

```
Input (pad)
  → CarSystem / SimpleGameplayTick
    → física longitudinal + lateral + yaw  (CarDynamics / CarPhysicsV2)
    → ground probes + adesão vertical       (GroundFollower)
    → parede 2D / push
  → CarWheelRig (pitch/roll visual a partir dos Y das rodas)
  → MeshRenderer (corpo + rodas locais)
  → CameraSystem (chase, pitch suave do body)
  → TrackSystem (observed segment, esteira, draw)
```

Arquivos principais:

| Papel | Arquivo(s) |
|-------|------------|
| Orquestração | `car_system.cxx` / `.hpp`, `game_loop_system.hpp` |
| Dinâmica XZ + câmbio | `car_dynamics_model.hpp`, `car_physics_v2*.hpp` |
| Solo / MapHeight | `car_ground_follower.hpp` |
| Estado / tunables | `car_physics_shared.hpp` |
| Atitude visual | `car_wheel_rig.cxx` / `.hpp` |
| Render do mesh | `mesh_renderer.*` |

### 1.2 Movimento planar (XZ)

- Velocidades em **16.16 fixed**: `forwardSpeed`, `lateralSpeed`, `yawRateDegPerFrame`.
- Integração típica (modo cinemático / slip):  
  `ΔX, ΔZ` a partir de `sin(yaw)·forward + cos(yaw)·lateral` (e sinais de orientação do mesh).
- Steering arcade com perda de autoridade em alta velocidade, freio em reta vs trail-brake, launch, etc. (tabelas em `Tunables`).
- **Não** há simulação de suspensão por mola; o “grude” no chão é **MapHeight + adesão de Y**.

### 1.3 O que o jogador “sente” vs o que o código faz

| Sensação | Mecanismo real |
|----------|----------------|
| Acelerar / frear | `forwardSpeed` + drag / gear bands |
| Virar | `steerDeg` → yaw rate → yaw acumulado |
| Subir / descer rampa | Probes de Y + `ApplyVerticalAdhesion` |
| Inclinar o chassis | `CarWheelRig` a partir de ΔY frente/trás e esquerda/direita |
| Câmera “acompanhar” o morro | `SetRoadAttitude(grade, bodyPitch)` → boom + pitch suave |

---

## 2. Física (solo, colisão, limites Saturn)

### 2.1 Princípio

Física **arcade de baixo custo** (`PhysicsFeatureFlags::kEnableSaturnLowCostPhysics` e cadências de probe em `Tunables`):

- Poucos samples de superfície por frame.
- Reuso de seed de segmento (`lastSurfaceSegmentId` / observed).
- Paredes em **push planar 2D** (não full 3D mesh-vs-mesh).
- Solo por **altura de face** (MapHeight / family ou surface-type), inspirado em REDRIVER2/PS1.

### 2.2 Ground follower — probes e plano 4 rodas

Em `ProbeSurfaceTarget` (`car_ground_follower.hpp`):

1. Calcula 4 pontos no retângulo das rodas (FL/FR/RL/RR), offsets `kProbeHalfWheelBase` ≈ 0.85 e `kProbeHalfTrack` ≈ 0.55.
2. Cada ponto: `TryProbeSurfaceY` (strict + fallback soft com afinidade de segmento).
3. **Suporte:** `hasGroundSupport` se eixo dianteiro **ou** traseiro tem hit.
4. Médias de eixo/lado → `frontY`, `rearY`, `leftY`, `rightY`.
5. **Grade:**  
   `tan ≈ (frontY − rearY) / wheelbase` (16.16), filtrado (`kGradeFilterShift`).  
   Y-down: `tan > 0` ⇒ nariz mais baixo ⇒ declive andando para frente.
6. **Altura do corpo:** média dos cantos válidos (preferência mid F/R, e mid L/R se os quatro eixos laterais valem) + `GetRideHeightOffset()` (base ~−0.25 + lift em `main.cxx` se aplicado).
7. **Topologia:** se o alvo de Y “cai” (asfalto mais baixo) ou o acorde F−R indica declive, marca `topologyDropFrames` para adesão mais agressiva na descida.

### 2.3 Adesão vertical (`ApplyVerticalAdhesion`)

- Aplica push de parede em XZ; **não congela** XZ em miss de solo (evita “ficar preso” no declive).
- Se sem superfície: segura Y filtrado por `surfaceContactFrames`, depois solta o filtro.
- Com superfície: aproxima `surfaceYFiltered` → `surfaceYTarget` com taxas **assimétricas**:
  - **Descer** (dY > 0 em Y-down): steps grandes / snap em gap ou topo drop.
  - **Subir**: steps menores; snap duro se o corpo está **penetrando** o asfalto (`kClimbPenetrateHardY`).

### 2.4 Atitude visual (`CarWheelRig`)

- Só atualiza **pitch** se `frontValid && rearValid` (bits no `groundMask`).
- Só atualiza **roll** se `leftValid && rightValid`.
- `pitch ∝ (Yfront − Yrear) / wheelbase`, com deadzone, filtro de salto e **rate assimétrico** (nariz-baixo mais rápido que subida).
- Aplica `SetBodyAttitude` no `MeshRenderer` + offsets de suspensão cosméticos nas rodas.

### 2.5 Paredes

- Queries de push planar por raio / multi-probe (seed de segmento).
- Separadas do MapHeight de asfalto; densificação de faces de muro melhora a colisão sem alterar o modelo de rampa.

### 2.6 Limites conscientes do hardware

| Recurso | Implicação |
|---------|------------|
| Sem Z-buffer SGL típico | Ordem de draw (pista vs carro) importa |
| Pouca LWR/HWR | Janela de 20 segs, não malha full track |
| SH2 single/dual | Física no Master (ou POC Slave); probes devem caber no frame |
| MapHeight por face | Depende de GEO/RDR denso sob as rodas (lod_0) |

---

## 3. Renderização dos segmentos

### 3.1 Pipeline de assets (offline → runtime)

```
Blender / NYA / LOD sets
  → GEO / MAT / RDR (packs TRKRDR, opcional TRKRDRL)
  → CD/data/*.BIN
  → TrackSystem::BuildSegmentIntoRenderer / LoadRdrMapped
  → TrackRenderer (faces + family ids)
  → slots VDP1 (texturas 64/32 por rank)
  → producer / sort / draw
```

### 3.2 Design LOD por **rank lógico** na janela (não por ID global)

Comentários e constantes em `track_system.cxx`:

| Rank na esteira | Malha (design) | Textura (apresentação) |
|-----------------|----------------|-------------------------|
| 0–1 | lod_0 high (`TRKRDR`) | 64×64 |
| 2–9 | lod_1 low (`TRKRDRL` se ativo) | 64×64 |
| 10–19 | low | 32×32 |

- **MapHeight e paredes** preferem malha densa sob o carro → ranks 0–1 importam para a física.
- `ResolveDesignGeoTierByRank` escolhe high/low no load/slide.
- **Risco conhecido:** trocar malha **in-place** no renderer vivo (`BuildSegmentIntoRenderer` no slot ativo) pode esvaziar geometria se falhar → pista some, carro continua. (Tema de estabilidade, não de física.)

### 3.3 O que é “renderizar um segmento”

1. Slot com `renderer` + `lodState.Ready()`.
2. `faceFamilyIds` preenchidos a partir do RDR.
3. `currentFaceSlots` com texturas VDP1 válidas (`ApplyFaceTextureSlotsGlobal`).
4. Entrada na lista ordenada (profundidade / painter).
5. `renderer->Render(light, camera)`.

Se (3) falha e o caminho **determinístico** não repara slots, `LastDrawnMeshes()` pode ficar 0 → **invisível sem crash**. Com `kEnableBenchmarkProfile` / stats off, falhas de textura podem ser **silenciosas**.

### 3.4 Background (VDP2)

- `BackgroundManager` / `SkyEnvironment` — independente da esteira VDP1.
- Compete por HWR/Cart no boot com load de packs de pista; falha de céu ≠ falha de slot de segmento, mas o sintoma “só o carro” pode misturar os dois.

---

## 4. Movimentação da “esteira” (streaming da pista)

### 4.1 Modelo

A pista **não** carrega 290 segmentos desenháveis ao mesmo tempo. Mantém um **anel fixo de 20 slots** em LWR:

| Estado | Função |
|--------|--------|
| `segmentRenderers_[0..19]` | Pool físico reutilizado |
| `activeWindowHead_` | Índice físico do rank 0 |
| `activeWindowStartId_` | ID do segmento no rank 0 |
| `windowDirection_` | `+1` ou `−1` (sentido do anel) |
| `targetWindowStartId_` | Meta do catch-up por slides |
| `observedCarSegmentId_` | Segmento reportado pela física |

```
physicalIndex = (activeWindowHead_ + logicalRank) % 20
```

### 4.2 Posição do carro na esteira

`ResolveWindowStartFromCarSegment(car, dir)`:

- `dir +1` → `start = car − 1` → ranks `[car−1, car, car+1, …]`  
- `dir −1` → `start = car + 1` → ranks `[car+1, car, car−1, …]`  

Objetivo: carro no **2º slot (rank 1)** — densos lod_0.

### 4.3 Construção inicial

`RebuildActiveSegmentWindow(start, 20, dir)`:

```
for rank in 0..19:
  id = wrap(start + rank * dir)
  BuildSegmentIntoRenderer(id, slot[rank], designGeo(rank))
  tenta remap de texturas por LOD do rank
```

Usizado no boot / repair. **Não** deve ser o caminho de cada 180° sob pressão de HWR (histórico de wipe).

### 4.4 Slide (avanço O(1) da esteira)

1. **Drop** = segmento que sai “atrás” da marcha (`ResolveWindowOutgoingSegmentId`).
2. **Incoming** = próximo à frente (`ResolveWindowIncomingSegmentId` / prefetch).
3. Build no scratch/prefetch → commit no slot reciclado.
4. Atualiza `activeWindowStartId_` e head.
5. Prefetch do próximo ID no sentido de `windowDirection_`.

Catch-up no `BeginFrame`: se `start ≠ target`, `backlog` na direção do slide; 1–3 slides/frame. Se `backlog ≥ total/2`, o código **zera** backlog (anti-wrap) — isso pode **estagnar** reverse mal alinhado.

### 4.5 Direção do anel

- Hoje: `UpdateCameraDrivenWindowDirection` usa **câmera vs trackForward** (histerese 3 frames + cooldown).
- Sob estabilização, flip prefere `target + slides` a rebuild full.
- **Viagem do carro** como fonte primária de `dir` ainda não é o núcleo estável (experimentos recentes foram revertidos).

### 4.6 Draw vs stream

- Stream só decide **quais IDs** estão nos 20 slots.
- Draw ordena por câmera e submete VDP1.
- Inverter a esteira = mudar **dir / start / drop / incoming**, não reescrever o painter.

### 4.7 Diagrama mental

```
IDA  dir=+1, carro C:
  ranks: [C-1][ C ][C+1] ... [C+18]
  slide: drop C-1 → load C+19

VOLTA dir=-1 (alvo conceitual):
  ranks: [C+1][ C ][C-1] ... 
  slide: drop fim do arco → load próximo no sentido −1
```

---

## 5. Problemática: declives e coerência com a topologia

### 5.1 Sintomas observados

1. Em **algumas faces** do declive (ex.: região 28–42 e lajes similares), as rodas **não encontram** o asfalto no frame.
2. Sem hit em **frente e trás** ao mesmo tempo, o `CarWheelRig` **não atualiza pitch** (fica no hold ou “achatado”).
3. O corpo pode **descer em Y** pela adesão, mas **sem inclinação 3D** coerente com o plano da pista.
4. Na descida, a **câmera** (boom chase) pode **entrar nas faces** do asfalto se o pitch do body sobe e o lift vertical não acompanha.
5. Se o carro sai dos ranks 0–1 da esteira, probes batem em malha mais grossa → mais miss e pior topologia.

### 5.2 Por que o MapHeight falha em lajes

| Fator | Efeito |
|-------|--------|
| Costuras entre faces / segmentos | Sample “cai no buraco” entre triângulos |
| Seed de segmento errado (atraso da esteira) | Strict miss; soft fallback com afinidade 0 rejeitado |
| Probe no canto externo da roda | Fora da faixa asfalto (escape/parede) |
| LOD baixo sob o carro | Faces grandes, altura “em degrau” |
| Cadência de probe reduzida (low-cost) | Amostra atrasada no declive rápido |
| Sem hold por canto em miss | Um frame sem hit zera o par F/R → sem pitch |

### 5.3 Desacoplamento Y vs atitude

Hoje:

```
bodyY  ← média / plano dos cantos válidos + ride + adesão
pitch  ← só se frontValid && rearValid no mesmo frame
roll   ← só se leftValid && rightValid
```

Isso permite:

- Corpo “correto” em altitude média, mas **horizontal** se um eixo falhou.
- Ou pitch **travado** no último hold enquanto o asfalto já mudou (atraso visual).

Para coerência topológica, o alvo arcade (Ridge / Driver / SEGA) é:

> **Um único plano** das 4 rodas define **altura do chassis e pitch/roll** no mesmo frame; miss curto usa **contato sticky** (último Y válido por roda), não “desliga” o plano.

### 5.4 Câmera no declive

Mecanismos existentes (`camera_system`):

- Pitch suave a partir de `bodyPitch` / grade.
- Boom com rotação local (Y,Z) pelo pitch.
- Clearance extra e `ApplyGradeBehindClearance`.
- **Sem** MapHeight na câmera (correto para Saturn).

Lacuna típica: após **lerp** do chase, o hard floor de altura pode não ser reaplicado → boom “afunda” no asfalto mesmo com pitch certo no target.

### 5.5 Esteira e topologia

```
Carro em rank ≥ 2  →  malha L / faces grandes  →  MapHeight pior  →  pitch pior
Carro em rank 0–1 →  lod_0 denso              →  topologia melhor
```

Qualquer atraso de slide no declive **compõe** o problema de física visual, mesmo com probes perfeitos em teoria.

### 5.6 O que **não** é a solução no Saturn

| Abordagem moderna | Por quê evitar |
|-------------------|----------------|
| Full rigid body + solver de contatos | Orçamento SH2 / frame |
| Raycast MapHeight na câmera todo frame | Custo + histórico de instabilidade |
| Rebuild de 20 segmentos a cada correção de janela | Wipe de pista sob HWR |
| Dual mesh swap no slot vivo sem scratch | Blank de cenário |

### 5.7 Direção desejada (resumo de desenho)

1. **Probes:** 4 cantos; retry seed±1 e probe inward no miss; hold sticky por roda (N frames).  
2. **Plano único:** bodyY + pitch + roll do mesmo sample set.  
3. **Adesão:** descer rápido no declive; não empinar no climb; anti-penetração.  
4. **Esteira:** manter carro em ranks 0–1 enquanto sobe/desce (só slides).  
5. **Câmera:** lift vertical ∝ pitch nariz-baixo + hard floor pós-smooth; zero ray de pista.  
6. **Assets:** faces densas e costuras soldadas no declive (já em subdivisão) reduzem miss na origem.

---

## 6. Mapa de arquivos (consulta rápida)

| Tema | Caminhos |
|------|----------|
| Movimento / input | `src/car_system.*`, `src/game_loop_system.hpp` |
| Dinâmica | `src/car_dynamics_model.hpp`, `src/car_physics_v2*.hpp` |
| Solo | `src/car_ground_follower.hpp`, `src/car_physics_shared.hpp` |
| Pitch visual | `src/car_wheel_rig.*`, `src/mesh_renderer.*` |
| Câmera | `src/camera_system.*`, `src/camera_safety.hpp` |
| Esteira / draw | `src/track_system.cxx` / `.hpp` |
| Flags física | `src/physics_feature_flags.hpp` |
| Boot / lift | `src/main.cxx` |

---

## 7. Estado e prioridades (pós-reset)

| Área | Estado no código atual | Prioridade de refinamento |
|------|------------------------|---------------------------|
| Movimento XZ / câmbio | Maduro, arcade | Baixa |
| Esteira ida (slide O(1)) | Funcional em princípio | Média (estabilidade dual mesh / texturas) |
| Render segmentos | Dependente de RDR + slots VDP1 | Alta se blank; senão média |
| Solo em **declive** | Funciona em parte; miss de face + pitch fraco | **Alta** |
| Câmera no declive | Parcial (pitch/lift); risco de furo no asfalto | **Alta** |
| Reverse 180° / esteira | Parcial (dir por câmera); não é o foco agora | Adiada |

---

## 8. Conclusão

O Interlagos_racing separa bem três motores:

1. **Dinâmica arcade XZ** do carro.  
2. **Solo MapHeight + plano de rodas** para Y e atitude.  
3. **Esteira de 20 slots** para streaming/render da pista.

A coerência em **declives** depende da cadeia:

```
esteira (lod_0 sob o carro)
  → MapHeight nos 4 cantos
    → bodyY + pitch/roll do mesmo plano
      → câmera sobe com o pitch
```

O elo fraco atual é o **contato intermitente com faces de asfalto** e a **atitude 3D** quando um eixo perde o sample — não a falta de um motor de física “completo”. Qualquer plano de ação seguinte deve atacar esse elo **sem** reabrir reverse de esteira até a topologia de descida e a câmera estarem estáveis.

---

*Documento gerado a partir da leitura do código em 2026-08-02. Não substitui os ACTION_PLAN_* de implementação; serve como mapa mental e registro da problemática de topologia.*
