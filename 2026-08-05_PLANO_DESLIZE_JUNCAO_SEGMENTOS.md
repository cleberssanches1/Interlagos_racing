# Plano de ação — Deslize contínuo no declive (anti-escada nas junções)

**Data:** 2026-08-05  
**Vídeo:** `Yabause v0.9.14 2026-08-05 22-48-57.mp4`  
**Sintoma:** no declive o carro (1) desce ao entrar no segmento, (2) **anda em linha reta** dentro do segmento, (3) **desce de novo** na junção → sensação de escada.  
**Alvo:** deslizar com o asfalto de forma contínua (heave + pitch), sem ignorar junções.

---

## 1. Diagnóstico (vídeo + código)

### 1.1 Comportamento observado

| Fase | O que se vê |
|------|-------------|
| Entrada no declive | Alinha à face (perde altura de uma vez) |
| Dentro do segmento | Trajetória **retilínea** em Y (não acompanha a rampa) |
| Troca de segmento | Novo “degrau” de descida para a face seguinte |

Isso é o padrão **MapHeight por laje plana** + **ancoragem rígida no Y da face**.

### 1.2 Causa no código (estado atual)

```
XZ move livre
    → MapHeight nos cantos (Y ≈ constante dentro da face/segmento)
    → surfaceYTarget = média + ride
    → adesão cola o body nesse Y
    → F−R ≈ 0 no slab → gradeTan → 0 → sem dy contínuo
    → junção: Y da face salta → degrau
```

Peças responsáveis:

| Peça | Problema |
|------|----------|
| `kEnableGradePredictY = false` | Nenhum `dy = tan·speed` contínuo |
| Target = só MapHeight | Dentro do slab o target **não desce** com o movimento |
| Face-switch reject (3 samples) | Atrasa aceitar a face da junção → depois **snap** |
| Seed preso no segmento antigo | Probe erra a face do próximo segmento |
| `surfaceYInitialized = false` no miss | Descola e reancora em degrau |
| Pitch só de F−R instantâneo | No slab plano o chassis fica **horizontal** |

**Conclusão:** não basta “colar no MapHeight”. É preciso **integrar a grade entre amostras** e tratar a junção como continuação da rampa, não como reset de altura.

---

## 2. Modelo alvo — “deslize na rampa”

A cada frame, com suporte de solo e grade válida:

```
measuredY  = média(MapHeight cantos) + ride
gradeTan   = filtro(F−R)  com HOLD se F−R zerar no slab
gradeDy    = gradeTan · forwardSpeed     (Y-down: + = desce)

se measured ≈ lastMeasured (mesma laje):
    targetY = lastTargetY + gradeDy      // continua o deslize
    se measured mais fundo que target → puxa suavemente para measured (junção antecipada)
    se measured mais alto (penetração) → sobe para measured
senão (mudou de verdade):
    targetY = blend(lastTarget + gradeDy, measured)
bodyY → segue targetY de forma contínua (sem snap de laje)
pitch → segue gradeTan mesmo com F−R≈0 no slab
```

Junções: probe com **seed±1 / seed+1**, aceitar **descida** na hora (sem multi-reject), **não** zerar heave no miss de 1 frame.

---

## 3. Fases de implementação

| ID | Ação | Arquivos | Gate |
|----|------|----------|------|
| **S0** | Plano + telemetria (já há overlay g/dY/m) | MD | — |
| **S1** | Grade hold + `lastForwardSpeed` + heave contínuo `tan·v` | `car_physics_shared`, `car_ground_follower` | Dentro do segmento o Y **desce continuamente** |
| **S2** | Junção: seed±1, accept descent, sticky heave | `car_arcade_suspension`, `car_ground_follower` | Sem degrau nítido na troca de seg |
| **S3** | Pitch a partir da grade hold no slab | `car_ground_follower` (publica F/R sintéticos) ou wheel rig | Nariz acompanha a rampa no meio do segmento |
| **S4** | Validar no Yabause (mesmo trecho do vídeo) | — | Deslize sem escada |

Fora de escopo: densificar asset, reverse belt, dual-mesh.

---

## 4. Critério de sucesso

1. No meio de um segmento de declive: bodyY **varia a cada frame** com a velocidade (não fica plano).  
2. Na junção: transição **suave** (sem degrau de 1 frame).  
3. Pitch **não zera** no meio do slab se a rampa continua.  
4. Sem enteramento prolongado no asfalto (anti-penetração mantida).

---

## 5. Status

| Fase | Estado |
|------|--------|
| S0 | feito (este doc) |
| S1 | feito — grade hold + `tan·v` + look-ahead |
| S2 | feito — seed±1, accept descent, sticky heave |
| S3 | feito — F/R sintéticos com grade hold no slab |
| S4 | pendente (rebuild + vídeo novo) |
