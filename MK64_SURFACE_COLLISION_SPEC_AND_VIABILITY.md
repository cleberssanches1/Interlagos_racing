# Especificação Técnica - Lógica de Solo e Colisão (Referência MK64) e Viabilidade no Interlagos_racing

## 1. Objetivo
Definir tecnicamente a lógica de solo/colisão observada no material de referência `mariokart64-master` e avaliar a viabilidade de adoção no projeto `Interlagos_racing` (Sega Saturn), com adaptação para a arquitetura atual (`TrackSystem`, `TrackCollisionQueryFromSystem`, `CarPhysics`).

## 2. Fontes Analisadas
- `C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\mk64levelhacking.txt`
- `C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\surface map fix.txt`
- `C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\Aug 9 2008.txt`
- Relatórios extraídos por script:
  - `mk64_collision_extract.md`
  - `mk64_collision_extract.json`

Observação: o repositório de referência é majoritariamente composto por notas/disassembly, não por código final compilável. Portanto, a especificação abaixo é uma reconstrução técnica com base nesses artefatos.

## 3. Modelo de Colisão/Solo Reconstruído do MK64
### 3.1 Conceito central
- O jogo mantém um **surface map** para colisão, separado da renderização visual.
- Cada entrada do surface map referencia display lists e gera polígonos de colisão com metadados de superfície.

### 3.2 Formato lógico do surface map (nível alto)
- `display_list_offset` (4 bytes)
- `surface_type` (1 byte)
- `display_list_index` (1 byte)
- `flags` (2 bytes)
- Terminador: entrada com `display_list_offset == 0`.

### 3.3 Geração da malha de colisão
- O parser percorre comandos de display list e monta polígonos de colisão.
- Comandos citados na referência:
  - `0x04` Load Vertex
  - `0x06` Call Display List (recursivo)
  - `0xB1` Draw 2 Triangles
  - `0xB5` Draw Line
  - `0xB8` End Display List
  - `0xBF` Draw Triangle
- Há emulação de cache de vértices durante o parse para resolver índices de triângulos.

### 3.4 Metadados por superfície
- `surface_type` controla comportamento físico/gameplay (asfalto, off-road, wall, boost, out-of-bounds etc).
- `flags` incluem semânticas de colisão, com destaque na documentação para bit de tangibilidade do verso (backface).
- Entradas em RAM (44 bytes por entrada, segundo as notas) incluem campos extras como fator relacionado a gravidade.

### 3.5 Consulta em runtime (inferida)
- O sistema parece evitar testar tudo contra tudo, usando contexto de região/índice para reduzir busca.
- O tipo de superfície é usado como chave de comportamento (som, grip, boost, Lakitu/out-of-bounds, etc).

## 4. Estado Atual do Interlagos_racing (base de comparação)
### 4.1 Consulta de solo
- `TrackSystem::FindSurfaceYByFamilySet(...)` em `src/track_system.cxx:17163`.
- `TrackSystem::FindSurfaceYBySurfaceTypeSet(...)` em `src/track_system.cxx:17120`.
- Query atual usa varredura por faces elegíveis por família, teste no XZ e solução de Y por plano.

### 4.2 Consulta de segmento/base de região
- `TrackSystem::FindNearestSegment(...)` em `src/track_system.cxx:17021`.
- `TrackCollisionQueryFromSystem` mantém `lastSegmentId_` e usa busca progressiva.

### 4.3 Colisão lateral
- `TrackSystem::FindPlanarWallPush(...)` em `src/track_system.cxx:17567`.
- `car_ground_follower.hpp` consome `ResolvePlanarWallPush(...)` quando habilitado.

### 4.4 Uso na física do carro
- `car_ground_follower.hpp` usa:
  - `SampleSurfaceYBySurfaceTypeSetStrict(...)`
  - `SampleSurfaceYBySurfaceTypeSet(...)`
  - cooldown de probe e cache de alvo Y
- `car_physics_shared.hpp` define `kEnableGroundSurfaceScan` e tipos dirigíveis.

