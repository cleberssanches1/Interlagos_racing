# Hotshot Racing — Lógica de Controle de Câmera

**Fonte de análise:** `HotshotRacing.exe` (strings + análise estrutural), `SlipstreamCameraEffects.dat` (SSRT binário), `cameras.zml` (binário cifrado — veja nota de formato).  
**Motor:** Summit Engine (Sumo Digital)  
**Data da extração:** 2026-04-19

---

## Nota sobre os Arquivos

| Arquivo | Formato | Legível? |
|---------|---------|----------|
| `cameras.zml` | Binário cifrado proprietário (entropia ~7.6) | Não — requer rotina de decriptação compilada no engine |
| `SlipstreamCameraEffects.dat` | SSRT (Summit Serialization Runtime) | Parcial — strings e floats extraídos |
| `HotshotRacing.exe` | PE64 | Strings ASCII extraídas — fonte primária deste documento |

Os valores numéricos concretos de `cameras.zml` não são acessíveis sem desmontagem do executável. Este documento documenta a **estrutura completa e semântica** de cada parâmetro inferida via análise das strings do executável, com valores típicos estimados baseados em comportamento observável do jogo.

---

## 1. Arquitetura do Sistema de Câmera

```
SeCameraManager
  └── SeDefinitionCameraNode / SeInstanceCameraNode
        ├── Camera_Chase          (câmera traseira padrão)
        ├── Camera_Chase_2        (ângulo alternativo traseiro)
        ├── Camera_Cockpit        (câmera interna no cockpit)
        ├── Camera_Bonnet         (câmera no capô)
        ├── Camera_Bumper         (câmera no para-choque frontal)
        ├── Camera_Rear           (câmera traseira, olhando para trás)
        ├── Camera_Weapon_Chase   (câmera de arma/alvo)
        └── Camera_RollingStart   (câmera de largada progressiva)
```

Cada modo de câmera possui um conjunto completo de parâmetros abaixo. Câmeras compartilham a base do sistema de mola/amortecimento mas têm configurações distintas de offset, FOV e comportamento de roll.

### Câmeras Especiais

| Câmera | Uso |
|--------|-----|
| `TrackIntroCameras` | Câmeras cinematográficas na apresentação da pista |
| `GridCameras` | Câmeras na largada (grid) |
| `TrackPreviewCamera` | Visualização da pista no menu |
| `Versus_Camera` | Câmera de tela dividida no modo versus |
| `Replay_Camera` | Câmera automática no replay |
| `Free_Camera` | Câmera livre de debug |
| `Orbital_Camera` | Câmera orbital (tela de seleção) |
| `Custom_Camera` | Câmera customizada por evento |
| `IslandCamera` | Câmera de visão geral do ambiente |
| `MiniMapCamera` | Câmera ortográfica do minimapa |

---

## 2. Sistema de Posição e Distância

O `TrackSystem` de câmera posiciona a câmera em relação ao carro usando um conjunto de offsets em coordenadas locais e globais, modificados dinamicamente pela velocidade, aceleração e inclinação do terreno.

### Offsets Base

| Parâmetro | Descrição |
|-----------|-----------|
| `fOffsetX` / `LocalOffsetX` / `GlobalOffsetX` | Offset lateral (eixo X) da câmera em relação ao carro |
| `fOffsetY` / `LocalOffsetY` / `GlobalOffsetY` | Offset vertical (eixo Y) |
| `fOffsetZ` / `LocalOffsetZ` / `GlobalOffsetZ` | Offset longitudinal (frente/trás, eixo Z) |
| `vLocalCarOffset` | Vetor 3D de offset completo no espaço local do carro |
| `fStartDistanceFromCentre` / `m_fStartDistanceFromCentre` | Distância inicial da câmera ao centro do carro |
| `fBaseElevation` / `ms_fBaseElevation` | Elevação base da câmera acima do carro |
| `fHeightAdjust` | Ajuste de altura adicional (dinâmico) |
| `heightOffset` / `fPosYOffset` | Offset de altura secundário |
| `vLeanOffset` | Offset aplicado durante inclinação do carro (curvas banked) |
| `PositionOffset` | Offset de posição global do nó de câmera |
| `PositionRacerOffset` | Offset em relação à posição do piloto (para cockpit) |

### Offsets Dinâmicos (Velocidade/Aceleração)

