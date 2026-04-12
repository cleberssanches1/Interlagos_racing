# TURBOQUANT SATURN COMPRESSION PLAN

## 1. Contexto

Objetivo: reduzir uso de memoria e custo de streaming no jogo de Sega Saturn com compressao **offline** e descompressao **runtime**, mantendo estabilidade da janela de 20 segmentos e meta de 30 FPS.

Observacao: o termo citado como "TurboQuantum" na pratica corresponde ao trabalho **TurboQuant** do Google Research.

## 2. O que e o TurboQuant (resumo tecnico)

TurboQuant e uma familia de algoritmos para quantizacao vetorial online (data-oblivious), com foco em:

- baixa distorcao para MSE e inner product;
- compressao extrema de KV cache em LLMs;
- baixo overhead de runtime em aceleradores modernos.

Pontos tecnicos principais:

- random rotation/preconditioning para facilitar quantizacao escalar por coordenada;
- etapa residual com **QJL 1-bit** para reduzir vies em estimativa de inner product;
- uso de ideias do **PolarQuant** para reduzir overhead de normalizacao e parametros por bloco.

## 3. Limite de aplicacao direta no Sega Saturn

TurboQuant foi projetado para vetores de inferencia de IA (KV cache / vector search), nao para pipeline de texturas/segmentos de VDP1/VDP2.

Portanto, no Saturn a estrategia correta e:

- **nao portar TurboQuant 1:1**;
- usar uma abordagem **inspirada**: quantizacao/compactacao offline forte e descompressao runtime barata, deterministica e com memoria fixa.

## 4. Diretriz de arquitetura para o projeto

### 4.1 Regra de memoria fixa

A memoria de pista deve ser tratada como **orcamento fixo** para 20 segmentos (pior caso), sem crescimento cumulativo por volta.

Janela alvo:

- 4 segmentos em 64x64
- 5 segmentos em 32x32
- 5 segmentos em 16x16
- 6 segmentos em 8x8

Transicao por avancar 1 segmento:

- remove 1 segmento da frente (recycle imediato);
- promove os limites de LOD (5->64, 10->32, 15->16);
- entra 1 novo segmento no fim em 8x8.

### 4.2 Contrato de pools (sem malloc dinamico no hot path)

- `HWR` (alta work ram): estruturas persistentes de janela ativa, tabelas de indices, metadados de slots.
- `LWR` (baixa work ram): staging ring para blobs comprimidos e saida de descompressao temporaria.
- Pools fixos por frame:
  - `SegmentSlotPool[20]` (estado READY/FILLING/FREE)
  - `DecodeScratchPool[k]` (duplo/triplo buffer)
  - `TextureUploadQueue` com teto por frame.

Sem expansao de pool em runtime. Quando lotado: fallback de LOD e/ou adiamento controlado.

## 5. Estrategia de compressao (offline) para Saturn

## 5.1 Camada A: Texturas (prioridade maxima)

- converter para formato Saturn nativo (preferencia 4bpp/8bpp indexado);
- paletas por familia de segmento quando possivel;
- opcional: compressao leve por bloco (RLE/LZSS simples) somente se custo de decode couber no budget.

Resultado esperado:

- grande reducao de memoria/IO sem matematica pesada no runtime.

## 5.2 Camada B: Segment payload (RDR/pack)

- comprimir entradas de segmento no pack runtime (`TRKRDR.BIN` ou equivalente);
- incluir no header: `codec`, `compressedSize`, `rawSize`, `crc`, `version`;
- descompressao somente no prefetch (nunca no commit critico de slide).

## 5.3 Camada C: Geometria (fase opcional)

- delta coding + quantizacao fixa de vertices/normais/UV (bit budget por atributo);
- codebook simples por classe de ativo (nao por frame);
- decode inteiro (fixed-point), sem dependencia de FPU.

Observacao: esta camada e onde a inspiracao de quantizacao vetorial (estilo TurboQuant) pode ser adaptada de forma pratica ao Saturn.

## 6. Orquestracao SH2 (master/slave)

Principio: slave faz trabalho CPU-only; master mantem operacoes sensiveis de render e commit.

Master SH2:

- controle de frame e sincronizacao;
- manutencao da janela ativa e commit final de slide;
- uploads para VDP1/VDP2, draw submit e recycle final.

Slave SH2:

- descompressao de blobs de segmentos em prefetch;
- parse para formato intermediario pronto para commit;
- ordenacao/planejamento que nao toca VDP.

Deadline:

- job do slave que passar do budget do frame vira "late" e entra fallback (sem travar master).

