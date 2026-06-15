# Bootstrap / CD / Memory Refactor Plan

## Objetivo

Reduzir o acoplamento do bootstrap em `src/main.cxx` extraindo dois pontos transversais:

- acesso simples a assets do CD
- política básica de orçamento de memória

## Problemas atuais

- helpers de CD vivem no `main` e misturam leitura de asset com composição da aplicação
- decisões de `CartRam` / `HighWorkRam` estão espalhadas entre bootstrap e áudio
- o projeto não tem um ponto único para evoluir streaming, fallback de caminho e budget por subsistema

## Refatoração executada nesta etapa

1. Criar `CdAssetSystem`
   - concentra:
     - busca do primeiro caminho existente
     - leitura binária simples de arquivo do CD

2. Criar `MemoryBudgetSystem`
   - concentra:
     - snapshot básico de HWR/LWR/CART
     - consulta de presença de CART
     - espaço livre relevante
     - política de alocação PCM

3. Reencaixar bootstrap
   - `main` deixa de carregar helpers locais de CD
   - `CarAudioSystem` deixa de decidir política de memória sozinho

## Benefício estrutural

- `main` fica mais próximo de composição de subsistemas
- áudio passa a depender de uma política central de memória
- próximos passos ficam destravados para:
  - `CdStreamingScheduler`
  - `MemoryBudgetPolicy` por categoria de asset
  - migração gradual de outros loaders para `CdAssetSystem`

## Próximas etapas recomendadas

1. mover carga de SBA / NYA / JSON de bootstrap para `CdAssetSystem`
2. criar categorias de memória por asset:
   - render track
   - render car
   - audio pcm
   - transient debug
3. introduzir um `SimulationScheduler` com budget Master/Slave por frame