| Parâmetro | Descrição |
|-----------|-----------|
| `fDistanceOffset` | Offset de distância adicional base |
| `fMaxDistOffsetOnAccel` | Máximo recuo da câmera durante aceleração intensa (câmera "puxa para trás") |
| `fMaxDistOffsetOnBrake` | Máximo avanço da câmera durante frenagem (câmera "empurra para frente") |
| `fMaxDistDeltaOffsetUphill` | Ajuste de distância em subidas |
| `fMaxDistDeltaOffsetDownhill` | Ajuste de distância em descidas |
| `fMaxDistHeightOffsetUphill` | Ajuste de altura em subidas |
| `fMaxDistHeightOffsetDownhill` | Ajuste de altura em descidas |
| `OffsetByPureSpeedZ` | Offset longitudinal proporcional à velocidade pura (sem direção) |
| `OffsetMultiplierX` / `OffsetMultiplierZ` | Multiplicadores de offset por eixo |
| `OffsetClampX` / `OffsetClampZ` | Limites máximos do offset dinâmico |
| `fMaxOffset` / `fMinOffset` | Clamp global do offset resultante |
| `fFallingHeightOffset` | Offset de altura quando o carro está caindo (pós-rampa) |
| `fRisingHeightOffset` | Offset de altura quando o carro está subindo uma rampa |
| `targetHeightOffset` | Offset de altura do ponto de interesse (look-at target) |

### Visibilidade por Modo de Câmera

| Parâmetro | Descrição |
|-----------|-----------|
| `ShowInCameraChase` | Objeto visível na câmera Chase |
| `ShowInCameraCockpit` | Objeto visível na câmera Cockpit |
| `ShowInCameraBonnet` | Objeto visível na câmera Bonnet |
| `ShowInCameraBumper` | Objeto visível na câmera Bumper |
| `ShowInCameraRear` | Objeto visível na câmera Rear |
| `ShowInCameraOther` | Objeto visível em outros modos de câmera |

---

## 3. Sistema de Mola e Amortecimento (Spring/Damper)

O núcleo do sistema de câmera é um **spring-damper** de segunda ordem que suaviza o seguimento do carro. O comportamento varia conforme estado (normal, entrando em curva, saindo de curva, aterrissando).

### Parâmetros Principais de Lag (Atraso de Seguimento)

| Parâmetro | Descrição | Valor Típico |
|-----------|-----------|-------------|
| `fLagStiffness` | Rigidez da mola de lag (maior = segue mais rápido) | 8.0–20.0 |
| `fLagDamping` | Amortecimento do lag (maior = menos oscilação) | 0.7–1.2 |
| `bAllowUpLag` | Habilita lag vertical (câmera não sobe instantaneamente) | true |

### Molas por Contexto

| Parâmetro | Descrição |
|-----------|-----------|
| `m_fDefaultSpring` / `DefaultSpringMul` | Mola base (estado normal) |
| `m_fEnterSpring` / `EnterSpringMul` | Mola ao entrar em estado especial (deriva, boost) |
| `m_fExitSpring` / `ExitSpringMul` | Mola ao sair do estado especial |
| `fSpringStrRestore` | Força de restauração da mola (retorno à posição padrão) |
| `fSpringDampRestore` | Amortecimento durante restauração |
| `SpringAcceleration` | Aceleração máxima da mola |
| `Springiness` | Fator global de elasticidade |
| `bAllowAccelSpring` | Habilita mola adicional durante aceleração |
| `bAllowJumpOffset` | Habilita offset especial ao detectar salto |

### Amortecimento Lateral e Frontal

| Parâmetro | Descrição |
|-----------|-----------|
| `m_ForwardDampingFromTime` | Damping frontal baseado em tempo (evita pitch em frenagens bruscas) |
| `m_SideDampingAccel` | Damping lateral durante aceleração (evita deriva lateral da câmera) |
| `m_SideDampingAccelBoost` | Damping lateral adicional durante boost |
| `m_SideDampingAccelOppSteer` | Damping lateral em direção oposta à esterçagem |
| `m_SideDampingNoAccel` | Damping lateral sem aceleração (costuma ser menor) |

### Impacto e Colisão

