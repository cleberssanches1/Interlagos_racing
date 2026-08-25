# Plano executado — Interlagos 290 segmentos / texturas 128×128

## Objetivo

Produzir uma variante de `INTERLAGOS_mundo_1.obj` que mantenha o orçamento e a divisão espacial do circuito original, mas elimine triângulos, limite cada vértice geométrico a quatro ligações e use texturas 128×128 com uma leitura visual mais próxima do Interlagos atual.

Restrições atendidas:

- exatamente 290 objetos/segmentos com faces;
- exatamente 4.803 faces, abaixo do limite de 5.000;
- exatamente 10.636 vértices geométricos (`v`), como no original;
- 100% das faces em quads;
- nenhuma face triangular;
- grau máximo de quatro arestas por vértice;
- mesma quantidade de faces por segmento;
- mesma sequência de materiais por face e segmento;
- todas as texturas referenciadas pelo MTL em 128×128.

## 1. Auditoria do original

O arquivo de origem possuía:

| Métrica | Original |
|---|---:|
| Segmentos | 290 |
| Faces | 4.803 |
| Quads | 4.792 |
| Triângulos | 11 |
| Vértices geométricos | 10.636 |
| Coordenadas UV | 17.410 |
| Maior número de ligações | 6 |
| Vértices com mais de 4 ligações | 56 |

Portanto, o arquivo exportado ainda não respeitava integralmente as regras topológicas solicitadas.

## 2. Correção topológica sem aumentar o orçamento

1. Os 11 triângulos foram transformados em quads dividindo sua maior aresta.
2. Para isso foram reaproveitados índices geométricos redundantes ou sem uso já existentes no OBJ; não foi criado nenhum novo `v`.
3. Foram adicionadas 11 coordenadas UV intermediárias, uma por reparo, para não deformar o posicionamento das texturas. Assim, `vt` passou de 17.410 para 17.421, sem alterar a contagem de vértices geométricos.
4. Vértices com cinco ou seis vizinhos foram separados em índices coincidentes. A posição visual não muda, mas nenhuma instância resultante excede quatro ligações.
5. Faces, materiais e objetos permaneceram na mesma ordem lógica do original.

## 3. Direção visual

A linguagem visual foi orientada pelo Interlagos contemporâneo: asfalto muito escuro e fino, marcas discretas de borracha, áreas pintadas de verde e zebras características em verde, amarelo e branco. Como referência factual foi usada a publicação oficial do GP São Paulo sobre o recapeamento e a pintura do circuito:

https://f1saopaulo.com.br/noticias/a-formula-1-sobre-o-asfalto-novo-de-interlagos/

Quatro materiais-base foram gerados especificamente para o circuito:

- asfalto recapeado escuro, com agregado fino e borracha discreta;
- grama curta e úmida de clima paulistano;
- zebra de concreto verde/amarela/branca, com desgaste e marcas de pneu;
- área de escape em asfalto verde antiderrapante.

Esses materiais foram convertidos em tiles contínuos e deram origem a 13 variações usadas pelo MTL. As outras 48 imagens — placas, publicidade, prédios, árvores, cercas e elementos específicos — foram ampliadas de modo controlado, mantendo seu conteúdo e área de UV. Essa decisão evita que geração por IA altere logotipos, números e identidade visual.

## 4. Resultado validado

| Métrica | Resultado |
|---|---:|
| Segmentos | 290 |
| Faces | 4.803 |
| Quads | 4.803 |
| Triângulos | 0 |
| Vértices geométricos | 10.636 |
| Coordenadas UV | 17.421 |
| Maior número de ligações | 4 |
| Vértices com mais de 4 ligações | 0 |
| Menor área de quad | 0,0991687 |
| Texturas referenciadas | 61 |
| Texturas fora de 128×128 | 0 |

Também foi validado que nenhuma face repete um índice geométrico e que a contagem de faces de cada um dos 290 segmentos é idêntica à fonte.

## 5. Integração futura no Sega Saturn

O OBJ/MTL foi preparado como ativo mestre; ele ainda não foi conectado ao executável do jogo. Texturas 128×128 custam quatro vezes a área de uma textura 64×64. Se todas as 61 fossem residentes ao mesmo tempo, o custo bruto seria aproximadamente 488 KiB em 4 bpp ou 976 KiB em 8 bpp, antes de CLUTs e outros recursos. Isso não é apropriado para manter tudo simultaneamente na VDP1 VRAM.

Na integração, a abordagem recomendada é:

1. manter a divisão em 290 segmentos;
2. carregar somente bancos de textura exigidos pelos segmentos visíveis/próximos;
3. agrupar materiais repetidos de pista, grama, zebra e área de escape em um banco comum;
4. manter publicidade, prédios e vegetação em bancos locais;
5. validar cada alteração com o build estável do projeto e teste no emulador/hardware.

## 6. Reprodutibilidade

O processo pode ser repetido com `tools/build_interlagos_128_track.py`. O relatório JSON gerado contém hashes da origem e da saída, reparos aplicados, métricas completas e o mapeamento de todas as texturas.
