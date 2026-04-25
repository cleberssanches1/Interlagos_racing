# Plano de Otimização de Renderização (30 → 60 FPS)

## Diagnóstico Confirmado

### O gargalo NÃO é o SH2 — é o VDP1

| Métrica | Valor | Interpretação |
|---------|-------|---------------|
| `fr:628` FRT ticks | ~fração de ms | SH2 termina rápido |
| `vb:2.00` | 2 vblanks/frame | VDP1 demora >16.67ms |
| `d30:0% d60:100%` | toda frame excede 60fps | VDP1-bound confirmado |
| `dr:519 fr:628` | 83% do SH2 é draw prep | Master idle o resto do frame |

**Fluxo real:**
1. SH2 monta SGL sort list em `RenderVisibleSegmentOrderStabilized()` → ~0.3ms
2. `SRL::Core::Synchronize()` escreve VDP1 commands + aguarda VBI → ~32ms wait
3. VDP1 renderiza a lista → excede 1 vblank → frame fica em 30fps

Para atingir 60fps, o VDP1 precisa terminar dentro de 1 vblank (16.67ms).

---

## Problemas Identificados no Código

### P1 — Sem culling de segmento antes do Render()
`RenderVisibleSegmentOrderStabilized()` (track_system.cxx:14530–14728) itera todos os segmentos
da janela e chama `entry->renderer->Render()` para **cada um**, incluindo segmentos que estão:
- 100% atrás da câmera
- Fora do campo de visão horizontal

`TrackRenderer::Bounds()` (track_renderer.hpp:1021) expõe o AABB exato de cada segmento.
Nenhum teste de frustum é feito hoje.

### P2 — Métrica `submittedTrackFaces` quebrada no modo estabilizado
`TrackSystem::Telemetry()` retorna `coordinator_.Telemetry()`, mas em modo estabilizado a função
`RenderVisibleSegmentOrderStabilized` **bypassa** o coordinator. Resultado: `coordinator_.usage_.drawnTrackFaces`
permanece 0 → `VDP1 FR%:0 F:0` no HUD, impossibilitando medir o impacto das otimizações.

### P3 — Atributo de visibilidade das faces (SingleSided vs DoubleSided)
Se os atributos das faces no pack estiverem como `DoubleSided`, o SGL não faz backface cull.
Todas as faces traseiras (ocultas pela câmera superior) vão para o VDP1 desnecessariamente.
Para um track de corrida com câmera sempre acima, todas as superfícies horizontais são candidatas.

### P4 — `SGL_MAX_POLYGONS = 1700` pode ser superestimado
Com 10 segmentos fixed64, a contagem real de faces pode ser bem menor que 1700.
Sort de 1700 polígonos em `Synchronize()` tem custo O(n log n) evitável.

---

## Ações de Otimização (ordem de implementação)

---

### Ação 1 — Corrigir métrica de faces no modo estabilizado

**Impacto: BASE para medir tudo. Sem isso não dá para calibrar as demais ações.**

**Arquivo:** [src/track_system.cxx:14731–14761](src/track_system.cxx) — loop de render em `RenderVisibleSegmentOrderStabilized`

**O que fazer:** Acumular `entry->renderer->LastDrawnFaces()` após cada `Render()` e setar em
`coordinator_.telemetry_.submittedTrackFaces`.

**Antes (atual):**
```cpp
for (size_t i = 0; i < preparedCount; ++i)
{
    ...
    entry->renderer->Render(lightDirection, cameraLocation);
    ...
    ++runtimeSafeRenderedThisFrame_;
}
```

**Depois:**
```cpp
uint32_t totalSubmittedFaces = 0u;
for (size_t i = 0; i < preparedCount; ++i)
{
    ...
    entry->renderer->Render(lightDirection, cameraLocation);
    totalSubmittedFaces += entry->renderer->LastDrawnFaces();
    ...
    ++runtimeSafeRenderedThisFrame_;
}
// Atualiza telemetria para o HUD mostrar VDP1 FR% real
coordinator_.telemetry_.submittedTrackFaces = totalSubmittedFaces;
```