| Parâmetro | Descrição |
|-----------|-----------|
| `fBumpSpringStr` | Força da mola de bump (absorção de impactos do terreno) |
| `fBumpDampingWrtCritical` | Amortecimento do bump como fração do amortecimento crítico |
| `vBumpMaxSpringVel` | Velocidade máxima da mola de bump (limita reposta a impactos extremos) |
| `fImpactDampingIn` | Amortecimento na entrada do impacto (colisão com obstáculo) |
| `fImpactDampingOut` | Amortecimento na saída do impacto |
| `fImpactMaxSpringSpeed` | Velocidade máxima de resposta da mola de impacto |
| `fImpactMinSpringSpeed` | Velocidade mínima de resposta da mola de impacto |

### Aterrissagem após Salto

| Parâmetro | Descrição |
|-----------|-----------|
| `fLandingSpringStrength` | Força da mola de aterrissagem (bounce visual ao pousar) |
| `fLandingSpringDampening` | Amortecimento horizontal do bounce de aterrissagem |
| `fLandingSpringUpDampening` | Amortecimento vertical do bounce de aterrissagem |
| `fLandingHeightTimeForMaxOffset` | Tempo (s) em queda para atingir offset máximo de aterrissagem |
| `fLandingHeightTimeForMinOffset` | Tempo (s) em queda para offset mínimo (pouso curto) |

---

## 4. Campo de Visão (FOV)

O FOV varia dinamicamente com a velocidade do carro para transmitir sensação de aceleração. Boost aplica um delta adicional ao FOV.

### Curva de FOV por Velocidade

```
FOV
 |   fMaxFov ─────────────────────────────  ←── velocidade ≥ fFovMaxSpeed
 |          /
 |         /  curva linear (ou customizável)
 |        /
 | fMinFov ──── ←── velocidade ≤ fFovMinSpeed
 |__________________________ Velocidade (MPH/KPH)
     fFovMinSpeed   fFovMaxSpeed
```

| Parâmetro | Descrição | Valor Típico |
|-----------|-----------|-------------|
| `fFovAtMinSpeed` / `fMinFov` | FOV mínimo (parado ou velocidade mínima) | 60–70° |
| `fFovAtMaxSpeed` / `fMaxFov` | FOV máximo (velocidade máxima) | 90–110° |
| `fFovMinSpeed` | Velocidade abaixo da qual FOV é mínimo (MPH) | 0–30 MPH |
| `fFovMaxSpeed` | Velocidade acima da qual FOV é máximo (MPH) | 150–200 MPH |
| `fBoostDeltaMulFOV` | Multiplicador de delta de FOV durante boost | 0.1–0.3 |
| `DefaultFOVLerpBackSpeed` | Velocidade de retorno do FOV ao padrão (após soltar boost) | 5–15°/s |
| `fFovAdjustScaleDistance` | Escala de FOV baseada em distância (para câmera com zoom) | — |
| `fPreserveFOVDistance` | Distância mínima de câmera para preservar FOV correto | — |
| `fGenFocalDistance` | Distância focal geral para DOF (depth of field) | — |
| `fSpeedFocalDistance` | Distância focal ajustada por velocidade | — |
| `VerticalFov` / `FovDegs` | FOV vertical fixo (alguns modos de câmera) | — |
| `CameraFOV` | FOV global da câmera (override do modo) | — |
| `CameraTanHalfFOV` | tan(FOV/2) — otimização interna (pré-computado) | — |

---

## 5. Sistema de Look-Ahead (Mirada Antecipada)

A câmera não aponta exatamente para o carro — aponta ligeiramente à frente, na direção de onde o carro está indo. Isso cria a sensação de "ver aonde vai".

### Parâmetros de Target

| Parâmetro | Descrição |
|-----------|-----------|
| `fSteerTargetDist` | Quanta distância à frente a câmera olha durante esterçagem normal |
| `fDriftTargetDist` | Distância de look-ahead durante drift (geralmente menor — câmera mais "presa") |
| `fTargetOffsetAmount` | Magnitude do offset lateral do target |
| `fTargetOffsetAngle` | Ângulo do offset do target em relação à direção do carro |
| `fTargetOffsetY` | Offset vertical do ponto de mira |
| `fTargetRefreshTime` | Taxa de atualização do target (s) — valores maiores criam lag intencional |
| `fTargetSpeedMultiplier` | Multiplicador do look-ahead por velocidade |
| `fTargetSpread` | Spread/variação do target para efeito de câmera mais "nervosa" |
| `fTargetBlendSpeed` | Velocidade de blend entre targets (suavização de transições) |
| `ms_fTargetZ` | Posição Z do target atual (estado runtime) |
| `ms_fLookAngle` | Ângulo de look atual (estado runtime) |
| `m_fLookAngleThreshold` | Limiar de ângulo mínimo para ativar look-ahead |
| `m_fYawLookSteerScale` | Escala do yaw de look pela esterçagem |
| `vTarget` | Vetor 3D do ponto de mira atual (runtime) |

