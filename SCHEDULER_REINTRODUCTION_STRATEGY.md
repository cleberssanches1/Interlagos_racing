# Scheduler Reintroduction Strategy

## Objetivo

Reintroduzir a refatoração do `SimulationScheduler` sem repetir a regressão de boot observada quando helpers foram adicionados diretamente dentro de `src/game_loop_system.hpp`.


## Fato observado

As microintegrações runtime abaixo compilaram e mantiveram o ISO em `4134912` bytes, mas ainda assim causaram `Master SH2 invalid opcode` no emulador:

- helper inline de `MarkCompleted(...)`
- helper inline de `MarkDispatched(...)`
- helper inline de `ClearCompleted()`
- delegação mínima de `FindExistingPath(...)` em `main.cxx`

Conclusão prática:

- não basta preservar o tamanho do ISO;
- pequenas mudanças de layout no caminho crítico continuam sendo perigosas;
- a reintrodução precisa ser ainda mais conservadora.


## Hipótese técnica

O problema mais provável não foi a semântica dos helpers.
O problema mais provável foi a combinação de:

- mudança de layout de código em `src/game_loop_system.hpp`;
- alteração de endereçamento/ordem de emissão no binário;
- sensibilidade do boot/runtime a mudanças pequenas no bloco crítico.


## Regra nova para o scheduler

Para `src/game_loop_system.hpp`, evitar por enquanto:

- adicionar novos métodos em structs locais;
- adicionar wrappers inline persistentes;
- adicionar includes novos;
- deslocar blocos do arquivo mais do que o mínimo.


## Estratégia de reintrodução segura

### Nível 0 — preparação fora do runtime

Permitido:

- contratos passivos;
- assemblers passivos;
- ops externas em headers ainda não integrados;
- documentação operacional.

Proibido:

- uso desses helpers dentro de `src/game_loop_system.hpp` nesta fase.


### Nível 1 — substituição textual 1:1

Quando voltar ao runtime:

- trocar apenas uma sequência repetida por outra sequência textual equivalente;
- não criar método novo;
- não criar helper local;
- não mover blocos.

Exemplo permitido:

- substituir uma sequência repetida por cópia textual alinhada já validada em outro ponto.

Exemplo proibido:

- criar `MarkCompleted(...)` dentro do struct;
- criar façade local;
- consolidar dispatch/completion em chamadas novas.


### Nível 2 — uso de ops externas só em arquivo menos sensível

Se for necessário testar integração real:

- começar por um arquivo menos sensível que `src/game_loop_system.hpp`;
- usar ops externas passivas lá primeiro;
- só depois considerar reaplicar a ideia no scheduler principal.


## Ordem revisada

1. manter o scheduler principal sem novas integrações runtime por enquanto;
2. continuar a refatoração só em contratos/documentação;
3. preparar helpers externos passivos;
4. só retomar runtime quando houver uma microestratégia textual 1:1;
5. priorizar próximos ganhos fora de `src/game_loop_system.hpp`.


## Critério de reentrada

Só voltar a tocar no scheduler runtime se a microetapa:

- não adicionar métodos novos em structs locais;
- não adicionar include novo em `src/game_loop_system.hpp`;
- não aumentar o número de pontos de chamada indireta;
- for revertível por um patch pequeno.
