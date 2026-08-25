# Plano do LOD global de Interlagos (~600 faces)

## Objetivo

Criar uma representação distante do circuito inteiro que possa permanecer residente e ser desenhada atrás da janela de segmentos detalhados. O ativo deve:

- manter a silhueta, largura e elevação reconhecíveis da pista;
- conter somente quads;
- limitar cada vértice a no máximo quatro vizinhos por aresta;
- reutilizar as texturas e as regiões semânticas do material original;
- ficar próximo de 600 faces.

Este trabalho não altera ainda o runtime. A integração depois da janela de LOD atual deve ser feita somente após aprovação visual e medição do custo no Saturn.

## Diagnóstico do arquivo-fonte

Fonte analisada: `INTERLAGOS_mundo_0.obj` (SHA-256 `cac73f6dd5d7f38ec935dc0272b33569f818ef59ee615d0d20877fe2dca3f768`).

| Métrica | Fonte | LOD global gerado |
|---|---:|---:|
| Vértices | 8.265 | 800 |
| Faces | 3.798 | 600 |
| Quads | 3.789 | 600 |
| Triângulos | 9 | 0 |
| Materiais usados | 2.063 | 13 famílias de textura |
| Vértices com mais de 4 ligações | 31 | 0 |
| Maior número de ligações | 5 | 4 |
| Arestas non-manifold | 126 na fonte | 0 no LOD |

O OBJ declara 292 objetos `pista_seg`, mas somente 290 possuem faces. Quatro setores (`103` a `106`) não têm quad com o material principal `asfalto_64`; seus dados são interpolados no LOD distante.

O MTL contém muitas instâncias duplicadas do mesmo material. As faces usam 53 caminhos de textura, embora apenas uma parcela seja necessária para o circuito distante. A maioria dos TGAs é 8×8 ou menor em um dos eixos; há exceções 16×16, 32×32 e 64×64. O gerador resolve automaticamente aliases de alta resolução para um TGA existente de até 8×8 com o mesmo nome quando isso é possível. No ativo final ficaram 13 caminhos; `F06864.TGA` continua 32×32 porque não há variante 8×8 real no diretório atual.

## Por que não usar Decimate

Um decimator comum não atende simultaneamente às restrições deste ativo:

- tende a produzir triângulos;
- não garante valência máxima quatro;
- atravessa fronteiras de material e UV;
- transforma faixas regulares em topologia irregular;
- não resolve as 2.063 instâncias de material duplicadas.

A redução escolhida reconstrói uma malha regular, em vez de colapsar a malha original indiscriminadamente.

## Topologia proposta e implementada

O circuito é reamostrado em 200 anéis. Cada anel possui quatro vértices compartilhados:

`entorno esquerdo — borda esquerda do asfalto — borda direita do asfalto — entorno direito`

Entre dois anéis consecutivos são criados três quads:

1. entorno esquerdo;
2. asfalto;
3. entorno direito.

O circuito é fechado: `200 anéis × 3 quads = 600 quads`.

Os vértices das bordas do asfalto têm exatamente quatro vizinhos no caso máximo: anterior, próximo, borda oposta do asfalto e borda externa. Os vértices externos têm três. Os índices de posição são compartilhados nas costuras, enquanto os índices UV continuam por face, algo permitido pelo OBJ.

## Preservação de forma e materiais

- O `PATH.obj` do mesmo diretório fornece uma linha central limpa com 286 pontos. Ele é alinhado automaticamente ao mundo do OBJ (escala planar 0,1, rotação de eixos e translação detectadas pelo gerador).
- A elevação do PATH é ajustada à pista por regressão contra os quads de asfalto; o erro RMS medido é aproximadamente 2,13 unidades.
- Largura da pista, camber, altura lateral, material e textura vêm das superfícies mais próximas do `INTERLAGOS_mundo_0.obj`.
- A meia largura do asfalto é limitada a 24 unidades para impedir que pit lane ou faces afastadas alarguem a pista distante.
- O entorno imediato é limitado a 24 unidades de cada lado. Isso mantém uma faixa de contexto sem criar sobreposição nas curvas fechadas.
- Cada textura selecionada ocupa a mesma categoria e lado da pista que no modelo-fonte, mas cobre menos faces. A correspondência não é pixel a pixel: com 600 quads não é possível preservar exatamente os 2.063 recortes de material existentes.

O ativo é deliberadamente um LOD do circuito, não um LOD de todos os prédios, arquibancadas, árvores, muros e placas. Esses elementos verticais foram removidos do orçamento de 600 faces.

