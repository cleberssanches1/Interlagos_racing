# Plano de Acao: Movimentacao CAR1.NYA nas Faces Renderizadas

## 1) Resultado da analise dos 3 projetos

- `stuntrally3-main`: fisica muito completa (Bullet + Pacejka + substeps), custo alto para SH2.
- `vdrift-master`: arquitetura de veiculo excelente (driveline + slip + yaw por momento), mas ainda pesada no estado original.
- `torcs-r1-3-1`: modelo claro de forcas no chassis + yaw por momento, porem com custo alto (loop muito frequente e modelo detalhado por roda).

### Decisao tecnica

Adotar uma base **hibrida simplificada TORCS/VDrift**:
- Modelo planar de 2 eixos (bicycle-like) em fixed-point.
- Slip lateral simplificado por eixo (frente/traseira).
- Yaw por momento lateral (sem solver de corpo-rigido completo).
- Reuso integral do pipeline atual de solo/face (`GroundFollower` + `TrackCollisionQuery`).

## 2) Estado atual implementado

### 2.1 Novo nucleo de dinamica

Arquivo: `src/car_dynamics_model.hpp`

- Logica anterior de curva foi substituida por:
  - calculo de slip por eixo;
  - forca lateral saturada por grip;
  - integracao de yaw por momento;
  - integracao X/Z pelo heading do carro.
- Direcao voltou ao fluxo natural no input (`left -> SteerLeft`, `right -> SteerRight`) e a conversao para convenção do jogo foi centralizada no modelo dinamico.

### 2.2 Regras de tipo de solo (detecao)

Arquivos: `src/car_ground_follower.hpp`, `src/car_physics_shared.hpp`, `src/simple_car_physics.hpp`

- Adicionada deteccao de solo por familia:
  - asfalto (`kAsphaltFamilyId`);
  - driveable nao-asfalto;
  - fallback sem suporte.
- `surfaceGripScale` agora alimenta a dinamica antes da integracao planar.

### 2.3 Colisao para nao atravessar faces

Arquivo: `src/car_ground_follower.hpp`

- `ResolvePlanarWallPush` agora roda sempre que ha suporte de solo (antes era condicional).
- Limite de correcao planar por frame foi ampliado para empurrar o carro para fora das faces laterais com mais robustez.

## 3) Regras de colisao adotadas para CAR1

1. Solo valido apenas por familias driveables (probes estritos + fallback controlado).
2. Correcao vertical por adesao (snap + step limits).
3. Correcao lateral por push de parede/faces renderizadas.
4. Sem suporte de solo: reduzir dinamica e retornar para ultima posicao planar estavel.
5. Borda parcial (left/right lost): damping adicional para evitar ganho de energia lateral.

## 4) Proximos passos (calibracao e hardening)

1. Calibrar `kLateralStiffnessFront/Rear`, `kLateralForceCapBase`, `kYawMomentGain`, `kYawCoupling` com telemetria `OVR dyn`/`OVR mov`.
2. Criar tabela de familias por tipo de solo (asfalto, zebra, grama, areia) e mapear `gripScale` por classe.
3. Adicionar sweep planar curto (capsule/circle no sentido da velocidade) para reduzir tunelamento em alta velocidade.
4. Aplicar limite de variacao de yaw por frame dependente da velocidade para reduzir "snap turn".
5. Ajustar roll visual do chassis com base em `yawStepDeg` e `surfaceGripScale`.

## 5) Critrios de aceite

1. `Acelerar + esquerda` curva para esquerda com deslocamento lateral consistente.
2. `Acelerar + direita` curva para direita sem inversao de inclinacao.
3. Em bordas/faces laterais, o carro nao ultrapassa parede visual por mais de 1 frame.
4. Sem input, o carro para de girar (yaw converge para zero).
5. Logs `OVR dyn` e `OVR mov` coerentes com comando (`st`, `ys`, `ndx`, `ndz`).
