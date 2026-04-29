# Plano de Ação — Sobreposição de Pista sobre o Carro e Distorção de Segmentos

## Contexto do Problema

Dois artefatos distintos ocorrem na renderização do jogo:

1. **Sobreposição de pista sobre o carro** — Quando o carro está próximo à fronteira entre dois segmentos, polígonos do segmento mais próximo à câmera são desenhados *na frente* do carro, cobrindo-o parcialmente.
2. **Distorção do segmento mais próximo** — O segmento de pista que se aproxima da câmera distorce visualmente: os quads grandes têm vértices que entram na zona do near-clip, causando esticamento e deformação de geometria.

---

## Como o Pipeline Funciona Hoje

### Fluxo de renderização por frame (game_loop_system.hpp:1222)

```
1. SRL::Scene3D::LoadIdentity() + LookAt  ← câmera para pista
2. trackSystem->RenderFrame(...)           ← linha 1258: submete polígonos da pista ao sort list SGL
3. SRL::Scene3D::LoadIdentity() + LookAt  ← reset de câmera para carro
4. RenderCar(camera)                       ← linha 1284: submete polígonos do carro ao sort list SGL
5. VDP1 frame flip → SGL desenha tudo painter's algorithm (longe→perto)
```

**Observação crítica**: Pista e carro compartilham o **mesmo sort list do SGL**. A ordem de submissão (`slPutPolygon`) não determina a ordem de desenho — o SGL re-ordena todos os polígonos pelo Z do centroide antes de enviar ao VDP1.

### Sort de segmentos — nível macro (track_system.cxx:14202)

`BuildStabilizedSortedHandles` ordena os 20 segmentos por distância Chebyshev da câmera (longe→perto). O segmento mais distante é submetido primeiro, o mais próximo por último. Isso garante a ordem inter-segmento. Porém, **a ordem intra-polígono** dentro do sort list do SGL é determinada pelo campo `ATTR.sort` de cada polígono individualmente.

### Sort de polígonos individuais — nível micro (track_system.cxx:4064)

```cpp
auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
{
    const uint16_t clamped = (raw > 3u) ? 0u : raw;
    return static_cast<SRL::Types::Attribute::SortMode>(SortMode::Center - clamped);
};
```

Polígonos da pista usam `SortMode::Center - offset` onde `offset` ∈ {0, 1, 2, 3}. `SortMode::Center` é o modo padrão de sort por centroide Z. O subtração do offset empurra a chave de sort ligeiramente para o bucket "mais próximo da câmera".

Polígonos do carro vêm do `ModelObject` com seus ATTRs originais — nenhum override de sort priority é aplicado em `MeshRenderer::Render()` ou `CarSystem::SubmitRender()`.

---

## Problema 1 — Sobreposição de Pista sobre o Carro

### Causa Raiz

O carro está posicionado *sobre* a superfície da pista. Quando o carro se encontra próximo à borda de um segmento, o quad de asfalto mais próximo à câmera tem seu **centroide Z** calculado pelo SGL como *mais próximo da câmera* do que o centroide de alguns polígonos do carro. Resultado: o SGL desenha o quad da pista por cima do carro.

Isso se intensifica em segmentos com quads longos: um quad que se estende de 10 unidades atrás do carro até 10 unidades à frente terá centroide Z exatamente na posição do carro — e a imprecisão da sort list (número finito de buckets) pode colocá-lo no mesmo slot ou em um slot "mais próximo".

O sort macro (Chebyshev) não resolve isso — ele garante apenas que o **segmento** correto seja submetido antes do outro, não que cada **polígono** do segmento apareça atrás do carro.

### Solução A — Sort Priority Boost para o Carro (RECOMENDADA)

**Arquivo:** [src/mesh_renderer.cxx](src/mesh_renderer.cxx) — método `Render()` / `DrawMesh()`
**Arquivo:** [src/mesh_renderer.hpp](src/mesh_renderer.hpp) — struct `Config`

Adicionar um campo `sortPriorityBoost` na config do `MeshRenderer`:

```cpp
// mesh_renderer.hpp — struct Config
int8_t sortPriorityBoost{0};  // >0 empurra para "mais próximo" no sort list SGL
```

Em `DrawMesh()`, antes de submeter cada face via `SRL::Scene3D::DrawMesh()`, modificar o ATTR de cada polígono para aplicar o boost:

