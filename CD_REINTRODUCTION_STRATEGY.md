# CdAssetSystem Reintroduction Strategy

## Objetivo

Reintroduzir a consolidação de `CdAssetSystem` sem voltar a desestabilizar `src/main.cxx` e o bootstrap do jogo.


## Fato observado

Uma delegação mínima de `FindExistingPath(...)` em `src/main.cxx` foi suficiente para voltar a provocar falha de boot no emulador, mesmo com:

- build ok;
- ISO em `4134912` bytes;
- mudança funcional praticamente nula.

Conclusão prática:

- `src/main.cxx` é sensível a mudanças pequenas de layout;
- a reintrodução de `CdAssetSystem` deve evitar tocar no bootstrap por enquanto;
- os próximos passos precisam ser externos e passivos.


## Hipótese técnica

Assim como no scheduler, o problema mais provável não foi a semântica do helper.
O problema mais provável foi:

- deslocamento de layout em arquivo crítico de bootstrap;
- alteração de endereçamento/ordem no binário final;
- sensibilidade do caminho de inicialização a mudanças muito pequenas.


## Regra nova para `main.cxx`

Evitar por enquanto:

- includes novos;
- delegações novas para helpers externos;
- wrappers inline substituindo utilitários locais;
- unificações de parsing em runtime.


## Estratégia de reintrodução segura

### Nível 0 — preparação fora do bootstrap

Permitido:

- contratos passivos;
- ops externas passivas;
- documentação operacional;
- normalização de listas de candidatos em arquivos auxiliares não integrados.

Proibido:

- tocar em `src/main.cxx` nesta fase;
- tocar em `src/game_loop_system.hpp` para CD nesta fase.


### Nível 1 — substituição textual 1:1

Quando a reentrada no bootstrap voltar a ser tentada:

- substituir uma duplicação por outra sequência textual equivalente;
- não adicionar include novo;
- não criar chamada indireta nova;
- não mover grupos inteiros de função.


### Nível 2 — unificação por blocos menos sensíveis

Antes de voltar ao bootstrap:

- priorizar unificação em pontos menos sensíveis do domínio de CD;
- manter `main.cxx` como último lugar a receber integração real.


## Ordem revisada de reentrada

1. manter `main.cxx` intacto;
2. preparar ops externas de:
   - path resolution;
   - binary read;
   - anchor parse;
3. preparar equivalência documental entre utilitários locais e `CdAssetSystem`;
4. só então considerar uma microetapa textual `1:1` no bootstrap.


## Critério de reentrada

Só voltar a tocar em `src/main.cxx` se a microetapa:

- não adicionar include novo;
- não criar chamada nova para classe externa;
- não mudar a forma de bootstrap além de uma equivalência textual mínima;
- for revertível com patch pequeno.