**Onde fica:** Adicionar acumulador local antes do segundo loop, somar `LastDrawnFaces()` após
cada `Render()`, e no final do loop atribuir ao `coordinator_.telemetry_`.

---

### Ação 2 — Segment-level frustum culling (câmera AABB test)

**Impacto: ALTO — elimina 2–4 segmentos/frame sem nenhum custo VDP1.**

**Arquivo:** [src/track_system.cxx:14530](src/track_system.cxx) — início do loop de preparação
em `RenderVisibleSegmentOrderStabilized`

**Algoritmo:** Teste de "atrás do plano near" usando produto escalar:
```
cameraForward = normalize(cameraLookTarget - cameraLocation)
segCenter = (bounds.min + bounds.max) / 2 + trackOffset
segRadius = distância do centro ao canto mais distante (metade da diagonal)
camToSeg = segCenter - cameraLocation
dotProduct = camToSeg · cameraForward
if (dotProduct < -segRadius * kCullMargin) → segmento 100% atrás → skip
```

O `cameraForward` pode ser computado uma única vez antes do loop de segmentos.
`kCullMargin = 1.2f` (margem conservadora para evitar pop-in).

**Requer:** Expor `cameraLocation` e `cameraLookTarget` para a função, ou calcular
`cameraForward` a partir dos parâmetros já passados para `RenderVisibleSegmentOrderStabilized`
(que já recebe `cameraLocation`). O `lookTarget` precisa vir como parâmetro novo ou via
estado interno (`framePlanCurrent_` pode conter).

**Alternativa mais simples:** Verificar se `dots` do centro do segmento com o forward da câmera
é negativo e maior (em magnitude) que o raio do segmento. Não requer lookTarget explícito se
guardarmos o forward no `BuildAndApplyFramePlanStage()`.

**Onde guardar o forward:** Adicionar `Vector3D cameraForwardLastFrame_` em TrackSystem para
reusar durante o draw sem recomputar.

---

### Ação 3 — Verificar atributo SingleSided das faces da pista

**Impacto: POTENCIALMENTE MUITO ALTO — se faces são DoubleSided hoje, enforçar SingleSided
reduz face count do VDP1 à metade.**

**Onde checar:** Após a Ação 1 (com métrica real), verificar se o face count muda ao
forçar `SingleSided` num teste.

**Arquivo:** [src/track_renderer.hpp:543](src/track_renderer.hpp)

O flag `forceDoubleSided_` já existe e força tudo para DoubleSided quando ativo. Se estiver
`false` (padrão), as attrs originais do pack são usadas. A questão é o que as attrs do pack
contêm.

**Como diagnosticar:** Adicionar um print temporário em `InitializeFromComponentDataCopied`
contando faces com `Visibility == DoubleSided`.

**Como enforçar SingleSided se for o caso:** Em `InitializeFromComponentDataCopied`, após copiar
as attrs, iterar e forçar `a.Visibility = SRL::Types::Attribute::FaceVisibility::SingleSided`
para toda face. Ou setar no build de LOD slot. Risco: curbs/muros com câmera passando ao lado
podem ter glitch. Testar visualmente antes de manter.

---

### Ação 4 — Reduzir janela para 8 segmentos

**Impacto: MÉDIO — garante -20% faces VDP1 sem risco de artefato visual.**

**Arquivos:**
- [src/main.cxx:852](src/main.cxx):
  ```cpp
  // De:
  trackConfig.initialSegments = kFixed64Mode ? 10u : 20u;
  trackConfig.minSegments     = kFixed64Mode ? 10u : 20u;
  // Para:
  trackConfig.initialSegments = kFixed64Mode ? 8u : 20u;
  trackConfig.minSegments     = kFixed64Mode ? 8u : 20u;
  ```
- [src/track_system.hpp:588](src/track_system.hpp):
  ```cpp
  // De: 11u : 21u
  static constexpr size_t kSlotPoolSize =
      kEnableTrackLeakIsolationFixed64Pipeline ? 9u : 21u;
  ```
- [makefile](makefile):
  ```makefile
  SGL_MAX_POLYGONS = 1400   # 8 segs × ~163 faces + 80 de margem
  SGL_MAX_VERTICES = 1800   # proporção similar
  ```

