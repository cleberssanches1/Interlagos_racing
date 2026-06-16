# CarRenderSystem Reintroduction Strategy

## Objetivo

Reintroduzir a extração de `CarRenderSystem` sem voltar a desestabilizar `src/game_loop_system.hpp`, `src/car_system.cxx` e o caminho visual do carro.


## Contexto

O render do carro já está recortado passivamente, mas a integração real ainda é arriscada porque envolve:

- lógica visual no `GameLoopSystem`;
- sincronização visual dentro de `CarSystem`;
- submissão final no Master SH2;
- dependência de layout sensível em arquivos críticos.


## Regra nova para o render do carro

Evitar por enquanto:

- includes novos em `src/game_loop_system.hpp`;
- includes novos em `src/car_system.cxx`;
- mover `RenderPipeline`;
- mover chamadas de shadow draw;
- introduzir packets reais no runtime crítico.


## Hipótese técnica

Mesmo mudanças pequenas no caminho visual do carro podem:

- alterar ordem de código em arquivos críticos;
- deslocar dados de render/sync;
- produzir regressão visual ou falha de boot, mesmo com ISO estável.


## Estratégia de reintrodução segura

### Nível 0 — preparação fora do runtime

Permitido:

- contratos passivos;
- ops externas passivas;
- documentação de equivalência;
- composição passiva de packet/shadow/submit fora do runtime.

Proibido:

- tocar em `src/game_loop_system.hpp`;
- tocar em `src/car_system.cxx`;
- tocar em `RenderPipeline` real.


### Nível 1 — packet em modo espelho

Quando a reentrada real voltar a ser tentada:

- montar packet visual apenas como espelho;
- não consumir o packet no fluxo real;
- manter `SubmitRender` e shadow draw como estão.


### Nível 2 — substituição textual mínima

Se o modo espelho estiver estável:

- substituir primeiro uma etapa local de preparação visual por sequência textual equivalente;
- não adicionar chamada indireta nova em arquivo crítico;
- não centralizar submit ainda.


## Ordem revisada de reentrada

1. manter `GameLoopSystem` intacto;
2. manter `CarSystem` intacto;
3. preparar ops externas de:
   - state assembly;
   - shadow assembly;
   - submit packet;
   - telemetry;
4. só depois considerar espelhamento local;
5. por último, considerar substituição textual mínima.


## Critério de reentrada

Só voltar a tocar no runtime do carro se a microetapa:

- não adicionar include novo em arquivo crítico;
- não mover `RenderPipeline`;
- não alterar a submissão real;
- for revertível com patch pequeno.
