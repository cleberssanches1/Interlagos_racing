# Camera + Physics: Comparação PS1 e Plano Estrutural

**Projeto:** Interlagos_racing (Sega Saturn / SaturnRingLib)  
**Data:** 2026-07-14  
**Objetivo deste doc:** registrar pesquisa e um plano estrutural para evolução futura da câmera e da movimentação, sem misturar com micro-passos de SH2/custo.

---

## 1. Problema observado (câmera)

Sintoma: **ao acelerar, a câmera fica para trás** — parece mais lenta que o carro.

### Causa no código (estado antes do hotfix)

Em `camera_system.cxx`:

1. Posição ideal: `target = carWorldPosition + offset(heading)`.
2. Aplicação: `cam = lerp(camPrev, target, alpha)` com **`alpha` fixo ≈ 0.30**.
3. `ChaseResponsePreset` (Loose/Rigid) era **configurado em `main.cxx` mas não usado** em `CameraFollowBlendRaw()`.

Com blend exponencial e velocidade alta, o atraso em regime é da ordem:

\[
\text{lag} \approx \Delta x_{\text{frame}} \cdot \frac{1-\alpha}{\alpha}
\]

Com \(\alpha = 0.3\): lag ≈ **2,3× o deslocamento por frame** → a câmera “escorrega” para trás na reta.

Isso é **bug de follow**, não “estilo GT” intencional.

### Hotfix imediato (aplicado nesta sessão)

- `CameraFollowBlendRaw()`:
  - usa **Rigid / Loose** de verdade;
  - base mais alta (≈0,70 / 0,60);
  - **boost com `movementSpeedNorm`** para não aumentar lag com a velocidade.
- Default em `main.cxx`: **Rigid** (antes Loose).

---

## 2. Como jogos de corrida PS1 (e arcade) resolvem isso

Referências típicas: **Ridge Racer**, **Gran Turismo 1/2**, **Destruction Derby**, **Wipeout**, **Daytona** (arcade → lógica de chase).

| Abordagem | Onde aparece | Comportamento |
|-----------|--------------|---------------|
| **Attach rígido** | Ridge Racer (muitos modos), arcade | `cam = car + R(yaw)*offset`. Zero lag de posição. |
| **Lag só no yaw / look** | GT, alguns modes RR | Posição quase rígida; suaviza rotação e look-ahead. |
| **Mola/amortecedor (spring-damper)** | GT-like, sims | Força proporcional ao erro de posição + damping de velocidade da câmera. |
| **Look-ahead por velocidade** | Quase todos | Alvo de olhar mais longe em alta velocidade. |
| **PATH-guided** | Daytona-like, alguns ports | Heading e alvo vêm da pista, não do yaw ruidoso do carro. |

### O que **não** é boa prática em chase de corrida

- `lerp` com alpha baixo **fixo** na **posição** da câmera (cria lag proporcional à velocidade).
- Suavizar posição e heading com o mesmo alpha fraco.
- Câmera que “persegue” o carro como um AI agent lento (estilo 3ª pessoa de ação).

### Melhor lógica para Interlagos (arcade Saturn)

Ordem de preferência estrutural:

1. **Posição:** attach cinemático ao carro (`car + offset(heading)`), com alpha alto ou 1,0.
2. **Heading:** suavizar yaw levemente (anti-jitter de steering), não a posição.
3. **Look-ahead:** escalar com velocidade (já parcialmente feito).
4. **Opcional futuro:** PATH-guided (`CAMERA_LOGIC_PLAN.md`) para curvas/segmentos.
5. **Spring-damper** só se quiser “peso de câmera” de sim; não como default de arcade.

Conclusão: o modelo atual (offset + lerp) é **aceitável**, mas o alpha antigo **não** era a melhor lógica possível. O hotfix aproxima o padrão PS1/arcade. O plano estrutural abaixo fecha a diferença com GT/Daytona.

---

## 3. Física e movimentação — comparação rápida

### Nosso stack atual

- Master: física sync (`enableSlaveSimulation=false`).
- Fixed-point, 1 step/frame (`PHYS_SATURN_LOW_COST`).
- Planar integrate + ground/wall probes + grip.
- Não é motor rigid-body genérico (tipo PS1 com GTE full sim).

### PS1 típico

| Título | Movimentação | Câmera |
|--------|--------------|--------|
| Ridge Racer | Arcade: grip/drift simplificado, resposta rápida | Chase rígido / semi-rígido |
| Gran Turismo | Mais sim: pneus, peso, freio | Chase com mais inércia, mas posição acompanha velocidade |
| Wipeout | Alta velocidade, “rail + thruster” | Lag cinematográfico intencional (não nosso caso) |

### Estamos na melhor lógica possível?