## 7. Pipeline runtime proposto (deterministico)

1. Frame N: master coleta resultados prontos do slave.
2. Frame N: master aplica no maximo `X` promocoes/commits (budget fixo).
3. Frame N: master enfileira jobs de N+1/N+2 para slave.
4. Frame N: slave descomprime para scratch e publica slot `READY` com generation id.
5. End frame: recycle de slots removidos e zeragem de ownership.

Regra obrigatoria:

- cada segmento removido da janela deve liberar/reutilizar 100% dos buffers associados no mesmo ciclo de manutencao.

## 8. Telemetria obrigatoria

Minimo para validacao continua:

- `hwr_free_min`, `lwr_free_min`, watermark por frame;
- `decomp_jobs_enqueued/completed/late/timeout`;
- `segment_recycle_ok`, `segment_recycle_fail`;
- `bytes_compressed_in`, `bytes_raw_out`, ratio medio;
- ticks SH2 por estagio (master/slave);
- `frame_slack_us` antes de sincronizacao;
- `fallback_lod_count`.

Sem essa telemetria nao existe validacao de vazamento.

## 9. Plano de acao executavel

### Fase 0 - Baseline congelado

Entrega:

- snapshot do comportamento atual (memoria, ticks, stalls) em corrida longa.

Criterio:

- 3 corridas com variacao <= 5% nos indicadores base.

### Fase 1 - Formato de asset comprimido v1

Entrega:

- especificacao de container (header/chunks/crc/version/codec);
- leitor com validacao de integridade.

Criterio:

- parser aprova 100% dos assets de teste; erro gera fallback limpo.

### Fase 2 - Compressor offline no build

Entrega:

- ferramenta no pipeline (`tools/`) para gerar packs comprimidos + manifesto;
- opcao por tipo de codec por ativo.

Criterio:

- reducao >= 30% no total de bytes de pista sem regressao funcional.

### Fase 3 - Descompressor runtime com pools fixos

Entrega:

- descompressao integrada ao prefetch;
- nenhum `malloc` no hot path;
- recycle comprovado por contadores.

Criterio:

- sem crescimento sustentado de HWR/LWR apos multiplas voltas.

### Fase 4 - Delegacao SH2 (segura)

Entrega:

- slave executa decode/parse CPU-only;
- master apenas commit/upload/draw.

Criterio:

- timeout/late dentro de limite; sem flicker/colapso de pista.

### Fase 5 - Soak + hardening

Entrega:

- testes longos (>= 30 min), injecao de erro de CRC, rollback automatico por flag.

Criterio:

- 0 crash por esgotamento de memoria no soak alvo;
- fallback funcional em 100% dos casos de erro injetado.

## 10. Matriz de viabilidade

1. Textura indexada + paleta (offline)
- Viabilidade: alta
- Ganho esperado: alto
- Risco: baixo

2. Compressao por segmento no pack runtime
- Viabilidade: alta
- Ganho esperado: medio-alto
- Risco: medio (se decode entrar no caminho critico)

3. Quantizacao vetorial de geometria (estilo TurboQuant-inspirado)
- Viabilidade: media
- Ganho esperado: medio
- Risco: medio-alto (complexidade de tooling/validacao)

## 11. Regras de Go/No-Go

Go somente se todas forem verdadeiras:

- memoria de pista estabilizada (sem drift por volta);
- 20 segmentos ativos com regra de LOD mantida;
- sem regressao severa de frame time para meta de 30 FPS;
- sem corrupcao visual persistente na troca de segmentos.

No-Go se ocorrer:

- crescimento monotonic de HWR/LWR por mais de N janelas;
- decode bloqueando commit de slide;
- timeout recorrente da slave sem fallback limpo.

## 12. Referencias primarias (pesquisa)

- Google Research Blog (24 Mar 2026):
  - https://research.google/blog/turboquant-redefining-ai-efficiency-with-extreme-compression/
- TurboQuant paper:
  - https://arxiv.org/abs/2504.19874
  - OpenReview (ICLR 2026): https://openreview.net/forum?id=tO3ASKZlok
- QJL paper:
  - https://arxiv.org/abs/2406.03482
- PolarQuant paper:
  - https://arxiv.org/abs/2502.02617

## 13. Proximo passo recomendado

Implementar primeiro as fases 1 e 2 (formato + compressor offline) com feature flag, mantendo fallback para o caminho atual. Em seguida ativar fase 3 com pools fixos e telemetria completa antes de mover mais carga para a slave.