## 5. Gap Técnico (MK64 vs Interlagos_racing)
1. MK64 usa surface map explicitamente separado da malha visual; hoje o projeto consulta geometria do renderer por face/família.
2. MK64 carrega flags semânticas de colisão por polígono; hoje há tipagem de superfície, mas sem conjunto equivalente completo de flags por face.
3. MK64 sugere seleção regional forte por mapa/índice; hoje ainda existe custo relevante de query por frame em varredura local.
4. MK64 descreve parâmetros por superfície (incluindo campo ligado à gravidade); hoje há grip e regras, mas sem tabela completa de parâmetros por tipo/flag.

## 6. Viabilidade de Uso no Sistema Atual
## 6.1 Parecer
**Viabilidade: Alta (com adaptação), sem copiar implementação literal.**

### 6.2 O que é viável reaproveitar conceitualmente
1. Separar definitivamente **malha de colisão** da **malha visual**.
2. Manter metadado por face de colisão: `surface_type`, `flags`, `material_behavior`.
3. Aplicar regra de face one-sided/two-sided via flag de backface tangível.
4. Parametrizar física por tipo de solo: grip, drag extra, limitação de velocidade, fator de gravidade.
5. Manter busca regional por segmento-semente + vizinhos imediatos para reduzir custo por query.

### 6.3 O que não é viável/necessário portar
1. Emular parser de display list N64 (`0xB1`, `0xBF`, etc) em runtime Saturn.
2. Replicar layout RAM/endereços da referência.
3. Introduzir recursão de display list na execução do jogo; isso deve ficar no pipeline de build offline.

## 7. Arquitetura Alvo Recomendada
1. Build offline gera `CollisionSurfaceMap` por segmento com triângulos simplificados de colisão.
2. Cada triângulo carrega:
   - `surfaceType` (u8)
   - `flags` (u16)
   - `gravityScale` (fix16, opcional)
   - `gripScale` (fix16, opcional)
   - `segmentId`
3. Runtime mantém índice local por janela ativa de segmentos.
4. Query de solo segue esta ordem:
   - seed segment
   - vizinhos de seed
   - fallback na janela ativa
5. Query retorna `y`, `normal`, `surfaceType`, `flags`, `segmentId`.
6. Física aplica comportamento por `surfaceType/flags` sem nova varredura redundante.

## 8. Impacto Esperado
1. Redução do custo de `Q` (surface query) por frame, hoje visível em telemetria (`Q:6/281/0` no seu log de exemplo).
2. Melhora de estabilidade do frame time em pistas com mais triângulos visuais.
3. Controle mais determinístico de comportamento do carro por tipo de solo.
4. Menor acoplamento entre render e colisão, facilitando otimização futura de VDP1 sem quebrar física.

## 9. Riscos
1. Aumentar memória se a malha de colisão for duplicada sem simplificação.
2. Inconsistência visual vs colisão se export/build não alinhar coordenadas.
3. Regressões de dirigibilidade se calibração de `surfaceType` não for feita por pista.

## 10. Plano de adoção incremental
1. Definir formato `CollisionSurfaceMap` e export no build (`tools/build_all_nya_geo_mat.ps1`) com tipo e flags por face.
2. Implementar loader leve no `TrackSystem` para esse mapa por segmento.
3. Adicionar nova API de query estruturada (`SampleSurfaceEx`) sem remover as atuais.
4. Migrar `GroundFollower` para usar `SampleSurfaceEx` e eliminar dupla consulta redundante.
5. Ativar regra de backface tangível por flag.
6. Validar com telemetria (`Q` ticks/query, FPS, estabilidade lateral, aderência por tipo de solo).

## 11. Conclusão
A lógica de referência do MK64 é tecnicamente compatível em termos de conceito com o seu projeto e deve trazer ganho prático se aplicada como **surface map de colisão offline + query regional + física por tipo de superfície**. O caminho recomendado é adaptação arquitetural progressiva, não port direto do disassembly.
