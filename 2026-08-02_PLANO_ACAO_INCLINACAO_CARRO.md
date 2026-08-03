# Plano de ação — inclinação do carro pela altitude das quatro rodas

## Diagnóstico

O fluxo existente já consulta FL/FR/RL/RR, mas uma falha transitória em uma face
reduz o conjunto usado no cálculo de `bodyY` e pode invalidar pitch ou roll no
`CarWheelRig`. Assim, a posição vertical e a atitude visual deixam de representar
o mesmo plano. A zona morta de 0,5 unidade também elimina aclives suaves: sobre a
distância entre eixos aproximada de 1,70, ela exige uma inclinação muito alta
antes de o chassi começar a reagir.

## Modelo adotado

Para o retângulo das rodas, o plano de melhor ajuste de baixo custo é:

```text
centroY = (FL + FR + RL + RR) / 4
pitchTan = (((FL + FR) / 2) - ((RL + RR) / 2)) / wheelbase
rollTan  = (((FR + RR) / 2) - ((FL + RL) / 2)) / track
```

Esse cálculo aceita alturas diferentes nas quatro rodas, custa apenas somas e
divisões fixed-point e mantém altura, pitch e roll derivados do mesmo snapshot.

## Implementação

1. Manter os quatro probes FL/FR/RL/RR já existentes, sem consultas adicionais.
2. Calcular médias de eixo/lado e `bodyY` pelo fluxo fixed-point existente.
3. Usar o hold de pitch/roll que já existe no `CarWheelRig` quando um eixo perde
   contato por um frame, sem acrescentar estado ao stack da física.
4. Reduzir a zona morta de pitch/roll de 0,5 para 0,0625 unidade, preservando o
   filtro e os limites de velocidade angular já existentes.
5. Validar build Saturn, headers passivos/observabilidade e envelope da ISO.

Os experimentos de retry inward e cache sticky em `GroundState` foram removidos
após a regressão de runtime. Em um miss, cada `TryProbeSurfaceY` já executa strict
e soft fallback; o retry podia elevar o pior caso das quatro rodas de 8 para 16
consultas, cada uma capaz de varrer faces de vários segmentos. O cache também
aumentava o objeto `SimpleCarPhysics`, que vive no stack crítico do loop principal.
A versão segura conserva o custo e o tamanho de estado do baseline.

## Critérios de aceite no emulador

- Em aclive e declive contínuos, o chassi converge para a tangente da pista sem
  permanecer horizontal enquanto o Y muda.
- Em inclinação transversal, o lado correspondente acompanha a diferença entre
  rodas esquerdas e direitas.
- Uma costura isolada usa o hold visual já existente em vez de retornar
  instantaneamente à horizontal.
- Não há regressão de boot, áudio, draw da pista ou tamanho da ISO estável.

## Resultado da validação automatizada

- Compilação e link SH2 da imagem Saturn: aprovados.
- Headers passivos do game loop: aprovados.
- Headers de observabilidade: aprovados.
- Testes host de decisão de frame reuse: aprovados.
- Envelope da ISO: pendente; o conteúdo local de `cd/data` gerou 15.636.480
  bytes, enquanto o baseline do validador exige 4.134.912 bytes. A divergência
  ocorre após o link bem-sucedido e precisa ser reconciliada com o conjunto de
  assets local, sem alterar automaticamente o baseline estável.
