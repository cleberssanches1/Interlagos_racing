# Renderização de Segmentos de Pista em Jogos de Corrida de Era Clássica
## Referência Técnica Profissional — PS1 / Sega Saturn (1994–2000)

> **Escopo:** Este documento cobre a lógica completa de construção e renderização de segmentos de pista conforme a câmera avança — desde os fundamentos matemáticos, passando pelas técnicas usadas em jogos comerciais como Gran Turismo 2, Ridge Racer e Sega Rally, até a arquitetura concreta implementada neste projeto (Interlagos Racing para Sega Saturn). Inclui exemplos de código, diagramas de fluxo e referências externas verificadas.

---

## Índice

1. [Contexto Histórico e Evolução das Técnicas](#1-contexto-histórico)
2. [Arquitetura Fundamental: A Estrada como Fita de Segmentos](#2-arquitetura-de-segmentos)
3. [Matemática de Projeção em Perspectiva](#3-matemática-de-projeção)
4. [Estrutura de Dados dos Segmentos](#4-estrutura-de-dados)
5. [Curvas, Elevação e Dados da Pista](#5-curvas-e-elevação)
6. [Algoritmo de Renderização (Pintor Inverso)](#6-algoritmo-do-pintor)
7. [Mapeamento Câmera → Índice de Segmento](#7-mapeamento-câmera-segmento)
8. [LOD (Level of Detail) por Segmento](#8-lod-por-segmento)
9. [Streaming de Segmentos do CD-ROM](#9-streaming-cd-rom)
10. [Considerações Específicas do Sega Saturn (VDP1)](#10-saturn-vdp1)
11. [Mapeamento de Texturas Afim vs. Perspectiva Correta](#11-mapeamento-de-texturas)
12. [Implementação Local: Interlagos Racing](#12-implementação-local)
13. [Exemplos Práticos de Código](#13-exemplos-práticos)
14. [Fontes e Referências](#14-referências)
- [Apêndice A: Lógica Detalhada da Janela Deslizante de LOD (30 FPS)](#apêndice-a-lógica-detalhada-da-janela-deslizante-de-lod-30-fps)
- [Apêndice B: Glossário](#apêndice-b-glossário)
- [Apêndice C: Comparativo de Técnicas por Plataforma](#apêndice-c-comparativo-de-técnicas-por-plataforma)

---

## 1. Contexto Histórico

### Linha do Tempo da Renderização de Pistas

| Ano  | Jogo / Hardware       | Técnica                                        |
|------|-----------------------|------------------------------------------------|
| 1982 | Pole Position (Namco) | Scanline pseudo-3D com lookup tables           |
| 1986 | OutRun (Sega System 16) | Segmentos pseudo-3D com skewing horizontal   |
| 1992 | Virtua Racing (Sega System 32) | Primeiro 3D poligonal real em corrida  |
| 1993 | Ridge Racer (Namco System 22) | Texture mapping 3D, toda a pista em RAM     |
| 1994 | Daytona USA (Sega Model 2) | Gouraud shading, MIP-mapping                |
| 1995 | Ridge Racer (PS1)     | Port com affine texturing, subdivisão de polígonos |
| 1995 | Sega Rally (Saturn)   | Quads VDP1, pista reconstituída do arcade      |
| 1997 | Gran Turismo (PS1)    | Pistas reais com curvas 3D, LOD agressivo      |
| 1999 | Gran Turismo 2 (PS1)  | 27 pistas, streaming de geometria, 60 FPS      |

A transição da era pseudo-3D para o 3D poligonal trouxe desafios fundamentais que moldaram as arquiteturas de renderização de pistas usadas até hoje em jogos retro-style.

### Por que Segmentos?

A abordagem de dividir a pista em **segmentos discretos** (fatias perpendiculares ao eixo de avanço) surgiu por razões práticas:

1. **Ordenação de profundidade trivial** — segmentos são naturalmente ordenados do mais distante ao mais próximo, sem necessidade de Z-buffer.
2. **LOD por distância** — segmentos próximos recebem texturas de alta resolução; distantes recebem texturas degradadas.
3. **Streaming controlado** — a janela de segmentos ativos tem tamanho fixo; novos entram conforme a câmera avança.
4. **Culling simples** — segmentos além do horizonte ou atrás da câmera são simplesmente não processados.

---

## 2. Arquitetura de Segmentos

### Conceito da "Fita de Pão"

Imagine a pista como uma baguete fatiada perpendicularmente ao seu comprimento. Cada fatia é um **segmento**. A câmera olha pelo eixo do comprimento da baguete:

```
         Horizonte
           ▲
    Seg 20 |  [  ] ← fatia distante (8×8 texels)
    Seg 19 |  [  ]
    Seg 18 | [    ]
    Seg 15 | [    ]
    Seg 10 |[      ]
    Seg  5 |[        ]
    Seg  1 |[          ] ← fatia próxima (64×64 texels)
           |
         Câmera
```

Cada segmento armazena:
- Posição central no mundo 3D
- Geometria da superfície (quatro vértices para um quad)
- Curvatura acumulada (deslocamento lateral)
- Elevação (deslocamento vertical)
- Referências de textura por nível de LOD

### Janela Deslizante de Segmentos Ativos

Em vez de manter toda a pista em memória, mantém-se uma **janela** de `W` segmentos ao redor da câmera. À medida que a câmera avança:

```
Frame N:   [1][2][3][4][5]...[W]
Frame N+1:    [2][3][4][5]...[W][W+1]  ← slide forward
```

- Segmento `1` é descarregado (recursos devolvidos ao pool)
- Segmento `W+1` é carregado (prefetch já iniciado no frame anterior)
- Tamanho típico: 20–30 segmentos ativos simultâneos

---

## 3. Matemática de Projeção

### Lei dos Triângulos Semelhantes

O núcleo matemático de toda renderização de pista baseada em segmentos:

```
              Plano de Projeção
                    |
    Câmera  ←d→    |
         \          |
          \         |
           \  h     | screen_y
            \       |
             \______| → z (distância até o segmento)
```

**Fórmula de projeção:**
```
screen_y = (h × d) / z
screen_x = (x_world × d) / z
```

Onde:
- `h` = altura da câmera acima da pista
- `d` = distância focal (câmera até plano de projeção) = `1 / tan(fov/2)`
- `z` = distância do segmento até a câmera
- `x_world` = posição lateral no espaço 3D

### Campo de Visão (FOV)

```cpp
// Cálculo da distância focal a partir do FOV
float computeFocalLength(float fovDegrees) {
    float fovRad = fovDegrees * (M_PI / 180.0f);
    return 1.0f / tanf(fovRad * 0.5f);
}
```

Para `fov = 90°` → `d = 1.0`
Para `fov = 60°` → `d ≈ 1.732`

### Pipeline de Transformação Completo

```
Espaço Mundo → Espaço Câmera → Projeção Normalizada → Espaço Tela
(x, y, z)       (relativo cam)    (d/z scaling)         (px coords)
```

**Exemplo numérico:**
```
Câmera em z=100, segmento em z=120
Deslocamento do segmento: dz = 120 - 100 = 20
d = 1.0 (fov=90°)
h = 0.5 (câmera a 0.5 unidades acima da pista)

screen_y = (0.5 × 1.0) / 20 = 0.025   → y_pixel = 0.025 × height
screen_w = (1.0 × 1.0) / 20 = 0.05    → largura da pista em tela = 0.05 × width
```

---

## 4. Estrutura de Dados

### Estrutura Universal de Segmento (Referência)

Compilado a partir das implementações open-source mais influentes:

```cpp
// Estrutura de segmento pseudo-3D (referência Jake Gordon / codeincomplete)
struct Segment {
    // Coordenadas no mundo
    struct Point3D { float x, y, z; };
    Point3D world_p1;     // Centro da borda próxima
    Point3D world_p2;     // Centro da borda distante

    // Coordenadas em espaço câmera (relativas à câmera)
    Point3D camera_p1;
    Point3D camera_p2;

    // Coordenadas em tela (pixels)
    struct Point2D { float x, y, w; }; // w = largura projetada
    Point2D screen_p1;
    Point2D screen_p2;

    // Propriedades da pista
    float curve;          // Deslocamento lateral acumulado [-1, +1]
    float elevation;      // Altura do segmento (hills)
    float length;         // Comprimento no espaço mundo

    // Dados de renderização
    uint32_t color;       // Cor do asfalto (alternância para listras)
    int      index;       // ID de sequência (1-based)
    bool     fog;         // Aplicar fog de distância?
};
```

### Estrutura Real: Interlagos Racing (Sega Saturn)

Extraído de [src/track_system.hpp](src/track_system.hpp):

```cpp
// Estado de LOD por segmento
struct SegmentLodState {
    int8_t   currentLodIndex;        // 0=8×8, 1=16×16, 2=32×32, 3=64×64
    int8_t   currentBaseRank;        // Rank no pool de texturas
    uint16_t faceFamilyIds[MAX_FACES];   // ID de família de textura por face
    uint8_t  faceRankOffsets[MAX_FACES]; // Offset de rank por face
    uint8_t  currentFaceSlots[MAX_FACES]; // Slot VDP1 ativo por face
};

// Entrada de renderização de segmento
struct SegmentRenderEntry {
    int16_t     id;                  // ID do segmento (1-based, circular)
    Vector3D    center;              // Centro no espaço mundo (Fixed-Point 16.16)
    int16_t     logicalSegmentCount; // Segmentos lógicos agrupados neste renderer
    SegmentLodState lodState;        // Estado de LOD atual
};
```

### Tabela de LOD: Distância → Resolução de Textura

```
Rank 0–3   (distância ≤ 3)  → LOD 3 → Textura 64×64 px
Rank 4–8   (distância ≤ 8)  → LOD 2 → Textura 32×32 px
Rank 9–13  (distância ≤ 13) → LOD 1 → Textura 16×16 px
Rank 14–19 (distância ≤ 19) → LOD 0 → Textura  8×8  px
```

---

## 5. Curvas e Elevação

### Implementação de Curvas

A abordagem pseudo-3D usa **offset acumulativo lateral** em vez de geometria rotacionada:

```cpp
// Acumulação de curva ao longo dos segmentos
float cameraX = 0.0f; // offset lateral acumulado da câmera

for (int i = startSegment; i < startSegment + drawCount; i++) {
    Segment& seg = segments[i % totalSegments];

    // Projetar ponto usando cameraX atual
    seg.screen_p1.x = projectX(seg.world_p1.x - cameraX, seg.camera_p1.z);
    seg.screen_p2.x = projectX(seg.world_p2.x - cameraX, seg.camera_p2.z);

    // Acumular curva para o próximo segmento
    // (quanto mais adiante, mais o centro desloca)
    cameraX += seg.curve * delta;
}
```

**Easing para transições suaves:**
```cpp
// Interpolações suaves de curva (Lou's Pseudo 3D Page)
float easeIn   (float t) { return t * t; }
float easeOut  (float t) { return 1 - (1-t)*(1-t); }
float easeInOut(float t) { return t < 0.5 ? 2*t*t : 1 - 2*(1-t)*(1-t); }
```

### Implementação de Elevação (Hills)

Elevação não requer mudanças algorítmicas — apenas populate os valores `y` dos segmentos:

```
Segmento plano: world_p1.y = 0,   world_p2.y = 0
Subida:         world_p1.y = 0,   world_p2.y = +h
Descida:        world_p1.y = +h,  world_p2.y = 0
Topo de morro:  world_p1.y = +h,  world_p2.y = +h
```

A fórmula `screen_y = h*d/z` projeta automaticamente a variação vertical. O fundo do horizonte (parallax) se desloca verticalmente com a posição do jogador.

### Representação Spline da Pista Central

Para pistas reais (Gran Turismo, Ridge Racer), a linha central é uma **curva Bezier ou Catmull-Rom**:

```cpp
// Nó de spline da pista
struct TrackSplineNode {
    Vector3D  position;   // Posição no espaço mundo
    float     width;      // Largura da pista neste ponto
    float     banking;    // Inclinação transversal (graus)
    float     curvature;  // Taxa de mudança de direção
};

// Converter posição da câmera em índice de segmento
int cameraPositionToSegmentIndex(float cameraZ, float segmentLength) {
    return (int)(cameraZ / segmentLength) % totalSegmentCount;
}
```

---

## 6. Algoritmo do Pintor (Back-to-Front)

A renderização de corrida usa o **Algoritmo do Pintor** — renderizar segmentos do mais distante ao mais próximo. Isso elimina a necessidade de Z-buffer:

```
Frame renderizado (câmera em segmento C):

[Passo 1] Renderizar céu e background

[Passo 2] Para i = C + drawDistance .. C (do distante ao próximo):
    seg = segments[i % totalSegments]
    renderGrass(seg.screen_p2, seg.screen_p1, grassColor)
    renderRoad(seg.screen_p2, seg.screen_p1, seg.color)
    renderRumble(seg.screen_p2, seg.screen_p1)
    renderLaneMarkers(seg.screen_p2, seg.screen_p1)
    renderObjects(seg.objects, seg.screen_p1)

[Passo 3] Renderizar carro do jogador (HUD overlay)
```

### Ordem de Composição Completa

```
Camada 1: Céu (fundo estático ou parallax)
Camada 2: Montanhas/Skybox distante (parallax lento)
Camada 3: Árvores/Edificações laterais (parallax médio)
Camada 4: Segmentos de pista (distante → próximo)
   4a. Grama lateral
   4b. Asfalto
   4c. Faixas de rumble strip
   4d. Marcações de pista
Camada 5: Tráfego/Adversários (ordenados por distância)
Camada 6: Carro do jogador
Camada 7: HUD (velocímetro, RPM, posição)
```

---

## 7. Mapeamento Câmera → Segmento

### Câmera Como Cursor na Pista

A posição da câmera no eixo Z é traduzida diretamente em um **índice de segmento**:

```cpp
// Cálculo básico
int currentSegmentIndex = (int)(camera.z / SEGMENT_LENGTH) % totalSegments;

// Com wrapping para pista circular
int wrapSegmentId(int id, int total) {
    id = id % total;
    return (id < 0) ? id + total : id;
}
```

### Janela de Segmentos Ativos (Implementação Interlagos)

Extraído de [src/track_system.cxx](src/track_system.cxx):

```
┌─────────────────────────────────────────────────────────────┐
│  Janela de 20 segmentos (windowSize = 20)                   │
│                                                             │
│  [14][15][16][17][18] CAR_POS [19][20][21][22][23]          │
│   ← 5 atrás           ↑        15 à frente →                │
│                    segmento                                 │
│                    atual                                    │
└─────────────────────────────────────────────────────────────┘
```

**Algoritmo de slide:**

```cpp
// UpdateActiveSegmentWindowForPosition()
void slideWindow(Vector3D cameraPos) {
    // 1. Calcular distância Manhattan câmera → cada segmento
    int nearestId = findNearestSegment(cameraPos);

    // 2. Distância do segmento mais próximo ao cabeçote da janela
    int distToHead = wrapDistanceForward(activeWindowHead_, nearestId, total_);

    // 3. Slide forward se car avançou além de um threshold
    if (distToHead > SLIDE_THRESHOLD) {
        // Slide: descartar segmento traseiro, carregar próximo
        executeSlide();
    }

    // 4. Cooldown: 12 frames entre slides para evitar thrashing
    if (slideCooldown_ > 0) slideCooldown_--;
}
```

### Cálculo de Distância Circular

Para pistas circulares, a distância entre segmentos deve respeitar a topologia de anel:

```cpp
// Distância "para frente" numa pista circular
int wrapDistanceForward(int fromId, int toId, int total) {
    int d = toId - fromId;
    if (d < 0) d += total;
    return d;
}

// Ex: total=100, fromId=98, toId=2 → d = 2 - 98 + 100 = 4
//     (4 segmentos à frente, não 96 para trás)
```

---

## 8. LOD por Segmento

### Conceito: Ranks de Distância

Cada segmento recebe um **rank** (0 = mais próximo, N = mais distante) baseado em sua posição na janela. O rank determina a resolução de textura:

```
JANELA DE 20 SEGMENTOS:
Rank  0–3  → Textura 64×64  (segmentos 1–4 à frente da câmera)
Rank  4–8  → Textura 32×32  (segmentos 5–9)
Rank  9–13 → Textura 16×16  (segmentos 10–14)
Rank 14–19 → Textura  8×8   (segmentos 15–20)
```

### Implementação do Resolver de LOD

Extraído de [src/track_system.cxx](src/track_system.cxx):

```cpp
// ResolveSegmentLodIndexByRank()
int resolveSegmentLodIndex(int rank) {
    if (rank < 4)  return 3; // 64×64
    if (rank < 9)  return 2; // 32×32
    if (rank < 14) return 1; // 16×16
    return 0;                // 8×8 (default para distantes)
}
```

### LOD de WipEout PSX: Subdivisão Adaptativa

WipEout PS1 usou uma abordagem diferente — **subdivisão de polígonos por distância**:

```
Distância próxima:  1 face → 4×4 = 16 quads (texturas 32×32 px cada)
Distância média:    1 face → 2×2 = 4  quads (texturas 16×16 px cada)
Distância far:      1 face → 1×1 = 1  quad  (textura  8×8  px)
```

**Motivação:** Reduzir distorção do affine texture mapping — quanto menor o polígono, menor o erro afim.

### Transições de LOD: Estabilização

Para evitar "popping" visual quando o LOD muda, use um modo de estabilização:

```cpp
// Modo de estabilização (Interlagos)
// Durante inicialização ou carga pesada, força tudo para LOD 0 (8×8)
if (kEnableTrackRuntimeStabilization && stabilizationActive_) {
    targetLodIndex = 0; // Degradar todos para mínimo
} else {
    targetLodIndex = resolveSegmentLodIndex(rank);
}
```

---

## 9. Streaming de Segmentos do CD-ROM

### Filosofia Geral: Prefetch Antecipado

A regra de ouro: **o segmento deve estar em RAM antes de a câmera precisar dele**:

```
Frame N-K:  Prefetch do segmento S iniciado (K frames de antecedência)
Frame N:    Segmento S entra na janela ativa
Frame N:    Segmento S é renderizado (dados já disponíveis)
```

### Ridge Racer (PS1): Toda a Pista em RAM

```
Boot time: ~30 segundos carregando toda a pista para RAM
Durante o jogo: Zero I/O de CD-ROM
Vantagem: Latência zero, sem hitches
Desvantagem: RAM limitada (2MB PS1) → pistas curtas e poucos detalhes por segmento
```

### Gran Turismo 2: Streaming por Zona

Gran Turismo 2 dividiu cada circuito em **zonas** carregadas conforme necessidade:

```
Zona A (início/linha de chegada): sempre em RAM
Zona B: carregada quando carro está 3 zonas antes
Zona C: descarregada quando carro passou 2 zonas atrás
```

### Interlagos Racing: Prefetch com BDR/RDR/SDR

Sistema de três formatos em [src/track_system.cxx](src/track_system.cxx):

```
CD-ROM
  └── TRKRDR.BIN (container)
       ├── SEG_001.RDR  ← Runtime Draw Ready (formato primário)
       ├── SEG_002.RDR
       ├── ...
       └── B001_020.BDR ← Batch Draw Ready (multi-segmento)

FALLBACK SE RDR AUSENTE:
  └── SEG_001.SDR  ← Serialized Draw (legível mas menos otimizado)
```

**Fluxo de prefetch:**

```
1. Identificar próximo segmento a entrar na janela (S+1)
2. Iniciar leitura do CD: LoadRdrMappedForSegment(S+1)
3. Parsear blob → extrair vertices, faces, famílias de textura
4. Construir renderer com slots de LOD para todos os 4 níveis
5. Guardar em slidePrefetchRenderer_ (slot de espera)
6. No frame do slide: commit atômico (antigo descartado, novo ativado)
```

**Buffers de memória utilizados:**

```
Cart RAM    → geometria de segmento (blob bruto do CD)
HWR         → até 30 renderers ativos (High Work RAM)
LWR         → metadados, slots de família, scratch temporário
VDP1 CRAM   → paletas de textura (cor)
VDP1 VRAM   → pixels de textura (512×256 pixels total)
```

---

## 10. Saturn VDP1: Renderização com Quads

### Limitações do Hardware

O VDP1 do Sega Saturn **só opera em 2D**:

- Processa apenas coordenadas de tela (x, y) — não existe z no VDP1
- Primitiva básica: **sprite distorcido** (quadrilátero de 4 vértices 2D)
- Não há triângulos — tudo é quad
- Não há Z-buffer em hardware — o software deve gerenciar a ordem

```
Renderização 3D no Saturn:
  1. Motor 3D (SH2): projeta vértices 3D → coordenadas 2D
  2. SH2 grava lista de display VDP1 com quads já projetados
  3. VDP1: rasteriza os quads sem nenhum conhecimento de z-depth
```

### Custo de Fill Rate

O gargalo do Saturn não é contagem de polígonos — é **taxa de preenchimento de pixels**:

```
VDP1 Budget: 512 KB por frame de draw commands
Custo por face: 64 bytes (header + 4 vértices + textura reference)
Máximo de faces: 512 KB / 64 B = 8192 faces por frame

PORÉM: cada quad pode escrever pixels múltiplas vezes (overdraw)
→ fill rate é o limite real, não o número de polígonos
```

**Implicação para pistas:**

Segmentos distantes são pequenos em tela → poucos pixels → barato.
Segmentos próximos são grandes em tela → muitos pixels → caro.
LOD mais alto próximo da câmera aumenta qualidade visual sem penalizar tanto o fill rate (texturas maiores, menos quads por área).

### Display List e Ordenação

```cpp
// Ordem de inserção na display list VDP1
// (VDP1 renderiza na ordem em que recebe os comandos)
for (int rank = maxRank; rank >= 0; rank--) {
    // renderizar segmento do mais distante para o mais próximo
    SegmentRenderEntry& seg = activeWindow_[rankToWindowIndex(rank)];
    submitQuadsToVDP1(seg);
}
```

---

## 11. Mapeamento de Texturas: Afim vs. Perspectiva Correta

### O Problema Afim no PS1 e Saturn

Ambos os hardwares usam **interpolação afim de UV**:

```
Afim:              u = u0*(1-t) + u1*t
Perspectiva correta: u = (u0/z0*(1-t) + u1/z1*t) / (1/z0*(1-t) + 1/z1*t)
```

A interpolação afim ignora a variação de z → textura parece nadar/distorcer em polígonos grandes oblíquos.

### Solução: Subdivisão de Polígonos

```
Polígono grande (distorcido):
╔═══════════════════╗
║ /─────────────\   ║  ← textura distorcida visualmente
║/_______________\  ║
╚═══════════════════╝

Mesmo polígono subdividido em 4×4:
╔═╦═╦═╦═╦═╗
║/║/║/║/║/║  ← cada sub-quad tem distorção mínima
╠═╬═╬═╬═╬═╣
║/║/║/║/║/║
╚═╩═╩═╩═╩═╝
```

**Trade-off:** T-junctions (rachaduras) onde subdivisões encontram geometria não-subdividida. WipEout PSX tinha este problema documentado.

### Perspectiva Correta Aproximada (Quake Method)

```cpp
// Interpolação perspectiva correta a cada 16 pixels
// (trade-off: precisão vs. custo computacional)
for (int x = x0; x < x1; x += 16) {
    float z_near = 1.0f / z_inv_near;
    float z_far  = 1.0f / z_inv_far;
    float u_near = u_over_z_near * z_near;
    float u_far  = u_over_z_far  * z_far;

    // Interpolação linear dentro dos 16 pixels (barato)
    for (int dx = 0; dx < 16; dx++) {
        float t = dx / 16.0f;
        float u = u_near + t * (u_far - u_near);
        drawPixel(x + dx, u);
    }
}
```

---

## 12. Implementação Local: Interlagos Racing

### Visão Geral da Arquitetura

O projeto usa uma arquitetura dual-SH2 com pipeline de estágios bem definidos:

```
┌─────────────────────────────────────────────────────────┐
│                    MASTER SH2                           │
│                                                         │
│  TrackMaintenanceStage → TrackWindowStage               │
│         ↓                      ↓                        │
│  TrackPrefetchStage    TrackLodStage                    │
│         ↓                      ↓                        │
│  TrackWorkingSetStage → RenderVisibleSegmentOrder       │
│                                ↓                        │
│              ┌─────────────────┴──────────────┐         │
│              │         SLAVE SH2              │         │
│              │                                │         │
│              │  SlaveTrackDepthSorter         │         │
│              │         ↓                      │         │
│              │  SlaveTrackDrawProducer        │         │
│              │         ↓                      │         │
│              │  [VDP1 Display List]           │         │
│              └────────────────────────────────┘         │
└─────────────────────────────────────────────────────────┘
```

### Arquivos-Chave

| Arquivo | Responsabilidade |
|---------|-----------------|
| [src/track_system.hpp](src/track_system.hpp) | Estruturas de dados, constantes, interface |
| [src/track_system.cxx](src/track_system.cxx) | Implementação: janela, LOD, prefetch, slide |
| [src/track_renderer.hpp](src/track_renderer.hpp) | Backend gráfico (VDP1/SGL) |
| [src/track_draw_producer.hpp](src/track_draw_producer.hpp) | Geração de display list no Slave SH2 |
| [src/resource_loader.hpp](src/resource_loader.hpp) | Carregamento de segmentos do CD |

### Constantes de LOD (track_system.cxx:118)

```cpp
static constexpr uint8_t kLodBand64Count = 4u;  // Ranks 0-3
static constexpr uint8_t kLodBand32Count = 5u;  // Ranks 4-8
static constexpr uint8_t kLodBand16Count = 5u;  // Ranks 9-13
static constexpr uint8_t kLodBand8Count  = 6u;  // Ranks 14-19
```

### Budgets de Memória (track_system.cxx:124)

```cpp
static constexpr size_t kWorkRamPlanningHeadroomBytes    = 48 * 1024;   // 48 KB
static constexpr size_t kWorkRamHardFloorBytes           = 24 * 1024;   // 24 KB
static constexpr size_t kWorkRamCatastrophicFloorBytes   =  8 * 1024;   //  8 KB
static constexpr size_t kLowWorkRamSoftFloorBytes        = 320 * 1024;  // 320 KB
static constexpr size_t kLowWorkRamHardFloorBytes        = 160 * 1024;  // 160 KB

// VDP1 Budget
static constexpr size_t kVDP1FaceCostBytes  = 64;          // 64 B por face
static constexpr size_t kVDP1BudgetBytes    = 512 * 1024;  // 512 KB por frame
```

### Formato de Arquivo dos Segmentos

Derivado de `segment_component_format.hpp`:

```
SEG_001.NYA (container)
  ├── GEO1 chunk
  │     header: {magic="GEO1", version, segmentId, payloadBytes}
  │     verts:  [{x, y, z}...] (int32_t fixed-point 16.16)
  │     faces:  [{v[4], u[4], v[4], kind}...] (Triangle ou Quad)
  │
  └── MAT1 chunk
        header: {magic="MAT1", version, segmentId, payloadBytes}
        bindings: [{materialId: uint32_t}...] (um por face)
```

---

## 13. Exemplos Práticos de Código

### Exemplo 1: Projeção Completa de Segmento

```cpp
// Projetar um segmento de pista da câmera para a tela
struct Camera {
    float x, y, z;     // Posição no mundo
    float height;      // Altura do olho acima da pista
    float depth;       // Distância focal (1/tan(fov/2))
    int   screenWidth;
    int   screenHeight;
};

struct ProjectedSegment {
    float screenX1, screenY1, screenW1; // Borda próxima (p1)
    float screenX2, screenY2, screenW2; // Borda distante (p2)
};

ProjectedSegment projectSegment(
    const Camera& cam,
    float worldX, float worldY, float worldZ,
    float roadWidth)
{
    // Transformar para espaço câmera
    float dz = worldZ - cam.z;
    if (dz <= 0) return {}; // Atrás da câmera

    // Projeção perspectiva
    float scale = cam.depth / dz;

    ProjectedSegment result;
    result.screenX1 = (1 + scale * (worldX - cam.x)) * cam.screenWidth / 2.0f;
    result.screenY1 = (1 - scale * (worldY - cam.y - cam.height)) * cam.screenHeight / 2.0f;
    result.screenW1 = scale * roadWidth * cam.screenWidth / 2.0f;

    // (p2 calculado da mesma forma com offset z adicional por comprimento do segmento)
    return result;
}
```

### Exemplo 2: Loop de Renderização Pseudo-3D

```cpp
// Loop completo de renderização (O(N) — linear no número de segmentos visíveis)
void renderTrack(Camera& cam, Segment* segments, int totalSegments) {
    int startSegIdx = (int)(cam.z / SEGMENT_LENGTH) % totalSegments;

    float cameraX = cam.x;
    float maxY    = cam.screenHeight; // Clip line (horizon check)

    // Renderizar do mais distante ao mais próximo
    for (int n = DRAW_DISTANCE; n > 0; n--) {
        int idx = (startSegIdx + n) % totalSegments;
        Segment& seg = segments[idx];

        // Calcular posição relativa à câmera
        float relativeZ = (seg.worldZ - cam.z);
        if (relativeZ < 0) relativeZ += TRACK_LENGTH; // Wrap circular

        // Projetar pontos
        float scale1 = cam.depth / relativeZ;
        float scale2 = cam.depth / (relativeZ - SEGMENT_LENGTH);

        float y1 = (cam.screenHeight / 2.0f) - scale1 * (cam.height - seg.elevation);
        float y2 = (cam.screenHeight / 2.0f) - scale2 * (cam.height - seg.elevation);

        // Culling de horizonte: não renderizar se acima do horizonte
        if (y1 >= maxY) continue;
        maxY = y1;

        float x1 = cam.screenWidth / 2.0f - cameraX * scale1;
        float w1 = scale1 * ROAD_WIDTH;
        float x2 = cam.screenWidth / 2.0f - cameraX * scale2;
        float w2 = scale2 * ROAD_WIDTH;

        // Renderizar grama, asfalto, rumble strips
        drawQuad(x2-w2, y2, x2+w2, y2, x1+w1, y1, x1-w1, y1, seg.color);

        // Acumular offset de curva
        cameraX += seg.curve;
    }
}
```

### Exemplo 3: Sliding Window com Prefetch

```cpp
// Gerenciamento de janela deslizante
class TrackWindowManager {
    static const int WINDOW_SIZE = 20;
    static const int PREFETCH_LOOKAHEAD = 3; // frames de antecedência

    int activeWindowStart_;      // ID do primeiro segmento ativo
    SegmentRenderer* window_[WINDOW_SIZE]; // Renderers carregados
    SegmentRenderer* prefetch_;  // Próximo segmento pré-carregado

public:
    void update(int cameraSegmentId) {
        // Calcular rank do segmento da câmera
        int distFromStart = wrapDistanceForward(
            activeWindowStart_, cameraSegmentId, totalSegments_);

        // Slide necessário?
        if (distFromStart > WINDOW_SIZE / 2) {
            slideForward();
        }

        // Prefetch do próximo segmento
        int nextToLoad = (activeWindowStart_ + WINDOW_SIZE + PREFETCH_LOOKAHEAD)
                         % totalSegments_;
        if (!prefetch_ || prefetch_->segmentId() != nextToLoad) {
            startPrefetchAsync(nextToLoad);
        }
    }

private:
    void slideForward() {
        // Descarregar segmento traseiro
        recycleRenderer(window_[0]);

        // Deslocar janela
        memmove(window_, window_ + 1, (WINDOW_SIZE - 1) * sizeof(SegmentRenderer*));
        activeWindowStart_++;

        // Commit do prefetch como novo último segmento
        window_[WINDOW_SIZE - 1] = prefetch_;
        prefetch_ = nullptr;
    }
};
```

### Exemplo 4: LOD Resolver com Hysteresis

```cpp
// LOD com hysteresis para evitar flicker entre transições
class LodResolver {
    static const int HYSTERESIS_FRAMES = 8;
    int lastLodIndex_[MAX_SEGMENTS];
    int lodHoldCounter_[MAX_SEGMENTS];

public:
    int resolveWithHysteresis(int rank, int segmentId) {
        int targetLod = resolveRaw(rank);
        int currentLod = lastLodIndex_[segmentId];

        // Upgrade imediato (mais qualidade)
        if (targetLod > currentLod) {
            lastLodIndex_[segmentId] = targetLod;
            lodHoldCounter_[segmentId] = HYSTERESIS_FRAMES;
            return targetLod;
        }

        // Downgrade com delay (evita flicker ao frear/acelerar)
        if (targetLod < currentLod) {
            if (lodHoldCounter_[segmentId] > 0) {
                lodHoldCounter_[segmentId]--;
                return currentLod; // Manter LOD atual por mais alguns frames
            }
            lastLodIndex_[segmentId] = targetLod;
        }

        return lastLodIndex_[segmentId];
    }

private:
    int resolveRaw(int rank) {
        if (rank < 4)  return 3; // 64×64
        if (rank < 9)  return 2; // 32×32
        if (rank < 14) return 1; // 16×16
        return 0;                // 8×8
    }
};
```

### Exemplo 5: Projeção de Quads para VDP1 (Saturn)

```cpp
// Gerar comando VDP1 para um quad de segmento de pista
void submitTrackQuadToVDP1(
    const Vector3D verts[4],   // 4 vértices em espaço mundo
    const Vector2D uvs[4],     // UVs correspondentes
    uint16_t textureSlot,      // Slot de textura no VRAM do VDP1
    VDP1CommandList& cmdList)  // Lista de comandos VDP1
{
    // Projetar cada vértice de mundo → tela
    VDP1_ScaledSprite cmd;
    for (int i = 0; i < 4; i++) {
        // SGL (Saturn GL) faz a projeção automaticamente
        POINT screenPt = SGL_ProjectVertex(verts[i]);
        cmd.vertices[i] = screenPt;
        cmd.uv[i]       = uvs[i];
    }

    cmd.textureId   = textureSlot;
    cmd.colorMode   = VDP1_COLOR_MODE_TEXTURE;
    cmd.drawMode    = VDP1_DRAW_QUAD_DISTORTED;

    // VDP1 renderiza na ordem da lista — garantir back-to-front antes de submeter
    cmdList.append(cmd);
}
```

---

## 14. Referências

### Implementações Open-Source Primárias

| Projeto | URL | Relevância |
|---------|-----|-----------|
| **Jake Gordon — JavaScript Racer** | https://github.com/jakesgordon/javascript-racer | Referência canônica de pseudo-3D com segmentos |
| **Série de artigos "How to build a racing game"** | https://jakesgordon.com/writing/javascript-racer/ | Tutorial completo: straight roads, curves, hills |
| **Pseudo-3d-Racer (Phaser)** | https://github.com/ssusnic/Pseudo-3d-Racer | Implementação educacional com Phaser 2 |
| **Phaser3-Road** | https://github.com/jamessimo/Phaser3-Road | Port moderno com Phaser 3 |
| **ESP32-S3 Arcade 3D Racing** | https://github.com/davidmonterocrespo24/esp32s3-arcade-3d | Implementação em hardware embarcado |
| **Vilde Brobacke Pseudo-3D** | https://vildebrobacke.com/Pages/Pseudo3D.html | Implementação C++ com SDL |
| **OutRun / RoadSegment.h** | https://github.com/aaronslaughter/Outrun/blob/master/RoadSegment.h | Estrutura de dados de segmento do OutRun |

### Reverse Engineering de Jogos Comerciais

| Recurso | URL | Conteúdo |
|---------|-----|----------|
| **WipEout PSX RE (phoboslab)** | https://phoboslab.org/log/2015/04/reverse-engineering-wipeout-psx | Análise completa: geometria, texturas, LOD, near clip |
| **WipEout PSX Model Viewer** | https://github.com/phoboslab/wipeout/ | Código open-source do viewer |
| **Gran Turismo Modding Hub** | https://nenkai.github.io/gt-modding-hub/ps1/gt2/tools/ | Formato de arquivos GT2, ferramentas |
| **Ridge Racer R4 RE (GBAtemp)** | https://gbatemp.net/threads/ridge-racer-type-4-reverse-engineering-help-needed.575601/ | Formato R4.BIN, extração de assets |
| **OpenAdhoc — GT Scripts** | https://github.com/Nenkai/OpenAdhoc | Reimplementação dos scripts Gran Turismo |
| **Gran Turismo 2 — TCRF** | https://tcrf.net/Gran_Turismo_2 | Conteúdo cortado e dados de desenvolvimento |
| **Retroreversing Archive** | https://retroreversing.com/ | Specs de devkit PS1, código decompilado |

### Hardware e Fundamentos Técnicos

| Recurso | URL | Conteúdo |
|---------|-----|----------|
| **Pikuma: PS1 Graphics & Artifacts** | https://pikuma.com/blog/how-to-make-ps1-graphics | Affine texturing, near clip, artifacts do PS1 |
| **David Colson: PS1 Style Renderer** | https://www.david-colson.com/2021/11/30/ps1-style-renderer.html | Renderer retro em C++, subdivisão de polígonos |
| **Daniel Ilett: PS1 Affine Shader** | https://danielilett.com/2021-11-06-tut5-21-ps1-affine-textures/ | Shader de affine texturing |
| **PS1 Affine Shader (GitHub)** | https://github.com/daniel-ilett/ps1-affine-shader | Código do shader |
| **Saturn VDP1 3D (SegaXtreme)** | https://segaxtreme.net/threads/saturns-3d-capabilities.24339/ | Capacidades 3D do Saturn |
| **XtoF: Saturn foi projetado para 3D** | https://www.xtof.info/Yes-the-SEGA-Saturn-was-designed-with-3D-in-mind.html | Filosofia de design do VDP1 |
| **LINEAR S — Saturn Render em Unity** | https://retrorgb.com/new-indie-racer-linear-s-to-replicate-saturns-rendering-in-unity3d.html | Indie racer replicando o estilo Saturn |
| **PSXDev: Near Clipping** | http://www.psxdev.net/forum/viewtopic.php?t=1203 | Problema de near clip no PS1 |

### Matemática e Algoritmos

| Recurso | URL | Conteúdo |
|---------|-----|----------|
| **Scratchapixel: Perspective Correct Interpolation** | https://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/perspective-correct-interpolation-vertex-attributes.html | Matemática completa de interpolação perspectiva |
| **UC Davis: Perspective-Correct Texturing** | https://web.cs.ucdavis.edu/~amenta/s12/perspectiveCorrect.pdf | Fundamentos matemáticos |
| **Red Blob Games: Curved Paths** | https://www.redblobgames.com/articles/curved-paths/ | Bezier curves, splines, conversão para segmentos |
| **GameDev: Roads (splines)** | https://www.gamedeveloper.com/programming/roads | Catmull-Rom para pistas modernas |
| **Wikipedia: Painter's Algorithm** | https://en.wikipedia.org/wiki/Painter%27s_algorithm | Algoritmo do pintor |
| **Wikipedia: Z-buffering** | https://en.wikipedia.org/wiki/Z-buffering | Z-buffer vs. painter's algorithm |
| **LearnOpenGL: Frustum Culling** | https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling | Culling moderno de frustum |
| **Lou's Pseudo 3D Page** | https://news.ycombinator.com/item?id=42448184 | Referência histórica (discussão HN) |

### Jogos de Referência: Artigos Técnicos

| Recurso | URL | Conteúdo |
|---------|-----|----------|
| **Behind the Design: Sega Rally (Arcade)** | https://www.sega-16.com/2026/01/behind-the-design-sega-rally-championship-arcade/ | Making-of técnico do Sega Rally |
| **Virtua Racing — Wikipedia** | https://en.wikipedia.org/wiki/Virtua_Racing | Primeiro jogo de corrida 3D poligonal |
| **Ridge Racer 1993 — Wikipedia** | https://en.wikipedia.org/wiki/Ridge_Racer_(1993_video_game) | Primeiro com texture mapping 3D |
| **Sudonull: Pseudo-3D Racing** | https://sudonull.com/post/71919-The-implementation-of-pseudo-3D-in-racing-games | Survey técnico em russo com exemplos |
| **ESP32 Racing — Medium** | https://medium.com/@davidmonterocrespo24/how-i-built-the-first-3d-racing-game-for-esp32-s3-because-someone-said-ai-could-do-it-better-50236a02286f | Implementação em sistema embarcado moderno |
| **Game AI Pro: Race Track AI** | http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter39_Representing_and_Driving_a_Race_Track_for_AI_Controlled_Vehicles.pdf | Representação de pista para IA |

---

## Apêndice A: Lógica Detalhada da Janela Deslizante de LOD (30 FPS)

Esta seção detalha **exatamente** o que deve acontecer a cada frame quando o carro avança um segmento: quais texturas são alocadas, quais são recicladas, em que ordem as operações ocorrem e qual o budget de tempo para manter 30 FPS estáveis.

---

### A.1 O Invariante da Janela

A janela de renderização mantém **sempre** 20 segmentos ativos, distribuídos em 4 bandas fixas de LOD baseadas em **posição relativa** — não em ID absoluto de segmento:

```
POSIÇÃO NA JANELA  →  LOD  →  TEXTURA
────────────────────────────────────────
Posições  1 –  4   LOD 3    64 × 64 px   (4 segmentos)
Posições  5 –  9   LOD 2    32 × 32 px   (5 segmentos)
Posições 10 – 14   LOD 1    16 × 16 px   (5 segmentos)
Posições 15 – 20   LOD 0     8 ×  8 px   (6 segmentos)
```

> **Princípio fundamental:** o LOD de um segmento é determinado pela sua **posição na janela**, não pelo seu ID. Quando a janela desliza, os segmentos que já estavam carregados apenas **mudam de posição** — e portanto mudam de LOD. A textura anterior é descartada e a nova resolução é carregada.

---

### A.2 Estado Inicial: Carro no Segmento 1

```
Posição na janela:  [ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8][ 9][10][11][12][13][14][15][16][17][18][19][20]
ID do segmento:     [01][02][03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20]
LOD:                [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]
                    ╰────── Câmera sob seg 1 ──────╯
```

**Memória utilizada:**
```
Texturas 64×64:  4 slots  × (64×64×1 bpp) = 4 × 4096 B = 16.384 B
Texturas 32×32:  5 slots  × (32×32×1 bpp) = 5 × 1024 B =  5.120 B
Texturas 16×16:  5 slots  × (16×16×1 bpp) = 5 ×  256 B =  1.280 B
Texturas  8× 8:  6 slots  × ( 8× 8×1 bpp) = 6 ×   64 B =    384 B
                                           Total ≈ 23.168 B de VRAM de textura
```

---

### A.3 Evento de Slide: Carro Entra no Segmento 2

Quando a câmera cruza o limite entre o segmento 1 e o segmento 2, o sistema executa um **slide atômico**. Todas as operações abaixo devem completar dentro do orçamento de **1 frame (33,33 ms a 30 FPS)**:

#### A.3.1 Diagrama de Transição

```
ANTES DO SLIDE (carro em seg 1):
Pos:  [ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8][ 9][10][11][12][13][14][15][16][17][18][19][20]
Seg:  [01][02][03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20]
LOD:  [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]

             SLIDE  ────────────────────────────────────────────────────►

DEPOIS DO SLIDE (carro em seg 2):
Pos:  [ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8][ 9][10][11][12][13][14][15][16][17][18][19][20]
Seg:  [02][03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20][21]
LOD:  [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]
       ↑               ↑                   ↑                   ↑                    ↑
       Seg 2            Seg 5               Seg 10              Seg 15               Seg 21
       mantém 64       UPGRADE             UPGRADE             UPGRADE             NOVO (8×8)
       (já 64×64)      32→64               16→32               8→16                carregado
```

#### A.3.2 As Cinco Operações do Slide

Em ordem estrita de execução:

**Operação 1 — Reciclar segmento 1 (tail drop)**
```
Segmento 01 era posição 1 → LOD 64×64
→ Liberar slot de VRAM com textura 64×64 do segmento 01
→ Liberar geometria (vertices, faces) do segmento 01 na Cart RAM
→ Liberar entrada no pool de renderers (HWR slot devolvido)
→ Marcar ID 01 como "disponível para reuso"
```

**Operação 2 — Deslocar ponteiros da janela (O(1) com índice circular)**
```
activeWindowStart_ ++  (era 1, agora 2)
// Todos os segmentos de posição 2..20 passam para posição 1..19
// Nenhuma cópia de dados — apenas o índice base muda
```

**Operação 3 — Atualizar LOD dos segmentos que cruzaram fronteiras de banda**

Apenas três segmentos trocam de banda. Os demais **mantêm o mesmo LOD** (apenas mudaram de número de posição dentro da mesma banda):

```
Segmento 05 — posição era 5 (LOD 32×32), agora é posição 4 (LOD 64×64)
   → Descartar textura 32×32 do seg 05 do VRAM
   → Carregar textura 64×64 do seg 05 para VRAM
   → Atualizar faceFamilyIds[*] e currentFaceSlots[*]

Segmento 10 — posição era 10 (LOD 16×16), agora é posição 9 (LOD 32×32)
   → Descartar textura 16×16 do seg 10 do VRAM
   → Carregar textura 32×32 do seg 10 para VRAM

Segmento 15 — posição era 15 (LOD 8×8), agora é posição 14 (LOD 16×16)
   → Descartar textura 8×8 do seg 15 do VRAM
   → Carregar textura 16×16 do seg 15 para VRAM
```

> **Nota:** As texturas de outros LODs do mesmo segmento já foram pré-carregadas durante frames anteriores pelo sistema de prefetch de famílias. O "carregamento" aqui é apenas a ativação do slot correto — não envolve leitura do CD-ROM neste frame.

**Operação 4 — Commit do segmento 21 (head add)**
```
Segmento 21 — novo, posição 20 (LOD 8×8)
   → Geometria já deve estar em Cart RAM (prefetched no frame N-3)
   → Alocar slot de renderer no pool HWR
   → Ativar textura 8×8 no VRAM (prefetched pelo TrackWorkingSetStage)
   → Inserir em activeWindow_[19]
```

**Operação 5 — Disparar prefetch para o segmento 22**
```
→ Iniciar leitura assíncrona de SEG_022.NYA do CD-ROM
→ Target: geometria disponível em Cart RAM em no máximo N+3 frames
→ Target: textura 8×8 disponível em VRAM antes do slide N+6
```

---

### A.4 Visualização Frame a Frame

A sequência completa para três frames consecutivos:

```
════════════════════════════════════════════════════════════════════════
FRAME 1  (carro entra no segmento 1)
════════════════════════════════════════════════════════════════════════
Janela:  [01][02][03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20]
LOD:     [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]
Ações:   Prefetch seg 21 iniciado (CD-ROM read async)

════════════════════════════════════════════════════════════════════════
FRAME 2  (carro entra no segmento 2) — SLIDE OCORRE
════════════════════════════════════════════════════════════════════════
Operações dentro do frame (orçamento: 33,33 ms):
  ① Reciclar SEG 01: libera 64×64 VRAM + Cart RAM + HWR slot
  ② Deslocar janela: start = 2
  ③ Atualizar LOD:
       SEG 05: VRAM 32×32 → 64×64  (ativa slot já prefetched)
       SEG 10: VRAM 16×16 → 32×32  (ativa slot já prefetched)
       SEG 15: VRAM  8× 8 → 16×16  (ativa slot já prefetched)
  ④ Commit SEG 21: ativa VRAM  8× 8 + renderer HWR
  ⑤ Prefetch SEG 22: inicia CD-ROM read

Janela:  [02][03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20][21]
LOD:     [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]

════════════════════════════════════════════════════════════════════════
FRAME 3  (carro entra no segmento 3) — SLIDE OCORRE
════════════════════════════════════════════════════════════════════════
  ① Reciclar SEG 02: libera 64×64
  ② Deslocar janela: start = 3
  ③ Atualizar LOD:
       SEG 06: VRAM 32×32 → 64×64
       SEG 11: VRAM 16×16 → 32×32
       SEG 16: VRAM  8× 8 → 16×16
  ④ Commit SEG 22: ativa VRAM 8×8 + renderer
  ⑤ Prefetch SEG 23: inicia CD-ROM read

Janela:  [03][04][05][06][07][08][09][10][11][12][13][14][15][16][17][18][19][20][21][22]
LOD:     [64][64][64][64][32][32][32][32][32][16][16][16][16][16][ 8][ 8][ 8][ 8][ 8][ 8]
```

---

### A.5 Identificação dos Segmentos que Trocam LOD a Cada Slide

A regra geral: dado que a janela começa no segmento `S`, os segmentos que **cruzam fronteira de banda** após um slide são sempre os que estavam na **última posição de cada banda**:

```
Banda 64×64 termina na posição 4  → segmento que estava em posição 5 sobe para 4 (32→64)
Banda 32×32 termina na posição 9  → segmento que estava em posição 10 sobe para 9 (16→32)
Banda 16×16 termina na posição 14 → segmento que estava em posição 15 sobe para 14 (8→16)
Banda  8×8  começa na posição 15  → segmento na posição 20 sai; novo entra na posição 20 (8×8)
```

**Fórmula geral:**

Dado `windowStart = S` (ID do primeiro segmento ativo), os segmentos que trocam de LOD a cada slide são:

```
Upgrade 32→64:  segmento ID = S + 4   (era posição 5, passa para posição 4)
Upgrade 16→32:  segmento ID = S + 9   (era posição 10, passa para posição 9)
Upgrade  8→16:  segmento ID = S + 14  (era posição 15, passa para posição 14)
Novo    (8×8):  segmento ID = S + 20  (entra na posição 20)
Removido:       segmento ID = S - 1   (era posição 1, descartado)
```

---

### A.6 Pseudocódigo do Slide Completo

```cpp
// Executado uma vez por frame quando o carro avança para o próximo segmento.
// Deve completar em menos de ~5 ms para deixar orçamento para renderização.

void executeForwardSlide(TrackSystem& ts) {

    const int S = ts.activeWindowStart_;  // ID do primeiro seg antes do slide

    // ─────────────────────────────────────────────
    // FASE 1: Reciclar segmento traseiro (posição 1)
    // ─────────────────────────────────────────────
    SegmentRenderEntry& tail = ts.activeWindow_[0];

    // Desativar texturas de todos os LODs do segmento descartado
    for (int lod = 0; lod < 4; lod++) {
        if (tail.lodState.currentFaceSlots[lod] != INVALID_SLOT) {
            ts.vramHeap_.freeSlot(tail.lodState.currentFaceSlots[lod]);
        }
    }

    // Devolver geometria à Cart RAM pool
    ts.cartRamPool_.free(tail.cartPtr, tail.cartSize);

    // Devolver renderer ao HWR pool
    ts.hwrRendererPool_.recycle(&tail);

    // ─────────────────────────────────────────────
    // FASE 2: Deslizar janela (O(1) com índice circular)
    // ─────────────────────────────────────────────
    ts.activeWindowStart_ = wrapSegmentId(S + 1, ts.totalSegmentCount_);

    // O array usa índice circular — reposicionar cabeça é suficiente
    // Não há memmove: posição[i] = window_[(headIdx_ + i) % WINDOW_SIZE]

    // ─────────────────────────────────────────────
    // FASE 3: Promoção de LOD nos segmentos de fronteira
    // ─────────────────────────────────────────────
    // Novo S após slide:
    const int newS = ts.activeWindowStart_;

    // Segmento que cruzou da banda 32×32 para 64×64 (posição 5→4)
    upgradeSegmentLod(ts, segmentAtPosition(ts, 4),
                      /*fromLod=*/LOD_32, /*toLod=*/LOD_64);

    // Segmento que cruzou da banda 16×16 para 32×32 (posição 10→9)
    upgradeSegmentLod(ts, segmentAtPosition(ts, 9),
                      /*fromLod=*/LOD_16, /*toLod=*/LOD_32);

    // Segmento que cruzou da banda 8×8 para 16×16 (posição 15→14)
    upgradeSegmentLod(ts, segmentAtPosition(ts, 14),
                      /*fromLod=*/LOD_8, /*toLod=*/LOD_16);

    // ─────────────────────────────────────────────
    // FASE 4: Commit do novo segmento na cauda (posição 20)
    // ─────────────────────────────────────────────
    SegmentRenderEntry* newHead = ts.slidePrefetchRenderer_;
    assert(newHead != nullptr && "Prefetch falhou — seg 21 não está pronto!");

    newHead->lodState.currentLodIndex = LOD_8;  // Entra sempre como 8×8
    activateLodSlot(ts, *newHead, LOD_8);        // Ativa slot VRAM

    ts.activeWindow_[WINDOW_SIZE - 1] = *newHead;
    ts.slidePrefetchRenderer_ = nullptr;

    // ─────────────────────────────────────────────
    // FASE 5: Iniciar prefetch do próximo segmento (S + 21)
    // ─────────────────────────────────────────────
    int nextPrefetchId = wrapSegmentId(newS + WINDOW_SIZE, ts.totalSegmentCount_);
    ts.startCdRomPrefetchAsync(nextPrefetchId);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: promover segmento de um LOD para outro (usa slot já pré-carregado)
// ─────────────────────────────────────────────────────────────────────────────
void upgradeSegmentLod(TrackSystem& ts, SegmentRenderEntry& seg,
                       LodIndex fromLod, LodIndex toLod)
{
    // Liberar slot antigo
    if (seg.lodState.currentFaceSlots[fromLod] != INVALID_SLOT) {
        ts.vramHeap_.freeSlot(seg.lodState.currentFaceSlots[fromLod]);
        seg.lodState.currentFaceSlots[fromLod] = INVALID_SLOT;
    }

    // Ativar slot novo (textura deve estar disponível via prefetch de família)
    assert(seg.lodState.familySlots[toLod].isResident
           && "Textura de upgrade não estava prefetched!");

    seg.lodState.currentLodIndex = toLod;
    activateLodSlot(ts, seg, toLod);
}
```

---

### A.7 Budget de Tempo por Frame (30 FPS = 33,33 ms/frame)

Para garantir 30 FPS estáveis, cada fase do slide deve respeitar os seguintes limites estimados no Sega Saturn (SH2 @ 28,6 MHz):

```
┌───────────────────────────────────────────────────────────────────┐
│                  BUDGET DE FRAME — 33,33 ms                       │
├──────────────────────────────────┬────────────┬───────────────────┤
│ Operação                         │ Custo est. │ % do frame        │
├──────────────────────────────────┼────────────┼───────────────────┤
│ Reciclar segmento traseiro       │   ~0,2 ms  │  0,6%             │
│ Deslizar janela (índice circ.)   │   ~0,0 ms  │  0,0%  (O(1))     │
│ Upgrade LOD ×3 (ativar slots)    │   ~0,5 ms  │  1,5%             │
│ Commit segmento 21 (8×8)         │   ~0,3 ms  │  0,9%             │
│ Kick prefetch CD-ROM (async)     │   ~0,1 ms  │  0,3%             │
│ ─────────────────────────────── │ ────────── │ ───────────────── │
│ Total slide overhead             │   ~1,1 ms  │  3,3%             │
├──────────────────────────────────┼────────────┼───────────────────┤
│ Depth sort (Slave SH2)           │   ~2,0 ms  │  6,0%             │
│ Build VDP1 display list (Slave)  │   ~4,0 ms  │ 12,0%             │
│ VDP1 rasterização (hardware)     │  ~12,0 ms  │ 36,0%             │
│ Game logic, câmera, física       │   ~5,0 ms  │ 15,0%             │
│ Margem de segurança              │   ~9,2 ms  │ 27,7%             │
├──────────────────────────────────┼────────────┼───────────────────┤
│ TOTAL                            │  33,3 ms   │ 100%              │
└──────────────────────────────────┴────────────┴───────────────────┘
```

> **Regra crítica:** As operações de upgrade de LOD (Fase 3) **não devem ler do CD-ROM** neste frame. As texturas dos LODs superiores devem ter sido pré-carregadas pelos estágios `TrackPrefetchStage` e `TrackWorkingSetStage` durante os frames anteriores. O slide apenas **ativa** slots já residentes em VRAM.

---

### A.8 Política de Prefetch: Quando Pré-carregar Cada LOD

Para que o slide seja sempre instantâneo, o prefetch segue esta política:

```
Frame atual → Ação de prefetch
──────────────────────────────────────────────────────────────────
N           → Geometria de SEG(N+20) iniciada no CD (async)
N+3         → Textura 8×8 de SEG(N+20) garantida em VRAM
N+6         → Textura 16×16 de SEG(N+14) garantida em VRAM
              (pois em N+6 o slide colocará seg(N+14) em posição 14)
N+9         → Textura 32×32 de SEG(N+9)  garantida em VRAM
N+12        → Textura 64×64 de SEG(N+4)  garantida em VRAM
```

**Diagrama de prefetch para o segmento S+20:**

```
Frame:      N    N+1  N+2  N+3  N+4  N+5  N+6  N+7  N+8  N+9  N+10 N+11
            │                   │                   │                   │
CD read:    ├──[geo]──►         │                   │                   │
VRAM  8×8:                      ├──[tex 8 load]─►  │                   │
VRAM 16×16:                                         ├──[tex 16 load]──► │
VRAM 32×32:                                                              ├──[tex 32 load]──►
            ↑
          Slide dispara prefetch de SEG(N+20)
```

---

### A.9 Caso Especial: Velocidade Variável do Carro

O modelo acima assume que o carro avança exatamente **1 segmento por frame**. Na prática, a velocidade varia. As regras se adaptam assim:

| Situação | Comportamento |
|----------|---------------|
| **Carro parado ou lento** | Nenhum slide ocorre; janela permanece estática; prefetch continua em segundo plano |
| **Carro avança < 1 seg/frame** | Slide ocorre quando acumulado ≥ 1 segmento; pode não ocorrer todos os frames |
| **Carro avança = 1 seg/frame** | Slide ocorre a cada frame (30 Hz); caso nominal descrito acima |
| **Carro avança > 1 seg/frame** | Múltiplos slides por frame; cada um executa a sequência completa; o budget de tempo deve suportar N× o custo de slide |
| **Carro recua** | Slide reverso: segmento 21 é descartado; segmento 0 (S-1) é carregado; LODs se invertem |

**Proteção contra múltiplos slides por frame:**

```cpp
// Limitar a no máximo MAX_SLIDES_PER_FRAME slides para garantir budget
static constexpr int MAX_SLIDES_PER_FRAME = 2;

int slidesThisFrame = 0;
while (needsSlide() && slidesThisFrame < MAX_SLIDES_PER_FRAME) {
    executeForwardSlide(ts);
    slidesThisFrame++;
}
// Se ainda precisar de mais slides, defer para o próximo frame
// (aceitável: uma leve hitcha a velocidades extremas)
```

---

### A.10 Fluxo Completo em Diagrama de Estado

```
                        ┌─────────────────────────────────────────┐
                        │           ESTADO: Janela Estável         │
                        │  20 segmentos ativos, LODs corretos      │
                        │  Prefetch do próximo em andamento        │
                        └──────────────┬──────────────────────────┘
                                       │
                         Câmera cruza  │  limite de segmento
                                       ▼
                        ┌─────────────────────────────────────────┐
                        │        SLIDE TRIGGER DETECTADO          │
                        │  cameraSegmentId != activeWindowStart_  │
                        └──────────────┬──────────────────────────┘
                                       │
                    ┌──────────────────▼──────────────────────────┐
                    │  FASE 1: Reciclar SEG tail                   │
                    │  • Liberar VRAM (LOD 64×64 do seg removido) │
                    │  • Liberar Cart RAM (geometria)              │
                    │  • Devolver HWR renderer ao pool             │
                    └──────────────────┬──────────────────────────┘
                                       │
                    ┌──────────────────▼──────────────────────────┐
                    │  FASE 2: Deslizar índice circular (O(1))     │
                    │  activeWindowStart_++                        │
                    └──────────────────┬──────────────────────────┘
                                       │
                    ┌──────────────────▼──────────────────────────┐
                    │  FASE 3: Promoção de LOD (3 segmentos)       │
                    │  • pos 4: ativa slot 64×64 (libera 32×32)   │
                    │  • pos 9: ativa slot 32×32 (libera 16×16)   │
                    │  • pos14: ativa slot 16×16 (libera  8× 8)   │
                    └──────────────────┬──────────────────────────┘
                                       │
                    ┌──────────────────▼──────────────────────────┐
                    │  FASE 4: Commit SEG head (pos 20, LOD 8×8)  │
                    │  • Ativar renderer prefetched                │
                    │  • Ativar slot VRAM 8×8                      │
                    └──────────────────┬──────────────────────────┘
                                       │
                    ┌──────────────────▼──────────────────────────┐
                    │  FASE 5: Kick CD-ROM prefetch do SEG+21      │
                    │  • Operação assíncrona, retorna imediato     │
                    └──────────────────┬──────────────────────────┘
                                       │
                                       ▼
                        ┌─────────────────────────────────────────┐
                        │     ESTADO: Janela Estável (nova)        │
                        │  windowStart = S+1                       │
                        │  20 segmentos, LODs mantidos             │
                        └─────────────────────────────────────────┘
```

---

### A.11 Verificação de Consistência Pós-Slide

Para depuração e testes, verificar após cada slide:

```cpp
void assertWindowConsistency(const TrackSystem& ts) {
    const int S = ts.activeWindowStart_;

    // 1. Exatamente WINDOW_SIZE segmentos ativos
    assert(ts.activeRendererCount_ == WINDOW_SIZE);

    // 2. IDs são contíguos e circulares
    for (int i = 0; i < WINDOW_SIZE; i++) {
        int expectedId = wrapSegmentId(S + i, ts.totalSegmentCount_);
        assert(ts.activeWindow_[i].id == expectedId);
    }

    // 3. LODs correspondem exatamente às bandas
    for (int i = 0; i < WINDOW_SIZE; i++) {
        int expectedLod = resolveSegmentLodIndex(i); // rank = i
        assert(ts.activeWindow_[i].lodState.currentLodIndex == expectedLod);
    }

    // 4. Nenhum slot de VRAM duplicado
    std::unordered_set<int> usedSlots;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        int slot = ts.activeWindow_[i].lodState.currentFaceSlots[0];
        assert(usedSlots.find(slot) == usedSlots.end() && "VRAM slot duplicado!");
        usedSlots.insert(slot);
    }
}
```

---

## Apêndice B: Glossário

| Termo | Definição |
|-------|-----------|
| **Segment** | Fatia perpendicular da pista com posição, geometria e dados de textura |
| **Rank** | Índice de distância de um segmento à câmera (0=próximo, N=distante) |
| **LOD** | Level of Detail — resolução de textura baseada em distância |
| **Sliding Window** | Janela de W segmentos que se move junto com a câmera |
| **Prefetch** | Carregamento antecipado do próximo segmento antes de precisar dele |
| **Affine Texturing** | Interpolação UV que ignora z-depth (PS1/Saturn default) |
| **Painter's Algorithm** | Renderização back-to-front que garante profundidade sem Z-buffer |
| **Fill Rate** | Número de pixels que o hardware pode escrever por segundo |
| **VDP1** | Video Display Processor 1 do Saturn — renderiza sprites/quads 2D |
| **T-Junction** | Artefato de subdivisão onde vértices de polígonos não se alinham |
| **Near Clip** | Plano próximo da câmera — polígonos além dele são descartados |
| **BDR/RDR/SDR** | Formatos de segmento compilado deste projeto (Batch/Runtime/Serialized Draw Ready) |

## Apêndice C: Comparativo de Técnicas por Plataforma

| Aspecto | OutRun (1986) | PS1 (1995) | Saturn (1995) | Interlagos Racing |
|---------|---------------|------------|---------------|-------------------|
| **Tipo de primitiva** | Scanline | Triângulo | Quad | Quad (VDP1) |
| **Textura** | Paletas fixas | Affine mapping | Affine mapping | Affine mapping |
| **Z-sort** | Painter's algorithm | Painter's / Z-buffer | Painter's algorithm | Painter's algorithm |
| **LOD** | Nenhum | Subdivisão manual | Famílias de textura | 4 bandas (8/16/32/64px) |
| **Streaming** | Loop contínuo | Toda pista em RAM | Não documentado | Janela deslizante 20 segs |
| **Curvas** | Skewing lateral | Geometria 3D real | Geometria 3D real | Geometria 3D + offset |
| **Hardware Z** | Não | Opcional | Não | Não |

---

*Documento compilado em 2026-04-12. Pesquisa realizada em repositórios públicos, artigos técnicos e análise do código-fonte do projeto Interlagos Racing.*