## Artefatos

- `INTERLAGOS_mundo_LOD600.obj`: malha final.
- `INTERLAGOS_mundo_LOD600.mtl`: 13 materiais consolidados por TGA.
- `INTERLAGOS_mundo_LOD600_preview.png`: conferência superior; preto representa as bordas do asfalto, verde o limite do entorno e cinza a linha central-fonte.
- `INTERLAGOS_mundo_LOD600_report.json`: medidas, hash da fonte e parâmetros de alinhamento.
- `tools/build_interlagos_world_lod.py`: gerador reproduzível.

Comando de reprodução:

```powershell
python tools/build_interlagos_world_lod.py `
  "C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\INTERLAGOS_mundo_0.obj"
```

## Plano de integração futura no jogo

### 1. Aprovação do ativo

- Abrir OBJ/MTL no Blender e conferir o traçado, elevação, orientação UV e transições de material.
- Conferir especialmente S do Senna, Pinheirinho, Bico de Pato, Junção e subida dos boxes.
- Se o entorno estiver largo ou estreito demais, regenerar mudando `--max-shoulder`; não editar a topologia manualmente antes de fechar o orçamento.

### 2. Conversão separada da malha de colisão

- Converter o LOD global para um blob próprio (por exemplo, `TRKWLD.GEO/MAT/SDR` ou `TRKWLD.BIN`).
- Não registrar suas faces no `FaceSurfaceMap`, `MapHeight` ou colisão de muros. Colisão e altura continuam exclusivamente nos segmentos detalhados.
- O formato GEO atual ocuparia aproximadamente 26,4 KiB (`800 × 12 + 600 × 28`, mais cabeçalho). Um SDR completo, incluindo faces, atributos e famílias, fica perto de 35 KiB antes de estruturas do renderer.

### 3. Residência e texturas

- Carregar a geometria global uma vez, preferencialmente fora da janela rotativa de `SegmentRenderEntry`.
- Montar slots para as 13 famílias usadas e validar o custo real das texturas. Todas as aliases compatíveis já apontam para TGAs 8×8; `F06864` precisa de decisão separada se 8×8 for uma exigência absoluta.
- Manter essa geometria fora dos mecanismos de promoção/demissão dos LODs 0/1/2.

### 4. Ordem de desenho

- Desenhar o mundo distante primeiro.
- Desenhar os segmentos detalhados da janela depois, cobrindo a faixa global próxima da câmera.
- Como o Saturn não possui z-buffer convencional, validar a ordenação de polígonos e o risco de costuras/coplanaridade. Se houver vazamento visual, baixar apenas o LOD global alguns centímetros ou gerar uma máscara por trecho; não alterar a malha de colisão.

### 5. Limite “depois de 20 segmentos”

O código-fonte atual não está configurado para 20 segmentos por padrão: `TRACK_LOD0_SEGMENTS=2`, `TRACK_LOD1_SEGMENTS=8` e `TRACK_LOD2_SEGMENTS=6`, totalizando 16. Antes de ligar o LOD global, deve-se definir se o alvo real será 16 ou 20 e ajustar o perfil de build de modo explícito.

Para eliminar overdraw perto da câmera, a etapa seguinte pode associar cada um dos 200 quads de asfalto a um ID de segmento e pular as faces globais cobertas pela janela detalhada. A primeira prova, contudo, deve desenhar o LOD global inteiro antes dos segmentos detalhados, pois é o caminho de integração mais simples e mensurável.

### 6. Gates de desempenho e estabilidade

- Medir comandos VDP1, tempo de preparação no Master SH2, faces enviadas e memória livre.
- Comparar três builds: sem LOD global, 450 faces (150 anéis) e 600 faces (200 anéis).
- Só manter 600 faces sempre desenhadas se não houver regressão de frame pacing. Se houver, conservar o mesmo gerador e usar 150 anéis ou dividir a malha em blocos para culling.
- Executar a validação Saturn estável e confirmar que a ISO permanece com 4.134.912 bytes antes de considerar a integração concluída.

## Critérios de aceite

- 600 faces, todas quads.
- Nenhum vértice com mais de quatro vizinhos.
- Nenhuma aresta non-manifold.
- Nenhuma face invertida.
- Circuito fechado, sem cruzamento das duas bordas do asfalto.
- Materiais do asfalto e entorno coerentes em uma volta completa.
- Sem participação do LOD global em física/colisão.
- Custo de frame aprovado em hardware ou em perfil de emulador representativo.