```cpp
// mesh_renderer.cxx — DrawMesh(), antes de slPutPolygon
if (config_.sortPriorityBoost != 0)
{
    for (auto& attr : faceAttrs)
    {
        // SortMode::Center = valor numérico X; subtrair = "mais perto" no sort list
        const uint8_t current = static_cast<uint8_t>(attr.Sort);
        attr.Sort = static_cast<SRL::Types::Attribute::SortMode>(
            std::max<int16_t>(0, static_cast<int16_t>(current) - config_.sortPriorityBoost));
    }
}
```

Em `CarSystem` (car_system.cxx:60), ao construir `rendererConfig`:

```cpp
rendererConfig.sortPriorityBoost = 4;  // garante prioridade acima de qualquer sort=0..3 da pista
```

**Valor inicial recomendado:** `4` — é 1 acima do máximo usado pela pista (offset 0→3). Ajustar no hardware se necessário.

**Por que funciona:** O SGL coloca polígonos com chave Z menor (closer) nos buckets de "frente" do sort list. Ao adicionar um boost de 4 ao carro, seus polígonos sempre vencem os polígonos da pista no mesmo bucket de profundidade.

**Risco:** Baixo — afeta apenas o carro. O boost é constante e determinístico.

---

### Solução B — Depth Bias nos Segmentos Próximos ao Carro (COMPLEMENTAR)

