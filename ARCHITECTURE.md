# Arquitetura modular orientada a objetos

## Visão geral
- **Application / GameCore** monta subsistemas (`Car`, `TrackBuffer`, `CameraSystem`, `TrackRenderer`, `InputHandler`, `LogService`) e executa o loop principal.
- Cada subsistema é instanciado por interfaces (e.g. `ICarCommand`, `ITrackLoader`, `ICameraTarget`), garantindo inversão de dependência e permitindo trocar carros, pistas ou renderizadores sem modificar o núcleo.

## Carro (Vehicle)
- `Car` expõe comportamentos: acelerar, frear, girar (yaw/pitch), girar rodas, reagir a colisões e aplicar deformações/“amassados”.
- Componentes internos:
  - `CarDynamics` (transformações, velocidade / rotação).
  - `WheelSystem` (rotaciona rodas, aplica spin, estressa o visual).
  - `DamageModel` (registrar impactos e ajustar shaders/texturas).
  - `CarRenderer` (encapsula envio de meshes/texturas ao VDP1, recebe transformações do `Car` via `ITransformProvider`).
- Interfaces (`ICarCommand`, `ICarStatus`) permitem que o InputHandler e HUD afetem ou consultem o carro sem conhecer detalhes internos.

## Pista e segmentos
- `TrackSegment` encapsula meshes, texturas, offsets e bounds. Tem métodos `PrepareForRendering(CameraContext)` e `Render(RenderContext)` e implementa `ITrackSegment`.
- `TrackBuffer` carrega o `.NYA` para a DRAM / buffers de 4MB (serialização + instância). Cacheia 40 segmentos e pode recarregar sob demanda.
- `TrackRenderer` consome `ITrackSegment`s e recebe apenas `CameraContext` + `LightContext`; não depende do carro. A renderização é independente e reutilizável.
- Interfaces `ITrackLoader`, `ITrackSegment` e `ITrackBuffer` permitem trocar pistas no futuro com fábricas (ex: `TrackFactory::Load("INTLAGOS.NYA")`).

## Background
- Componente independente (`BackgroundManager`/`Skybox`) mantém o VDP2 e efeitos de céu. Possui sua própria lógica de atualização de materiais/texturas e não depende de carro ou track.
- A câmera influencia o background através da rotação, enviando a orientação atual para o manager (ex: scroll da skybox), garantindo o efeito visual completo sem acoplamentos.

## Câmera e foco
- `CameraSystem` orbita qualquer `ICameraTarget`, que o `Car` e os `TrackSegment`s implementam oferecendo posição + limites. Recebe comandos de input (yaw/pitch/strafe) e aplica-os internamente.
- `FocusSelector` mantém uma lista fluida de `ICameraTarget`s válidos. O botão `A` chama `NextTarget()` e o `CameraSystem` muda o `CameraTarget` sem saber se é o carro ou o segmento.
- `CameraInputHandler` injeta yaw/pitch/strafe/zoom e manipula `Y+R/L` para deslocamento vertical, mantendo os controles independentes do objeto orbitado.

## Fluxo de dados recomendado
1. `InputHandler` envia comandos para `Car` (acelera/freia) e `CameraSystem` (rotaciona/strafe), e dispara troca de foco no `FocusSelector`.
2. `TrackBuffer` carrega os segmentos e os disponibiliza ao `TrackRenderer`.
3. No loop principal: atualiza input → atualiza câmera (com foco atual) → renderiza track (independente) → renderiza carro.
4. `CameraSystem` fornece `LookAt` e posição para o renderer; a pista e o carro nunca acessam estado um do outro.

## Incremental
1. Tornar todas as dependências acessíveis apenas por interfaces e reescrever o loop para usá-las.
2. Implementar `TrackBuffer` + `TrackRenderer` desacoplados do carro. Testar apenas a pista (renderTrack true, renderCar false).
3. Adicionar `FocusSelector` e `CameraSystem` para trocar alvos (carro/pista) com `A`.
4. Plug-and-play: adicionar fábricas para novos carros/pistas, e novos comportamentos (drift, objetos dinâmicos) sem mexer no core.

## Observações finais
- O `LogFailure` existente complementa a arquitetura, avisando se o pipeline falha antes de chegar ao renderizador (já que exceptions estão desativadas).
- Cada responsabilidade (carro, pista, câmera) é encapsulada, testável, e pode ser modificada sem quebrar o restante.
- Quando reativarmos o car/tracks, basta reinstanciar `Car` e `TrackBuffer`, mantendo as interfaces inalteradas.
# Track pipeline
1. Copy `INTLAGOS.NYA` to cart RAM via `SerializeTrackToCart` (`cd/data` -> `cart`). `TrackSerializedCopy` end up in Work RAM.
2. `TrackRenderer::LoadFromSerialized` reconstructs a `ModelObject` from the cart image, computes mesh count/face count/minmax, caches centers and normals, and keeps `trackOffset`/`trackScale` (currently 1.0).
3. After `trackReady` becomes true the main loop places the car at `trackRenderer.StartMeshCenter()` so CAR1 and segment 0 coincide.
4. `TrackRenderer::Render` calls `EnsureCached` for mesh 0: the mesh is copied from the cart into `smoothCache_`/`flatCache_`, attributes are overwritten (`double sided`, `sprPolygon`) and vertices are recomputed with `scale` & `offset` (zero after latest changes).
5. Because `SetUseOriginal(true)` is active, rendering ultimately uses `SRL::Scene3D::DrawMesh`/`DrawSmoothMesh`, which writes polygons to VDP1. Any attempt to go through `SglPoly::DrawMesh` previously triggered invalid opcodes when coordinates were too large.
6. Logs record `Track rendered meshes`, camera/car positions, and the raw segment center (`s0 c`). This allows checking whether Blender export coordinates match the SATurn world.

```mermaid
flowchart LR
    A[INTLAGOS.NYA (.NYA exporter)] --> B[Cart RAM 4MB (SerializeTrackToCart)]
    B --> C[TrackRenderer::LoadFromSerialized]
    C --> D[Compute meshCount/faceCount, cache centers, normals, offsets/scale]
    D --> E{trackReady?}
    E -->|yes| F[main.cpp positions CAR1 at trackRenderer.StartMeshCenter()]
    F --> G[TrackRenderer::Render (drawLimit=1)]
    G --> H[EnsureCached (copies mesh 0 to cache + attributes)]
    H --> I[SRL::Scene3D::DrawMesh/DrawSmoothMesh (useOriginal=true)]
    I --> J[VDP1 renders segment 0 alongside CAR1]
    E -->|no| K[Wait/retry loading]
```