### Controles Manuais de Look (Player Input)

| Parâmetro | Descrição |
|-----------|-----------|
| `LookBack` | Câmera olha para trás (botão) |
| `LookLeft` | Câmera olha para a esquerda (botão) |
| `LookRight` | Câmera olha para a direita (botão) |
| `LookVelBlend` | Blend entre look direcionado e velocidade do veículo |
| `CameraLookMod` | Modificador de sensibilidade do look manual |
| `CameraLookLerp` | Fator de lerp para suavizar o look manual |
| `AroundHumanAheadDistance` | Distância à frente ao circular em torno do piloto |
| `AroundHumanBehindDistance` | Distância atrás ao circular em torno do piloto |

---

## 6. Controle de Roll (Inclinação Lateral)

A câmera inclina ("bancar") na direção das curvas para reforçar a sensação de velocidade. O roll é proporcional ao ângulo de esterçagem e à intensidade da deriva.

```
Roll total = fRollAngleFromSteering × steerInput
           + fRollAngleFromDrifting × driftIntensity
           (limitado a ±RollLimitDEG)
```

| Parâmetro | Descrição | Valor Típico |
|-----------|-----------|-------------|
| `fRollAngleFromSteering` | Graus de roll por unidade de esterçagem | 3–8° |
| `fRollAngleFromDrifting` | Graus de roll adicionais durante drift | 2–5° |
| `RollLimitDEG` | Limite máximo de roll (clamp) | 8–15° |
| `fRollFwdSpeedForMaxRoll` | Velocidade (MPH) para atingir roll máximo | 100–150 MPH |
| `fRollFraction` | Fração do roll máximo aplicada (0–1) | 0.8–1.0 |
| `fRollScale` | Escala global do roll | 1.0 |
| `fRollRadsSec` | Taxa de mudança do roll em rad/s | 2–4 rad/s |
| `RollDampRatePs` | Taxa de damping do roll em graus/s | 30–60°/s |
| `RollBlendTime` | Tempo de blend do roll (s) | 0.1–0.3s |
| `RollMul` | Multiplicador do roll final | 1.0 |
| `AdditionalCameraRoll` | Roll adicional fixo (para câmeras inclinadas) | 0° |
| `ExtraRoll` | Roll extra contextual (rampa, terreno banked) | variável |
| `IgnoreRoll` | Flag: câmera ignora roll completamente (ex: câmera de replay) | false |
| `m_bRollStabiliser` | Ativa estabilizador de roll (atenua roll em saltos/impactos) | true |
| `m_fAntiRoll` | Fator de anti-roll (0 = roll livre, 1 = roll completamente cancelado) | 0–0.3 |
| `m_fRollTargetXOffset` | Offset X do target de roll | 0 |

---

## 7. Controle de Pitch (Inclinação Vertical)

O pitch da câmera responde ao gradiente do terreno e ao estado de voo (salto/queda).

| Parâmetro | Descrição | Valor Típico |
|-----------|-----------|-------------|
| `fPitchAdjust` | Ajuste de pitch base | 0° |
| `fPitchScale` | Escala do pitch baseado em terreno | 0.3–0.7 |
| `fPitchFrameLerpAmount` | Lerp de pitch por frame (suavização) | 0.1–0.3 |
| `PitchLimitDEG` | Limite máximo de pitch (clamp) | 15–25° |
| `PitchDampRatePs` | Taxa de damping do pitch em graus/s | 20–50°/s |
| `PitchBlendTime` | Tempo de blend do pitch (s) | 0.1–0.3s |
| `IsFixedPitch` | Flag: pitch fixo (câmera não rotaciona verticalmente) | false (Chase), true (Cockpit) |
| `m_fAntiPitch` | Fator de anti-pitch (cancela inclinação do terreno) | 0.2–0.5 |
| `m_PitchLeanFactor` | Fator de pitch pela inclinação lateral do carro | 0–0.3 |

