# Plano de ação — gravidade e contato de quatro cantos no Saturn

> **Status: rollback aplicado.** A integração runtime fez com que pista e
> background deixassem de ser renderizados. O solver e seus testes permanecem
> passivos, mas nenhum header do jogo os inclui e nenhum estado adicional é
> alocado no runtime atual.

## Problema

O seguidor anterior calculava um `surfaceYTarget` médio e deslocava o chassi até
ele. Uma troca de face alterava esse alvo em bloco, reproduzindo as junções da
malha como degraus. Também não existia estado físico independente para FL, FR,
RL e RR: `verticalVelocity` era zerada assim que havia suporte.

## Arquitetura escolhida

1. Manter quatro caches compactos de altura, segmento, idade e validade.
2. Atualizar somente uma diagonal por frame:
   - fase 0: FL + RR;
   - fase 1: FR + RL.
3. Usar consultas strict-only; um miss conserva o cache por até três frames.
4. Representar atitude sem trigonometria:
   - `pitchDelta = frontY - rearY`;
   - `rollDelta = rightY - leftY`.
5. Integrar gravidade em Y e resolver somente penetrações dos cantos.
6. Transformar diferenças de reação em velocidade/correção de pitch e roll.
7. Alimentar o `CarWheelRig` com o plano físico já resolvido.

## Orçamento Saturn

- Consultas de altura: duas por frame, igual ao perfil anterior de dois eixos.
- Consulta central/segmento: preservada para não alterar o contrato topológico.
- Sem `float`, `sqrt`, matriz, alocação dinâmica ou busca extra para telemetria.
- Estado persistente adicional estimado: aproximadamente 60 bytes por carro.
- Trabalho do solver: quatro testes de canto, somas, shifts e clamps inteiros.

## Etapas e status

- [x] Helper fixed-point passivo e reversível.
- [x] Testes host: bootstrap, plano, canto isolado e queda livre.
- [x] Cache de quatro cantos com diagonais alternadas — protótipo revertido.
- [x] Gravidade e restrições locais de contato — protótipo revertido.
- [x] Pitch/roll derivados das reações dos cantos — protótipo revertido.
- [x] Integração com o wheel rig — protótipo revertido.
- [x] Build/link SH2 do rollback e geração da ISO.
- [ ] Teste visual no Yabause nos segmentos 22–42.

## Validação automatizada

- Compilação e link SH2: aprovados.
- Cabeçalhos passivos e de observabilidade: aprovados.
- Testes de reutilização de frame: aprovados.
- Testes do solver: 6 cenários aprovados, incluindo queda/elevação abrupta sem
  snap do chassi.
- A tentativa ativa gerou ELF de 1.458.632 bytes e ISO de 16.027.648 bytes; no
  emulador, apenas carro e estatísticas foram renderizados.
- O rollback recuperou o ELF de 1.454.935 bytes e a ISO de 16.023.552 bytes do
  último pacote funcional.
- `car_system.o`: 24.800 bytes.
- O verificador histórico ainda espera 4.134.912 bytes
  apesar do conjunto atual de assets já exceder esse baseline.

## Rollback

O include, os campos em `GroundState`, as diagonais alternadas e a chamada do
solver foram removidos integralmente. Somente o helper passivo e seus testes
continuam no repositório. `PHYS_WHEEL_STRICT_SURFACE` permanece independente.

## Critérios de aceite no emulador

1. Pista e background continuam renderizados desde a largada.
2. O chassi não muda de Y diretamente na troca de segmento.
3. Rodas podem entrar em contato em frames diferentes.
4. O carro converge para o plano da pista sem atravessar a face.
5. Sem oscilações persistentes em piso plano.
6. Sem aumento do número médio de consultas de altura.
