# Plano de implementacao: Auto Lap dirigido pela fisica

## Estado e objetivo

O menu oferece `AUTO LAP`. Com `AUTO_LAP_PHYSICS_DRIVER=1` (padrao), ele usa
`PATH.NYA` como guia de comando do carro pela fisica normal. O prototipo
anterior de pose direta permanece selecionavel com a flag em `0` para A/B.
`TREINO LIVRE` continua manual; `TIME ATTACK` conserva suas regras. Este e
um primeiro controlador experimental: build e testes host passaram, mas a
volta completa e a qualidade da conducao ainda exigem teste no emulador.
Na largada, o Auto Lap fisico alinha uma unica vez o carro ao ponto do guia
mais proximo, antes do primeiro quadro de fisica, usando a TCOL para Y. Isso
nao teletransporta o carro durante a volta nem afeta os outros modos.

Objetivo final: completar voltas por comandos de aceleracao, freio e direcao
passados a mesma fisica do jogador, sem teletransporte por waypoint. TCOL/FSMAP
continuam sendo a autoridade para solo, tipo de piso e paredes; a rota define
ordem de progresso e alvo de direcao, nao substitui colisao.

## Etapa 1 - Contrato e dados de rota

1. Reutilizar `PATH.NYA` e sua normalizacao de sentido. Nao fazer uma busca
   global na malha TCOL a cada quadro.
2. Ao carregar a pista, validar rota fechada, continuidade, sentido, mapeamento
   para segmentos e pontos sobre solo dirigivel. Registrar falha de guia sem
   trocar silenciosamente para um caminho de qualidade desconhecida.
3. Gerar uma tabela compacta por trecho: proximo ponto, tangente, curvatura,
   velocidade-alvo e identificador de segmento. Calcular esses valores uma vez,
   em vez de alocar ou percorrer o circuito inteiro durante o frame.
4. Usar o tipo de piso do contato ja obtido pela fisica: asfalto permite a meta
   normal; grama/area de escape reduzem a meta e ativam recuperacao conservadora.
   TDIR.BIN pode ajudar a conferir sentido, mas nao fornece sozinho uma linha
   de corrida nem limites laterais.

## Etapa 2 - Controlador deterministico e isolado

1. Criar um helper puro que receba pose, velocidade, contato de solo e uma
   janela curta da rota; ele devolve `GameplayInputSnapshot` ou um pacote de
   comandos equivalente. Sem CD, render, SGL, alocacao dinamica ou escrita na
   malha compartilhada durante `Step`.
2. Direcao: ponto-alvo adiante com distancia dependente da velocidade; erro
   angular com zona neutra, histerese e limite de mudanca por quadro.
3. Velocidade: antecipar curvas com curvatura e distancia de frenagem. Acelerar
   abaixo da meta e frear acima dela; evitar acionar freio ate iniciar re no
   fim de uma parada. Reusar a troca de marchas existente.
4. Recuperacao: se perder o contato `inside` ou a rota local, reduzir velocidade
   e tentar retomar o proximo ponto valido dentro de uma janela limitada.
   Proibir salto de posicao e marcar falha persistente para diagnostico.
5. Testar em host: reta, curva fechada, chicane, volta da rota, aderencia baixa,
   parede, parada e recuperacao; validar tambem a ausencia de oscilacao entre
   acelerar/frear e esquerda/direita.

## Etapa 3 - Integracao no jogo

1. Preservar o Auto Lap atual como modo de diagnostico por flag. Se a conducao
   fisica falhar na pista, usar a flag para comparar sem alterar o pipeline.
2. Antes de `BuildGameplayFrameState`, escolher entrada humana ou comando da IA.
   Enviar ambos pelo mesmo `CarSystem::ApplyGameplayInput`, `GameplayTick` e
   `CarPhysics::Step`; nao gravar `carWorldPosition` ou yaw pela rota durante a
   conducao pela fisica.
3. Deixar colisao TCOL, aderencia por piso, motor, RPM, marchas, audio e camera
   no caminho ja usado pelo jogador. A contagem de voltas deve aceitar apenas
   cruzamentos reais da linha de largada, no sentido correto.
4. Expor telemetria pequena e opcional: indice da rota, erro lateral/angular,
   velocidade atual/alvo, piso, comando da IA, estado de recuperacao e custo.

## Etapa 4 - Master/Slave SH-2 e desempenho

Primeiro executar o helper de controle no Master, antes de preparar o pacote
de simulacao. A fisica ja pode rodar assincronamente no Slave; nao criar uma
segunda tarefa Slave concorrente. Se a medicao mostrar custo relevante do
controle, incorporá-lo ao **mesmo** job de simulacao com entrada/saida de tamanho
fixo e rota imutavel. Manter render, audio e sequenciamento final no Master.
Nao consultar CD nem gerar pontos da rota no Slave durante o frame.

Medir FPS, tempo Master/Slave, atraso da pose, Low/High Work RAM, tamanho da
ISO e falhas de contato antes e depois. O historico do projeto ja mostrou
limite apertado de tamanho binario para integracoes amplas de Auto Lap; preferir
uma mudanca pequena por etapa e conferir o mapa/binario compilado.

## Criterios de aceite

- O menu entra em `AUTO LAP` sem alterar a selecao de pista/carro e sem ativar
  Auto Lap por padrao no Treino Livre.
- Na fase final, o carro completa voltas sem escrita direta da pose pela rota,
  respeita paredes e piso, freia antes de curvas e nao fica alternando comandos.
- Desligar a IA devolve o controle ao jogador sem salto de posicao ou camera.
- Nao ha alocacoes por quadro, scans globais de TCOL, novo job Slave ou perda
  de estabilidade da esteira. Confirmar em emulador e, depois, no Saturn.

## Validacao por entrega

Menu: teste de catalogo, build SH-2 e boot/selecao no emulador. Controlador:
testes puros e medicao do tempo por quadro. Integracao de fisica: testes de
contato/colisao, build SH-2 e uma volta completa, incluindo saida de pista.
Qualquer migracao para Slave exige nova rodada de testes de escalonamento,
boot e comparacao de telemetria com a versao Master.