| Área | Hoje | Avaliação |
|------|------|-----------|
| Integração planar no SH2 | Adequada para arcade | **Boa** |
| Probes de chão/parede | Low-cost + cadências | **Boa** (custo); qualidade ok |
| Câmera posição | Lerp fraco (pré-fix) | **Ruim** → hotfix melhora |
| Câmera vs PATH | `kPathGuidedChaseEnabled = false` | **Incompleta** para Daytona-like |
| Dual SH2 na física | Sim na Master (estável) | **Correto** para estabilidade |
| Sync áudio/visual | Commit no mesmo frame | **Boa** com sim Master |

**Não** precisamos copiar GT1 para “ficar certo”. Para corrida arcade Saturn, a lógica ideal é **Ridge/Daytona-like** (attach rígido + look-ahead + PATH opcional), não câmera com lag de 3ª pessoa.

---

## 4. Plano de ação estrutural (futuro)

Implementar em PRs separados, cada um com boot estável + ISO pin.

### PR-A — Camera follow “arcade-correct” (curto)

1. Manter/validar hotfix de blend + Rigid default.
2. Opção `kChasePositionRigid = true`: skip lerp de posição (alpha=1) em ChaseNear/Far.
3. Suavizar **apenas** heading (já existe `kHeadingSmoothBlendRaw`); calibrar.
4. Telemetria debug: `posErr = |cam - target|` e `speedNorm` on-screen.

**Aceite:** em reta full throttle, `posErr` estável e pequeno; sem “câmera ficando para trás”.

### PR-B — Look-ahead e framing por velocidade (médio)

1. Unificar look-ahead dinâmico com offset de distância atrás (atrás um pouco mais em alta velocidade, se desejado).
2. Presets ChaseNear/Far com curvas de distância vs speed.
3. Evitar look-ahead que “vire” a câmera para o lado do movimento lateral em drift.

**Aceite:** carro permanece enquadrado; curvas sem orbitação.

### PR-C — PATH-guided chase (médio/alto)

Base: `CAMERA_LOGIC_PLAN.md`.

1. Ligar `kPathGuidedChaseEnabled` com flag de build.
2. `forward` e look target do PATH; offset no frame do PATH.
3. Fallback para yaw do carro se PATH inválido.

**Aceite:** sem câmera invertida em troca de segmento; alinhamento carro/câmera com a pista.

### PR-D — Spring-damper opcional (baixo prioridade)

1. `camVel += (target - cam) * kSpring - camVel * kDamp`.
2. Só se PR-A/B não derem o “peso” desejado.
3. Manter clamp de distância máxima ao carro (nunca deixar o carro sair do frame).

### PR-E — Física estrutural (separado da câmera)

1. Contrato explícito: plano de contato (Y) vs planar (XZ/yaw).
2. Queries de pista com seed/cache (já parcialmente feito).
3. **Não** reintroduzir sim async na Slave sem redesenho de contensão de job.
4. Opcional: extrapolar posição visual 1 frame para câmera se um dia a sim voltar a N−1.

---

## 5. Ordem recomendada de execução

| Ordem | PR | Risco | Dependência |
|-------|-----|-------|-------------|
| 1 | A (follow rígido/blend) | Baixo | — |
| 2 | B (look-ahead/dist) | Baixo/médio | A |
| 3 | C (PATH) | Médio | PATH runtime estável |
| 4 | E (física) | Médio | — |
| 5 | D (spring) | Médio | só se necessário |

---

## 6. Métricas de aceite globais

- Reta full throttle: câmera **não** acumula atraso perceptível.
- Curva fechada: sem orbitar o carro; sem invert yaw.
- Freio/parede: framing estável.
- ISO pin e sem invalid opcode (política de headers leves).
- FPS não piora vs baseline dual-SH2 atual.

---

## 7. Arquivos relevantes

| Arquivo | Papel |
|---------|--------|
| `src/camera_system.cxx` / `.hpp` | Chase, blend, heading, look-ahead |
| `src/main.cxx` | Preset response / distância chase |
| `src/camera_controller.hpp` / orbit | Órbita (desligada no chase arcade) |
| `CAMERA_LOGIC_PLAN.md` | PATH-guided detalhado |
| `src/car_physics_v2.hpp` / `car_ground_follower.hpp` | Movimentação / probes |
| `SH2_FPS_BALANCE_ACTION_PLAN.md` | Split Master/Slave (contexto) |

---

## 8. Resumo executivo

| Pergunta | Resposta |
|----------|----------|
| Por que a câmera ficava para trás? | Lerp de posição com alpha baixo fixo (0,30), lag ∝ velocidade. |
| PS1 faz melhor? | Sim: attach rígido ou lag só em rotação; look-ahead por speed. |
| Melhor lógica para nós? | Arcade attach + heading suave + look-ahead; PATH depois. |
| Hotfix agora? | Blend mais alto + escala por speed + Rigid default. |
| Estrutural depois? | PRs A→B→C (e E/D se precisar), neste documento. |