**Pré-condição:** Verificar visualmente se 8 segmentos não gera pop-in no arco de visão
da câmera ao rodar na pista.

---

### Ação 5 — Budget de faces por rank (LOD espacial dentro do fixed64)

**Impacto: MÉDIO-ALTO — reduz faces dos segmentos distantes sem degradar os próximos.**

**Arquivo:** [src/track_system.cxx:14731](src/track_system.cxx) — loop de render

**Ideia:** Segmentos com `logicalRank ≥ 6` (distantes) recebem `SetDrawLimit(1)` ao invés de
`kMaxDrawMeshes = 2`. Com 1 mesh ao invés de 2 por segmento distante, reduzimos ~4 segmentos
× 50% = −20% de faces adicionais.

```cpp
for (size_t i = 0; i < preparedCount; ++i)
{
    auto* entry = preparedEntries[i];
    // Ajustar limit de draw por rank (mais longe = menos meshes)
    size_t logicalRank = 0u;
    if (TryGetWindowLogicalRank(entry->id, logicalRank) && logicalRank >= 6u)
    {
        entry->renderer->SetDrawLimit(1u);
    }
    else
    {
        entry->renderer->SetDrawLimit(kMaxDrawMeshes);
    }
    entry->renderer->SetOffset(trackOffset);
    entry->renderer->Render(lightDirection, cameraLocation);
    ...
}
```

**Pré-condição:** Cada segmento deve ter ao menos 2 meshes para que reduzir para 1 faça
diferença. Se os segmentos já têm apenas 1 mesh cada, esta ação é no-op — verificar com a
Ação 1 primeiro.

---

### Ação 6 — Calibrar SGL_MAX_POLYGONS pelo count real

**Impacto: BAIXO-MÉDIO — sort list menor = sort + command write mais rápido.**

**Pré-condição:** Ação 1 ativa com `VDP1 F:XXX` visível no overlay.

**Ação:** Após medir o pico real de faces (ex: 900 faces), reduzir:
```makefile
SGL_MAX_POLYGONS = 1050   # pico_medido + 15%
SGL_MAX_VERTICES = 1350
```

**Efeito:** `slSharedSort` na `SRL::Core::Synchronize()` processa lista menor → menos ciclos
antes do VBI. Pequeno, mas acumula com as demais.

---

## Sequência de Implementação Recomendada

| Passo | Ação | Build & Medir |
|-------|------|---------------|
| 1 | Ação 1: Fix métrica `submittedTrackFaces` | Build → verificar `VDP1 F:XXX` no HUD |
| 2 | Ação 2: Frustum culling por segmento | Build → comparar `VDP1 F:` antes/depois |
| 3 | Ação 3: Diagnosticar SingleSided (print de contagem) | Print temporário → avaliar impacto potencial |
| 4 | Se DoubleSided: Enforçar SingleSided no componentMode | Build → comparar FPS |
| 5 | Ação 4: Janela para 8 segmentos | Build → medir FPS + visibilidade |
| 6 | Ação 6: Tune SGL_MAX_POLYGONS com dados | Build final |
| 7 | Ação 5: DrawLimit por rank | Ajuste fino se FPS ainda insuficiente |

---

## Critérios de Aceitação

| Métrica | Atual | Meta |
|---------|-------|------|
| FPS | 30.0 | ≥ 45 (ideal 60) |
| `vb:` | 2.00 | ≤ 1.10 |
| `d60:%` | 100% | ≤ 20% |
| `VDP1 F:` | desconhecido | visível e ≤ 900 |
| `SWLWR free` | 934796 | mantido estável |
| `df:` per frame | 0 | mantido 0 |

---

## Arquivos Impactados

| Arquivo | Ações |
|---------|-------|
| [src/track_system.cxx](src/track_system.cxx) | Ações 1, 2, 3 (diag), 5 |
| [src/track_system.hpp](src/track_system.hpp) | Ação 4 (`kSlotPoolSize`), Ação 2 (campo `cameraForwardLastFrame_`) |
| [src/main.cxx](src/main.cxx) | Ação 4 (`initialSegments = 8`) |
| [makefile](makefile) | Ações 4 e 6 (`SGL_MAX_POLYGONS`) |
