# MemoryBudgetSystem Reintroduction Strategy

## Objetivo

Reintroduzir a consolidação de `MemoryBudgetSystem` sem voltar a desestabilizar `src/game_loop_system.hpp` e `src/track_system.cxx`.


## Contexto

O domínio de memória já está bem mapeado, mas ainda está espalhado entre:

- `src/memory_budget_system.hpp`
- `src/game_loop_system.hpp`
- `src/track_system.cxx`

Os dois últimos são sensíveis a mudanças pequenas de layout e comportamento.


## Regra nova para memória

Evitar por enquanto:

- inserir includes novos em `src/game_loop_system.hpp`;
- inserir includes novos em `src/track_system.cxx`;
- trocar chamadas locais por helpers externos nesses arquivos;
- centralizar `pressure/trim/floor logic` diretamente no runtime crítico.


## Hipótese técnica

Mesmo mudanças pequenas de layout nos arquivos críticos podem:

- alterar endereçamento;
- deslocar blocos sensíveis do binário;
- mudar comportamento de boot/runtime sem alterar o tamanho final do ISO.

Portanto, a reintrodução de memória precisa seguir o mesmo padrão conservador usado para scheduler e CD.


## Estratégia de reintrodução segura

### Nível 0 — preparação fora do runtime

Permitido:

- contratos passivos;
- ops externas passivas;
- documentação de equivalência;
- classificação de pressão fora do caminho crítico.

Proibido:

- integrar helpers novos em `src/game_loop_system.hpp`;
- integrar helpers novos em `src/track_system.cxx`.


### Nível 1 — equivalência em modo espelho

Quando voltar ao runtime:

- usar snapshots/policy externos apenas para comparação;
- manter a decisão concreta no local original;
- não mover `RunWorkRamMaintenance`;
- não mover overlays/logs.


### Nível 2 — substituição textual 1:1

Se a equivalência se mostrar estável:

- trocar primeiro uma leitura repetida de report por sequência textual equivalente;
- não criar wrappers novos;
- não introduzir chamadas indiretas novas em arquivos críticos.


## Ordem revisada de reentrada

1. manter `src/game_loop_system.hpp` intacto para memória;
2. manter `src/track_system.cxx` intacto para memória;
3. preparar ops externas de:
   - snapshot consolidado;
   - classificação de pressão;
   - policy PCM;
   - telemetria consolidada;
4. só depois considerar integração em modo espelho;
5. por último, considerar substituição textual mínima.


## Critério de reentrada

Só voltar a tocar em runtime crítico se a microetapa:

- não adicionar include novo;
- não mover blocos grandes;
- não alterar a lógica concreta de trim/slide;
- for revertível com patch pequeno.
