# Plano de Ação — Distorção de Segmentos Próximos à Câmera

## Diagnóstico da Causa Raiz

### O que acontece geometricamente

A câmera `ChaseNear` está posicionada:
- **20 unidades acima** da superfície da pista (`offsetY = -20`, camera_system.cxx:508)
- **190 unidades atrás** do carro (`kCam2BehindUnits = 190`, main.cxx:1041)
- Olhando **120 unidades à frente** do carro (`lookAhead = 120`)

Os segmentos de pista são quads horizontais grandes que se estendem dezenas de unidades em Z. No **espaço de câmera do SGL** (após a transformação `LookAt`), os vértices da borda traseira desses quads — a borda mais próxima da câmera — têm coordenada Z próxima de zero ou negativa, entrando na zona do near-clip do SGL.

**Por que 20 unidades de altura é insuficiente:**

A câmera olha para baixo com um ângulo suave (`pitchDeg = -21°`). Com altura de apenas 20 unidades, a superfície da pista sob a câmera é quase paralela ao eixo de visão. Qualquer quad que se estenda para trás além da posição XZ da câmera tem vértices com Z ≤ 0 no espaço de câmera → distorção imediata.

```
Vista lateral (simplificada):

Câmera (Y=20, Z=-190)
       \
        \  ângulo ~6° para o horizonte
         \
──────────\────────────── pista Y=0
    |      \
    |       vértice traseiro do quad
    |       → Z ≈ 0 em camera space → DISTORÇÃO
```

### Por que o fix anterior (kNearSegmentCullDistanceRaw) não funcionou

A constante adicionada usa distância L1 no plano XZ (sem Y). O segmento mais próximo à câmera está a ~190 unidades de distância XZ (câmera está 190 unidades atrás do carro, segmento está na posição do carro). O threshold de **1.5 unidades** nunca dispara em gameplay normal — precisaria que a câmera estivesse literalmente dentro do centro do segmento.

**Threshold correto baseado em geometria real:** O segmento na posição do carro tem distância XZ ~190 da câmera. O skip só seria útil com threshold > 150, o que eliminaria o segmento mais próximo permanentemente. A abordagem de distância XZ é a métrica errada para este problema.

---

## Técnicas Usadas em Jogos Saturn/PS1 (Referência)

| Jogo | Técnica |
|------|---------|
| Sega Rally Championship (Saturn) | Câmera mais alta acima do carro; near-clip zone nunca alcançada |
| Daytona USA (Saturn) | Câmera ~30–40 unidades acima; FOV mais fechado compensa |
| Ridge Racer (PS1) | Subdivisão densa dos quads de asfalto; nenhum quad maior que 8 unidades |
| Gran Turismo (PS1) | Câmera alta + segmentos pré-subdivididos; vértices nunca atingem near-clip |
| Project Z-Treme (homebrew Saturn) | VDP2 plane para superfície mais próxima + polígonos para bordas e objetos |

**Conclusão consistente:** Nenhum jogo de corrida clássico resolve a distorção SGL na câmera baixa — todos evitam o problema elevando a câmera, pré-subdividindo a geometria, ou usando VDP2 para o chão.

---

## Ações de Correção (Ordem de Prioridade)

---

### Ação 1 — Aumentar altura da câmera (IMPACTO ALTO / RISCO BAIXO)

