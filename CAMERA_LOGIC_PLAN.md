# Camera Logic Plan (Proposta)

## Objetivo
Manter a camera estavel estilo arcade (Daytona-like), sempre orientada pelo PATH da pista, com transicoes suaves entre segmentos e sem depender do yaw instantaneo do carro.

## Problemas que queremos evitar
- Camera girar para lado errado em troca de segmento.
- Carro apontar para direcao diferente do alvo da camera.
- Oscilacao de pitch/yaw em curvas curtas.
- Saltos visuais quando muda o segmento ativo.

## Principios
1. A direcao base da camera vem do PATH (nao do modelo do carro).
2. O alvo da camera e sempre um ponto `lookAhead` no PATH.
3. O offset da camera (atras/cima/lateral) usa base ortonormal do PATH.
4. Toda troca de estado usa filtro temporal (damping), sem alteracao instantanea.
5. Curvatura da pista regula dinamicamente distancia, altura e FOV.

## Estado por frame
Estrutura sugerida (`CameraFrameContext`):
- `carPosWorld`
- `carSegmentId`
- `pathS` (parametro ao longo do PATH)
- `pathTangent` (direcao local normalizada)
- `pathNormalUp` (normal local ou up global estabilizado)
- `curvatureAbs` (modulo da curvatura local)
- `speedNorm` (0..1)
- `slope` (incline/declive local)

## Perfis de camera
Definir perfis simples:
- `Straight`
- `TurnLight`
- `TurnHard`
- `CrestDip` (subida/descida mais forte)

Cada perfil controla:
- `behindDist`
- `height`
- `lateral`
- `lookAheadBase`
- `fovDeg`
- `dampingPos`
- `dampingAim`

## Classificacao de contexto
Regra por curvatura (exemplo):
- `curvatureAbs < C1` -> `Straight`
- `C1 <= curvatureAbs < C2` -> `TurnLight`
- `curvatureAbs >= C2` -> `TurnHard`

Se `abs(slope) > S1`, aplicar modificador `CrestDip`.

## Look-ahead no PATH
Calculo sugerido:
- `lookAhead = lookAheadBase + kSpeed * speedNorm - kCurv * curvatureAbs`
- Clamp: `lookAheadMin .. lookAheadMax`
- Alvo: `target = PathSample(pathS + lookAhead).position`

Isso garante que camera e carro "olhem" para a mesma direcao da pista.

## Base da camera no espaco
Com a amostra do PATH:
- `forward = normalize(pathTangent)`
- `up = stabilize(previousUp, pathNormalUp)` (ou up global com blend)
- `right = normalize(cross(forward, up))`
- Re-ortonormalizar `up = normalize(cross(right, forward))`

Posicao ideal:
- `camIdeal = carPosWorld - forward * behindDist + up * height + right * lateral`

## Suavizacao temporal
Nao aplicar `camIdeal` diretamente.

Usar filtro exponencial por frame:
- `camPos = lerp(camPosPrev, camIdeal, alphaPos)`
- `camAim = lerp(camAimPrev, target, alphaAim)`

Onde:
- `alpha = 1 - exp(-dt * damping)`
- `dt` fixo da engine (ex. 1/60)

## Sincronia carro x camera
Para evitar divergencia:
1. Carro visual deve usar yaw derivado do `forward` do PATH (com offset visual fixo).
2. Camera chase usa o mesmo `forward` como referencia primaria.
3. Se carro parar, manter `pathS` e `forward` estaveis (nao recalcular por ruido local).

## Integracao no projeto atual
Pontos naturais de integracao:
- `src/camera_system.cxx`
  - concentrar calculo de perfil, look-ahead e smoothing.
- `src/game_loop_system.hpp`
  - montar `CameraFrameContext` antes do `ResolveCameraFrameState`.
- `src/main.cxx`
  - tunables default (FOV/preset inicial).

## Logs de validacao (on-screen)
Adicionar/usar linhas de debug:
- `CAM mode:<profile> curv:<x> slope:<x>`
- `CAM lookAhead:<x> speed:<x>`
- `CAM posErr:<x> aimErr:<x>`
- `CAR yawPath:<x> yawVisual:<x> diff:<x>`

Critérios:
- `diff` pequeno e estavel em reta/curva.
- Sem salto de `posErr` na troca de segmento.

## Plano de implementacao (incremental)
1. Introduzir `CameraFrameContext` e `lookAhead` no PATH.
2. Trocar alvo da camera para `PathSample(pathS + lookAhead)`.
3. Aplicar smoothing em posicao e alvo.
4. Ligar classificacao de perfil por curvatura/slope.
5. Ajustar tuning por pista com logs ligados.

## Riscos e mitigacao
- Risco: over-smoothing e input lag visual.
  - Mitigar com `dampingAim > dampingPos` em curvas.
- Risco: up vector instavel em trechos inclinados.
  - Mitigar com blend para up global quando normal oscilar.
- Risco: mismatch entre PATH e malha visual.
  - Mitigar com clamp de altura e checagem por segmento ativo.

## Resultado esperado
- Camera consistente em toda a volta.
- Carro e camera apontando para o mesmo fluxo do PATH.
- Menor flicker perceptivo em transicao de segmentos.