---

## 8. Controle de Yaw (Rotação Horizontal)

| Parâmetro | Descrição |
|-----------|-----------|
| `fYawScale` | Escala do yaw baseado em esterçagem/velocidade |
| `fYawFrameLerpAmount` | Lerp de yaw por frame |
| `YawDampRatePs` | Taxa de damping do yaw (graus/s) |
| `fLoostenYawOnDriftEntry` | Afrouxamento do yaw ao entrar em drift (câmera atrasa mais) |
| `fLoostenYawOnDriftEntryTime` | Duração do afrouxamento de yaw no drift entry (s) |
| `m_fYawLookSteerScale` | Escala do look-yaw pela esterçagem |

---

## 9. Blend de Velocidade (Velocity Blend)

Em velocidades baixas, a câmera pode seguir a orientação do carro. Em velocidades altas, segue a direção do vetor velocidade. O `fVelocityBlend` controla essa transição.

```
orientação_câmera = lerp(orientação_carro, direção_velocidade, fVelocityBlend)
```

| Parâmetro | Descrição | Valor Típico |
|-----------|-----------|-------------|
| `fVelocityBlend` | Blend entre orientação do carro (0) e vetor velocidade (1) | 0.3–0.7 |
| `fVelocityBlendDrift` | Blend de velocidade durante drift (geralmente menor) | 0.1–0.3 |
| `VelocityBlend` / `VelocityBlendDelta` | Estado atual e delta do blend (runtime) | — |
| `fSpeedForNormalBlendMPH` | Velocidade para blend de orientação normal (MPH) | 30–50 |
| `fSpeedForPositionOnlyBlendMPH` | Velocidade para blend de posição apenas (ignora rotação) | 10–20 |
| `LookVelBlend` | Blend do look-ahead com vetor de velocidade | — |

---

## 10. Sistema de Blend de Câmera

Transições suaves entre modos de câmera ou estados.

| Parâmetro | Descrição |
|-----------|-----------|
| `BlendTime` / `BlendDuration` / `Blend_Time` | Duração da transição de câmera (s) |
| `BlendSpeed` | Velocidade de blend (alternativa a BlendTime) |
| `fTimeAllowedToBlend` | Tempo máximo permitido para completar um blend |
| `fBlendLength` / `m_fBlendLength` | Comprimento da zona de blend (distância) |
| `BlendInDistance` | Distância de início do blend-in |
| `BlendInPower` / `BlendInTime` | Perfil/duração do blend-in |
| `BlendOutRate_InSeconds` | Taxa de blend-out em segundos |
| `fFallingBlendSpeed` | Velocidade de blend ao entrar em queda |
| `fRisingBlendSpeed` | Velocidade de blend ao subir |
| `fTargetBlendSpeed` | Velocidade de blend do target de look-ahead |
| `FasterSkillDistanceBlend` / `SlowerSkillDistanceBlend` | Blend baseado em distância de habilidade |
| `SegmentBlendTime` | Tempo de blend entre segmentos da pista |
| `bDontBlend` | Flag: desabilita blend (transição instantânea) |
| `gBlend` / `gBlendScale` | Variáveis de blend global (shader/render) |
| `LinearBlend` | Flag: usa interpolação linear em vez de suavizada |

---

## 11. Comportamento no Drift

Durante drift, a câmera tem comportamento especial: o look-ahead diminui, o yaw afrouxa e o roll aumenta.

| Parâmetro | Descrição |
|-----------|-----------|
| `fDriftTargetDist` | Look-ahead reduzido no drift (câmera mais "presa" ao carro) |
| `fRollAngleFromDrifting` | Roll adicional no drift |
| `fVelocityBlendDrift` | Menor blend de velocidade no drift (câmera segue mais o carro) |
| `fLoostenYawOnDriftEntry` | Atraso de yaw no início do drift |
| `fLoostenYawOnDriftEntryTime` | Duração do atraso de yaw |
| `Drift_Camera_Blend_Time` | Tempo de transição para modo drift de câmera |
| `Drift_Level_1_Camera_Shake` | Shake nível 1 (drift suave) |
| `Drift_Level_2_Camera_Shake` | Shake nível 2 (drift moderado) |
| `Drift_Level_3_Camera_Shake` | Shake nível 3 (drift intenso/perfeito) |