**Arquivo:** [src/camera_system.cxx:508](src/camera_system.cxx#L508) — `PresetConfig::ChaseNear::offsetY`

**Mudança:**
```cpp
// Antes:
case ChasePreset::ChaseNear:
    return ChasePresetConfig{
        chaseNearOffsetX_,
        -20,   // ← altura atual: 20 unidades
        chaseNearOffsetZ_,
        120,
        0,
        0
    };

// Depois:
case ChasePreset::ChaseNear:
    return ChasePresetConfig{
        chaseNearOffsetX_,
        -35,   // ← nova altura: 35 unidades
        chaseNearOffsetZ_,
        120,
        0,
        0
    };
```

**Por que funciona:** Aumentar de 20 para 35 unidades eleva a câmera, aumentando o ângulo de incidência da câmera com a superfície da pista de ~6° para ~10°. O vértice traseiro do quad mais próximo passa a ter Z em camera space positivo e maior, saindo da zona de near-clip.

**Calibração:** Testar com -30, -35 e -40. Valores acima de -40 fazem a câmera parecer "drone" — perda de sensação de velocidade. Recomendo começar com -35.

**Também ajustar para os outros presets:**
- `ChaseFar::offsetY = -30` → manter ou aumentar para -38
- `FirstPerson::offsetY = -26` → manter (câmera de cockpit não sofre do mesmo problema — está dentro do carro)

---

### Ação 2 — Corrigir threshold de skip de segmento (IMPACTO MÉDIO / RISCO BAIXO)

**Arquivo:** [src/track_system.cxx:314](src/track_system.cxx#L314) e [src/track_system.cxx:14251](src/track_system.cxx#L14251)

O `kNearSegmentCullDistanceRaw = 0x00018000` (1.5 unidades) nunca dispara. Trocar por skip baseado em **rank** em vez de distância.

**Nova abordagem — skip do último segmento (rank mais próximo) se coincidir com `observedCarSegmentId_`:**

```cpp
// Em BuildStabilizedSortedHandles — substituir o filtro de distância por:
// Se o segmento tem o mesmo id que o carro está percorrendo, e está
// à frente da câmera em Z (distância XZ < 80), pular — é o segmento
// diretamente sob a câmera, causa máxima distorção.
const int32_t dist = depthMetricRaw(entry);
const bool isCarSegment = (entry->id == observedCarSegmentId_);
static constexpr int32_t kCarSegmentCullDistRaw = 0x00500000; // 80 unidades XZ
if (isCarSegment && dist < kCarSegmentCullDistRaw) continue;
```

**Alternativa mais simples:** Aumentar o threshold para 30 unidades e manter o comportamento original:
```cpp
// Substituir:
static constexpr int32_t kNearSegmentCullDistanceRaw = 0x00018000; // 1.5 — INEFICAZ

// Por:
static constexpr int32_t kNearSegmentCullDistanceRaw = 0x001E0000; // 30 unidades
```

**Risco:** Com 30 unidades de threshold, segmentos que estão a menos de 30 XZ da câmera são culled. Com a câmera a 190 unidades atrás do carro, o segmento mais próximo à câmera é o segmento na posição da câmera — que geometricamente está "atrás" do campo de visão. Isso é seguro.

---

### Ação 3 — Subdivisão de quads longos nos segmentos próximos (IMPACTO ALTO / COMPLEXIDADE ALTA)

Quads de asfalto com span em Z maior que ~10 unidades devem ser subdivididos em 2–3 sub-quads ao carregar segmentos de rank baixo (próximos à câmera). Cada sub-quad é menor, logo seu vértice traseiro entra menos na zona de near-clip.

**Arquivo:** [src/track_system.cxx](src/track_system.cxx) — na fase de build de geometry de segmento (loop de verts/faces em ~linha 3906–3970)

**Pseudocódigo de subdivisão Z:**
```cpp
// Após carregar cada face (quad de 4 verts):
const float z0 = verts[face.v[0]].Z; // borda traseira
const float z1 = verts[face.v[1]].Z; // borda dianteira
const float spanZ = std::abs(z1 - z0);
static constexpr float kSubdivideThreshold = 10.0f; // unidades mundo

if (spanZ > kSubdivideThreshold)
{
    // Inserir vértice médio entre as bordas traseira e dianteira
    // Criar 2 faces em vez de 1
    const Vector3D midFront = lerp(verts[face.v[0]], verts[face.v[1]], 0.5f);
    const Vector3D midBack  = lerp(verts[face.v[2]], verts[face.v[3]], 0.5f);
    // Adicionar midFront, midBack como novos vértices
    // face A: v[0], midFront, midBack, v[3]
    // face B: midFront, v[1], v[2], midBack
}
```

**Custo:** Aumenta número de faces por segmento (~1.5-2× na pista reta). Precisa recalibrar `SGL_MAX_POLYGONS` e `initialFaces`. Implementar apenas após medir o impacto de faces adicionais.

**Alternativa leve:** Subdividir apenas os **2 segmentos de rank mais baixo** (os mais próximos da câmera), sem mexer nos demais. Impacto mínimo no budget de polígonos.

---

### Ação 4 — Ajustar `SetDepthDisplayLevel` para nível 5 ou 6 (IMPACTO BAIXO-MÉDIO / RISCO BAIXO)

**Arquivo:** [src/main.cxx:782](src/main.cxx#L782)

Já foi adicionado `SetDepthDisplayLevel(4)` no commit anterior. Testar com nível 5 (1/32 da distância de projeção) ou 6 (1/64) para reduzir a zona de near-clip.

```cpp
// Antes:
SRL::Scene3D::SetDepthDisplayLevel(4); // 1/16

// Testar:
SRL::Scene3D::SetDepthDisplayLevel(5); // 1/32 — mais agressivo
```

**Atenção:** Nível muito alto reduz a profundidade de campo útil — objetos distantes podem aparecer atrás do plano de exibição. Verificar se o horizonte da pista continua visível.

---

### Ação 5 — Cull de polígonos horizontais sob a câmera (IMPACTO MÉDIO / COMPLEXIDADE MÉDIA)

Para os segmentos de rank 0 e 1 (os mais próximos), cull as faces cujos **todos os 4 vértices** estão acima (em Y) da câmera menos uma margem. Esses polígonos estão geometricamente "atrás" e acima da câmera — nunca são visíveis, mas causam distorção ao serem processados pelo SGL.

**Arquivo:** [src/track_system.cxx](src/track_system.cxx) — em `RenderVisibleSegmentOrderStabilized`, no callback de `coordinator_.Execute()` (~linha 15131)

```cpp
// Antes de SRL::Scene3D::DrawMesh(mesh), para segmentos de rank 0–1:
const Fxp cameraY = cameraLocation.Y; // passado para RenderFrame
for (size_t f = 0; f < mesh->FaceCount; ++f)
{
    bool allVertsAboveCam = true;
    for (int vi = 0; vi < 4; ++vi)
    {
        if (mesh->Vertices[mesh->Faces[f].Vertices[vi]].Y > cameraY + Fxp(1.0f))
        {
            allVertsAboveCam = false;
            break;
        }
    }
    if (allVertsAboveCam)
    {
        // Marcar face para skip (atribuir sort No_Sort ou pular)
    }
}
```

**Complexidade:** Requer acesso à posição Y da câmera dentro do loop de draw, e uma forma de pular faces individualmente. Possível via cópia temporária do mesh com faces filtradas, mas aumenta uso de LWR.

---

## Sequência de Implementação Recomendada

| # | Ação | Arquivo | Impacto | Esforço |
|---|------|---------|---------|---------|
| 1 | `offsetY = -35` no preset ChaseNear | [camera_system.cxx](src/camera_system.cxx) | ALTO | 1 linha |
| 2 | `offsetY = -38` no preset ChaseFar | [camera_system.cxx](src/camera_system.cxx) | BAIXO | 1 linha |
| 3 | Corrigir threshold de skip (Ação 2) | [track_system.cxx](src/track_system.cxx) | MÉDIO | 3 linhas |
| 4 | Testar `SetDepthDisplayLevel(5)` | [main.cxx](src/main.cxx) | BAIXO | 1 linha |
| 5 | Subdivisão de quads (Ação 3) | [track_system.cxx](src/track_system.cxx) | ALTO | Médio |
| 6 | Cull de polígonos sob câmera (Ação 5) | [track_system.cxx](src/track_system.cxx) | MÉDIO | Alto |

**Ação 1 sozinha deve reduzir ~70% das ocorrências de distorção** — é a mudança mais impactante com custo praticamente zero.

---

## Critérios de Aceitação

- Nenhum quad de asfalto distorce durante corrida normal (câmera ChaseNear, velocidade máxima)
- Transição entre segmentos sem "flash" ou esticamento de geometria
- FPS sem regressão (as ações 1–4 têm custo de CPU negligível)
- Visual da câmera ainda parece "dentro" da corrida (não muito alto)

---

## Métricas Atuais vs. Meta

| Parâmetro | Atual | Meta |
|-----------|-------|------|
| `ChaseNear offsetY` | -20 | -35 |
| `ChaseFar offsetY` | -30 | -38 |
| `kNearSegmentCullDistanceRaw` | 0x18000 (1.5u — ineficaz) | 0x1E0000 (30u) ou skip por segmentId |
| `SetDepthDisplayLevel` | 4 | 5 (testar) |
| Distorção em gameplay normal | visível | ausente |