**Arquivo:** [src/track_system.cxx:4064](src/track_system.cxx#L4064) — `DecodeSortMode`

Para o segmento que contém o carro (`observedCarSegmentId_`, track_system.cxx:14331) e o segmento imediatamente adjacente, adicionar um offset que empurra seus polígonos para buckets "mais distantes" no sort list:

```cpp
// Em RenderVisibleSegmentOrderStabilized, antes de submeter cada segmento
const bool isCarSegment = (entry->id == observedCarSegmentId_);
const bool isAdjacentSegment = std::abs(entry->id - observedCarSegmentId_) <= 1;
const uint8_t segSortBias = (isCarSegment || isAdjacentSegment) ? 3u : 0u;
```

Passar `segSortBias` para o render do segmento para que o `DecodeSortMode` some esse bias ao raw:

```cpp
// DecodeSortMode modificado
const uint16_t biasedRaw = static_cast<uint16_t>(raw + segSortBias);
const uint16_t clamped = (biasedRaw > 3u) ? 3u : biasedRaw;
```

**Efeito:** Polígonos da pista nos segmentos do carro ficam em buckets mais "distantes" que o normal — aparecem atrás do carro.

**Risco:** Médio — alterar o sort mode de segmentos específicos por frame requer passar contexto extra para o path de render. Também cria transições visíveis quando o carro cruza a fronteira de segmento se o bias não for interpolado.

---

### Solução C — Descarte de Polígonos de Chão no Segmento do Carro (COMPLEMENTAR)

**Arquivo:** [src/track_system.cxx](src/track_system.cxx) — na execução do coordinator por segmento

Polígonos de asfalto horizontal (normal Y ≈ -1 em espaço de modelo) no segmento `observedCarSegmentId_` estão completamente ocultos sob o carro — descartá-los elimina o conflito de Z sem alterar o sort. 

Implementação: no callback de `coordinator_.Execute()` (linha 15131), antes de `SRL::Scene3D::DrawMesh()`, filtrar polígonos com normal Y acima de um threshold se `entry.id == observedCarSegmentId_`.

**Risco:** Médio-alto — requer acesso às normais das faces em runtime. Útil apenas se a pista for geometricamente plana no segmento do carro (pode causar buracos visíveis em pistas inclinadas).

---

## Problema 2 — Distorção do Segmento Mais Próximo

### Causa Raiz

Os quads de asfalto em segmentos de pista são geometricamente longos (frequentemente 10–30 unidades na direção Z). Quando o carro se aproxima do segmento mais próximo à câmera, um ou mais vértices desses quads entram na zona do near-clip do SGL.

No Saturn (diferente do PS1), **não há warping afim** — a perspectiva é correta por pixel. A distorção observada é causada pelo **clipping do near plane**: o SGL projeta o vértice próximo para a borda da tela com coordenadas de tela enormes, produzindo um quad com um vértice "no infinito" que estica o polígono inteiro.

O near-clip padrão do SGL é muito próximo (tipicamente `z = 0.1` em espaço de câmera), permitindo que vértices entrem na zona antes de serem corretamente clipados.

### Solução A — Aumentar o Near-Clip (RISCO BAIXO, primeiro a tentar)

**Arquivo:** [src/main.cxx](src/main.cxx) ou na inicialização do SGL

O SGL expõe o near-clip via:

```c
// SGL nativo
slZClip(nearZ, farZ);  // valores FIXED 16.16
```

Aumentar `nearZ` de (estimado) `0x00001999` (`~0.1`) para `0x00006666` (`~0.4`) empurra o plano de clip para mais longe da câmera, evitando que os vértices dos quads longos entrem na zona de distorção.

**Efeito:** Os quads do segmento mais próximo são clipados antes de distorcer. Pode tornar o segmento mais próximo invisível se o carro estiver muito perto — aceitável pois esse segmento seria coberto pelo carro.

**Risco:** Baixo — ajuste de parâmetro global do SGL. Verificar se afeta a profundidade do HUD ou outros objetos 2D.

**Como encontrar o valor atual:** Buscar `slZClip` ou `SRL::Scene3D::SetClip` no codebase para localizar onde o near-clip é configurado hoje.

---

### Solução B — Subdivisão de Quads Próximos à Câmera (RISCO MÉDIO)

Quads longos têm centroide distante dos vértices — a imprecisão do sort e o clipping afetam o polígono inteiro. Subdividir cada quad em 2–4 quads menores ao carregar o segmento:

- Reduz o span do polígono individual
- Centroide de cada sub-quad é mais representativo da sua profundidade real
- Clipping afeta apenas a sub-parte do quad que cruza o near-plane, não o quad inteiro

**Arquivo:** [src/track_system.cxx](src/track_system.cxx) — na fase de build de `segmentDrawReady` ou na etapa de `PrimeRuntimeScratchCapacities`

**Implementação sketch:**

```cpp
// Ao construir os verts de um segmento, identificar quads com span_z > kSubdivideThreshold
// e inserir verts intermediários
static constexpr float kSubdivideThreshold = 8.0f;  // calibrar com base no near-clip
```

**Risco:** Médio — aumenta contagem de faces por segmento. Impacta `SGL_MAX_POLYGONS` e o budget do coordinator. Requer re-calibração de `initialFaces`.

---

### Solução C — Skip do Polígono Mais Próximo Quando Abaixo de Threshold (SIMPLES)

Em `RenderVisibleSegmentOrderStabilized`, para o segmento com `windowRank = 0` (o mais próximo da câmera), verificar a distância Chebyshev da câmera ao segmento:

```cpp
// Ao iterar orderedHandles, o último segmento é o mais próximo
// Se a distância é menor que kNearSegmentSkipThreshold → skip parcial ou total
static constexpr int32_t kNearSegmentSkipThreshold = 0x00050000; // ~5.0 em Fxp 16.16
```

Quando a distância é abaixo do threshold, não submeter o segmento ao SGL — o carro cobre completamente essa área de qualquer forma.

**Risco:** Baixo — pode causar artefato de "buraco" momentâneo se o carro não cobrir toda a área do segmento. Ajustar threshold com base na largura do modelo do carro.

---

## Problema 3 — Seam Visual na Fronteira entre Segmentos

### Causa Raiz

Quando a câmera está entre dois segmentos, o sort list pode inverter a ordem de submissão de polígonos no limite entre os dois segmentos (o centroide Z de um polígono do segmento "mais longe" pode ser calculado como mais próximo que um polígono do segmento "mais perto"). Resulta em flickers e seams visíveis.

### Solução — Overlap Margin entre Segmentos (COMPLEMENTAR À SOLUÇÃO A)

Com o sort priority boost do carro implementado (Solução A do Problema 1), a fronteira entre segmentos passa a ser irrelevante para o artefato principal. Para o seam em si, verificar se a pista tem geometria com uma pequena sobreposição (overlap) entre segmentos adjacentes para não expor o "gap" visual.

Isso é um problema de dados (arquivo `trkrdr.bin`) e não de código — verificar nas ferramentas de exportação se a geometria dos segmentos fecha corretamente.

---

## Sequência de Implementação Recomendada

| # | Ação | Arquivo | Impacto | Complexidade | Risco |
|---|------|---------|---------|--------------|-------|
| 1 | Adicionar `sortPriorityBoost` no carro (Problema 1, Solução A) | [mesh_renderer.hpp](src/mesh_renderer.hpp), [mesh_renderer.cxx](src/mesh_renderer.cxx), [car_system.cxx](src/car_system.cxx) | ALTO | BAIXO | BAIXO |
| 2 | Aumentar near-clip (Problema 2, Solução A) | [main.cxx](src/main.cxx) | ALTO | BAIXO | BAIXO |
| 3 | Skip de segmento abaixo de threshold (Problema 2, Solução C) | [track_system.cxx](src/track_system.cxx) | MÉDIO | BAIXO | BAIXO |
| 4 | Depth bias nos segmentos do carro (Problema 1, Solução B) | [track_system.cxx](src/track_system.cxx) | MÉDIO | MÉDIO | MÉDIO |
| 5 | Subdivisão de quads longos (Problema 2, Solução B) | [track_system.cxx](src/track_system.cxx) | ALTO | ALTO | MÉDIO |

---

## Como Saturn e PlayStation Resolviam Isso (Referência Histórica)

### Sega Saturn
- **Sort list com priority bits:** Os ATTRs do SGL têm 2 bits de "prioridade" no sort field que somam ou subtraem da chave Z computada. Jogos de corrida como Sega Rally usavam esses bits para garantir que o carro sempre aparecesse na frente da pista em qualquer situação de Z-fighting.
- **BSP trees:** Alguns títulos (Ridge Racer Saturn port, homebrew Project Z-Treme) usavam BSP pré-computadas para determinar a ordem de draw correta sem depender do sort list — eliminando completamente o Z-fighting ao custo de mais RAM e tempo de build.
- **Geometria sobreposta + draw order fixo:** A abordagem mais comum em jogos de corrida arcade era simplesmente não confiar no sort automático: a pista era desenhada primeiro, depois o carro com objetos 3D renderizados com o carro garantidamente "à frente". Isso funciona porque a câmera nunca vê a pista e o carro pelo mesmo ângulo em jogos de racing clássico.
- **Near-clip mais agressivo:** Jogos de Saturn como Sega Rally e Daytona USA com pistas fechadas usavam near-clip longe o suficiente para nunca expor os vértices dos quads de asfalto no frustum próximo.

### Sony PlayStation
- **Warping afim:** O PS1 NÃO tem Z-buffer e NÃO tem perspectiva correta por pixel — por isso tem o "wobbly polygon" característico. A técnica era simplesmente aceitar o warping e ajustar a câmera para minimizar quads muito longos no view frustum.
- **Polygon subdivision pré-baked:** Desenvolvedores de PS1 em jogos de corrida (Gran Turismo, Ridge Racer) subdividiam as pistas em geometria densa para reduzir o span de cada quad individual, minimizando o warping afim e melhorando o Z-sort centroide.
- **Object over-surface trick:** O carro era elevado 0.1–0.5 unidades acima da superfície da pista geometricamente para garantir que nenhum polígono da pista tivesse centroide Z mais próximo da câmera que o carro.

---

## Critérios de Aceitação

### Ação 1 — Sort Priority Boost
- Carro visível por cima da pista em todas as posições ao longo do circuito
- Nenhum polígono de asfalto aparecendo na frente do modelo do carro
- Sem artefatos de carro "flutuando" sobre objetos de fundo (over-prioritized)

### Ação 2 — Near-Clip
- Segmento mais próximo não distorce ao ser abordado pela câmera
- Carro não fica parcialmente clipado ao frear (câmera não se aproxima demais)
- HUD 2D e overlays de debug sem artefatos de clipping

### Ação 3 — Skip de Segmento Próximo
- Sem buraco visível na pista quando o carro está sobre o segmento mais próximo
- Threshold calibrado para cobrir a largura do carro

### Ações 1+2 combinadas
- Rodada completa de 3 voltas sem sobreposição de pista sobre carro
- Sem flicker de Z-fighting nas fronteiras de segmento
- `FPS: 30.0` sem regressão (as ações têm overhead de CPU negligível)