---

## 12. Efeitos de Slipstream

Quando um carro está na esteira de outro, a câmera recebe efeitos visuais especiais.

**Arquivo:** `SlipstreamCameraEffects.dat` (SSRT)

**Configurações encontradas:**
```
SlipStreamCamera1_Sys  →  aplicado em: Cockpit
SlipStreamCamera2_Sys  →  aplicado em: Bonnet, Bumper
Default                →  fallback para outros modos
```

| Parâmetro | Descrição |
|-----------|-----------|
| `m_fMaxDistanceToSlipstream` | Distância máxima para ativar efeito de slipstream |
| `SpeedForAutoCameraReverse` | Velocidade para ativar câmera reversa automática |
| `SpeedForAutoCameraForwardFromReverse` | Velocidade para voltar da câmera reversa |
| `LateralSpeedContributionForAutoCameraReverse` | Contribuição da velocidade lateral para câmera reversa |
| `cameraSeverity` | Intensidade do efeito de câmera no slipstream (0–1) |

---

## 13. Speed Line Effect (Linhas de Velocidade)

Offsets da câmera para o efeito de linhas de velocidade (motion blur contextual).

| Parâmetro | Câmera |
|-----------|--------|
| `m_fSpeedLineEffectOffset_Chase` | Camera Chase |
| `m_fSpeedLineEffectOffset_Chase2` | Camera Chase 2 |
| `m_fSpeedLineEffectOffset_Cockpit` | Camera Cockpit |
| `m_fSpeedLineEffectOffset_Bonnet` | Camera Bonnet |
| `m_fSpeedLineEffectOffset_Bumper` | Camera Bumper |

---

## 14. Câmera de Arma (Weapon Chase)

Câmera especial ativada durante uso de armas.

| Parâmetro | Descrição |
|-----------|-----------|
| `m_fWeaponChaseOffset` | Offset longitudinal da câmera de arma |
| `m_fWeaponChaseOffsetTime` | Tempo para atingir o offset de arma (blend) |

---

## 15. Câmera de Replay Automático

| Parâmetro | Descrição |
|-----------|-----------|
| `fMinTimeOnCamera` | Tempo mínimo em uma câmera de replay antes de mudar |
| `fNewCameraMinSubjectDist` | Distância mínima do sujeito para escolher nova câmera de replay |
| `gCameraPositionTime` | Tempo de posicionamento da câmera (múltiplos slots para câmeras diferentes) |

---

## 16. Nós de Câmera no Cenário

Câmeras posicionadas na pista para cinematics e replays.

| Parâmetro | Descrição |
|-----------|-----------|
| `SeGiDefinitionCameraVolume` | Volume que define zona de câmera fixa na pista |
| `SeGiInstanceCameraVolume` | Instância de volume de câmera |
| `SeDefinitionCameraParamsNode` | Nó de definição de parâmetros de câmera |
| `SeInstanceCameraParamsNode` | Nó de instância de parâmetros de câmera |
| `CameraFollowRacerPos` | Câmera de cenário segue posição do piloto |
| `CameraFollowRacerRot` | Câmera de cenário segue rotação do piloto |
| `CameraFollowRacerRotPlanar` | Segue rotação planar (sem pitch) do piloto |
| `CameraOrthographic` | Flag: câmera ortográfica (minimapa) |
| `CameraOrthoSize` | Tamanho do frustum ortográfico |
| `CameraOverrides` | Overrides de câmera por volume/zona |
| `Trackside_Camera` | Câmera lateral na pista (trackside) |

---

## 17. Flags e Configurações Globais

| Parâmetro | Descrição |
|-----------|-----------|
| `kDebugOption_Camera` | Opção de debug de câmera |
| `kDebugOption_ManualCamera` | Câmera manual de debug |
| `MSG_ON_CAMERA_CHANGED` | Mensagem disparada ao mudar modo de câmera |
| `CameraRay` | Ray cast de câmera (para colisão/oclusão) |
| `CameraCollision` | Colisão de câmera com geometria da pista |
| `MoveCameraVisibility` | Visibilidade ao mover câmera (fade de objetos) |
| `ChangeCamera` | Ação de mudança de câmera (input) |

---

## 18. Câmera de Pódio e UI

| Parâmetro | Descrição |
|-----------|-----------|
| `se_camera_podium_camera` | Câmera do pódio (fim de corrida) |
| `ShopCamera` | Câmera da loja de customização |
| `ExtraCameras` | Câmeras extras definidas por assets de personagem |
| `CockpitCameras` | Câmeras de cockpit específicas por veículo |
| `ms_fCameraSwitchAtAnimPercent` | Percentual de animação para troca de câmera |
| `AutoAspectCamera` | Câmera com aspect ratio automático |
| `SeDefaultCamera` | Câmera padrão do engine |
| `SeDebugCamera` | Câmera de debug do engine |

---

## 19. Resumo do Pipeline de Update (por Frame)

```
1. Lê input do jogador (LookLeft/Right/Back, ChangeCamera)
2. Computa target de look-ahead:
     target = carPos + dir_frente * fSteerTargetDist * steerInput
              (ou fDriftTargetDist se em drift)
3. Blend do target (fTargetBlendSpeed)
4. Computa posição desejada da câmera:
     desiredPos = carPos + vLocalCarOffset
                + fMaxDistOffsetOnAccel * accel
                + fMaxDistDeltaOffsetUphill * hillAngle
                + OffsetByPureSpeedZ * speed
5. Spring-damper (fLagStiffness, fLagDamping):
     vel += (desiredPos - camPos) * stiffness * dt - vel * damping * dt
     camPos += vel * dt
6. Computa FOV:
     fov = lerp(fMinFov, fMaxFov, smoothstep(fFovMinSpeed, fFovMaxSpeed, speed))
         + fBoostDeltaMulFOV * boostIntensity
7. Computa roll:
     roll = fRollAngleFromSteering * steerInput
          + fRollAngleFromDrifting * driftIntensity
     roll = clamp(roll, -RollLimitDEG, RollLimitDEG)
8. Computa pitch:
     pitch = fPitchScale * terrainGradient
     pitch = clamp(pitch, -PitchLimitDEG, PitchLimitDEG)
9. Aplica shake (se drift level 1/2/3 ativo ou impacto)
10. Blend de velocidade (orientação):
     camRot = lerp(carRot, velocityDir, fVelocityBlend)
11. Submete para o renderer: posição, rotação (com roll/pitch), FOV
```

---

## 20. Referências para Implementação no Saturn (Interlagos Racing)

Com base na análise do sistema da Hotshot Racing, os parâmetros mais críticos para implementar em hardware limitado (Sega Saturn, 60 fps target):

### Prioridade Alta (mínimo viável)

| Parâmetro Saturn | Equivalente HR | Comentário |
|-----------------|----------------|------------|
| `fLagStiffness` | `fLagStiffness` | Implementar como mola P simples: `vel += (des-cam)*k` |
| `fLagDamping` | `fLagDamping` | Multiplicar `vel *= (1 - damping * dt)` por frame |
| `fOffsetZ` (distância) | `fDistanceOffset` | Distância fixa atrás do carro |
| `fOffsetY` (altura) | `fHeightAdjust` | Altura fixa acima do carro |
| `fFovAtMinSpeed` / `fFovAtMaxSpeed` | mesmos | Variação de FOV com velocidade |
| `fRollAngleFromSteering` | mesmo | Roll de câmera em curva |
| `fSteerTargetDist` | mesmo | Look-ahead básico |

### Prioridade Média

| Parâmetro | Nota |
|-----------|------|
| `fMaxDistOffsetOnAccel` | Recuo dinâmico — sensação de "arrancada" |
| `fLandingSpringStrength` | Bounce após rampa — impacto visual forte |
| `fBumpSpringStr` | Absorção de bumps do terreno |
| `Drift_Level_1/2/3_Camera_Shake` | Shake de drift — identidade do jogo |

### Prioridade Baixa (polish)

| Parâmetro | Nota |
|-----------|------|
| `fVelocityBlend` | Custo de CPU; simplificar para lerp fixo |
| `fFallingHeightOffset` | Detalhe fino em saltos |
| `fLoostenYawOnDriftEntry` | Sutileza no entry de drift |
| `SlipstreamCameraEffects` | Efeito contextual; pode ser omitido |

---

*Documento gerado por análise de strings de `HotshotRacing.exe` + parser SSRT de `SlipstreamCameraEffects.dat`.*  
*Para extrair valores numéricos de `cameras.zml`: executar `hotshot_camera_extract.py --game-dir <PATH>`.*
