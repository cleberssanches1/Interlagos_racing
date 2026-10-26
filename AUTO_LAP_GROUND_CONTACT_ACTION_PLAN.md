# Plano de acao: Auto Lap natural com PATH.NYA e malha TCOL

## Objetivo e limite desta etapa

Transformar a opcao `AUTO LAP` de deslocamento guiado diretamente pela rota em
um piloto que aciona o mesmo carro e a mesma fisica usados pelo jogador.
`PATH.NYA` fornece a linha de referencia, sentido e pontos de antecipacao;
`TCOL.BIN` fornece contato de solo, tipo de piso e paredes. A rota **nao**
determina a altura final do carro nem substitui colisao ou suspensao.

Estado em 26/10/2026: a primeira integracao do controlador foi compilada.
`AUTO_LAP_PHYSICS_DRIVER=1` (padrao) envia aceleracao, freio e direcao
analogica pela fisica existente; `=0` preserva o prototipo de pose direta
para comparacao. Os testes host e o build SH-2 passaram, mas ainda falta a
volta completa no emulador/hardware. As fases de auditoria geometrica fina,
afinacao de curvas e telemetria especifica abaixo continuam pendentes.

Validacao em video `auto_lap_1.mp4`: o carro desviou na largada. O guia mais
proximo do centro do segmento 1 fica em X menor, mas a primeira versao do
controlador usava o sinal do produto vetorial como sinal da direcao. A fisica
aplica yaw com sinal oposto ao comando de direcao; assim o carro esterçava
para longe do guia. A conversao foi invertida e recebeu teste com as
coordenadas reais da largada. Ainda e necessario repetir o percurso no
emulador para confirmar aderencia a rota e afinar a velocidade nas curvas.

Largada do Auto Lap fisico: o centro do segmento 1 estava cerca de 47 unidades
laterais fora da linha-guia selecionada. Agora, antes do primeiro quadro de
fisica, o carro e alinhado uma unica vez ao waypoint mais proximo do PATH,
com yaw da tangente e Y obtido de uma face `inside` de asfalto da TCOL. A
mudanca nao ocorre ao ligar/desligar a IA durante a corrida nem nos outros
modos. Falha da guia ou do contato cancela a partida automatica.

## Diagnostico do fluxo atual

- `game_loop_system.hpp` executa `BuildGameplayFrameState` e a simulacao e,
  depois, chama `UpdateAutoLapRoute` no modo Auto Lap.
- `CarSystem::PrepareGameplayFrameState` nao encaminha comandos quando
  `autoLapEnabled` esta ativo; logo o Auto Lap nao acelera, freia nem esterça
  por meio da fisica.
- `InitializeAutoLapRouteIfNeeded`, `AdvanceAutoLapPlanarPosition`,
  `AdvanceAutoLapVerticalPosition` e `UpdateAutoLapHeading` escrevem pose/yaw.
  O Y e interpolado entre alturas consultadas na rota e pode usar centro do
  segmento ou o Y do arquivo como fallback. Isso nao e o contato real das rodas.
- O caminho normal ja usa `CarPhysicsV2::StepOnce` com `GroundFollower`,
  aderencia por tipo de piso e resposta de paredes. O resultado confirmado da
  simulacao e aplicado por `ApplyResolvedFrameState`.

Consequencia: adicionar uma consulta de solo ao Auto Lap atual ate poderia
melhorar seu Y, mas manteria dois autores concorrentes da pose. A correcao
estrutural e passar a rota como **entrada de controle**, nao como saida de pose.

## Fase 0 - Baseline e contrato de seguranca

1. Registrar video e telemetria do Auto Lap atual em reta, curva, zebra, aclive
   e colisao. Capturar FPS, custo Master/Slave, segmento observado, tipo de
   piso, mascara de rodas e falhas de contato.
2. Guardar o prototipo em uma selecao/flag de diagnostico separada do novo
   controlador; nunca executar os dois escritores de pose no mesmo frame.
3. Definir a regra: na conducao fisica, somente `CarPhysics` publica X/Z/Y,
   yaw, velocidade e segmento confirmado. A camera consome essa pose publicada.
4. Nao alterar `build_all`, LODs, TCOL ou esteira nesta fase. O `PATH.NYA` e o
   `TCOL.BIN` atuais sao as entradas a validar.

## Fase 1 - Auditoria e preparacao da rota

1. Reusar o carregamento, linha escolhida e normalizacao do `PATH.NYA`.
   Verificar fechamento da volta, saltos entre pontos, direcao e mapeamento
   de segmentos, sobretudo perto da linha de largada e de curvas fechadas.
2. No carregamento, conferir pontos-chave com consulta **estrita** de asfalto
   (`inside` da face) usando TCOL. Distinguir `fallback`/`miss`; nao aceitar
   altura de centro do segmento como prova de pista sob o ponto.
3. Preparar tabela compacta e imutavel de trechos: X/Z, comprimento acumulado,
   tangente, curvatura, segmento e velocidade-alvo inicial. Evitar `double`,
   divisao cara e alocacao no caminho por frame quando houver equivalente
   precomputado/fixed-point.
4. Se a guia falhar, manter o carro parado e mostrar uma falha de rota ou
   voltar ao controle manual de forma explicita. O fallback por centros de
   segmentos fica apenas no prototipo diagnostico ate ser auditado.

## Fase 2 - Localizacao e alvo de direcao

1. Na largada do Auto Lap fisico, achar uma vez o trecho mais proximo e alinhar
   o carro a ele **antes da primeira simulacao**, com contato `inside` validado.
   Depois avancar o indice por uma janela curta em torno do indice anterior.
   Evitar busca global por frame: em Interlagos, pontos fisicamente proximos
   podem representar trechos distantes da volta.
2. Projetar a posicao publicada pelo `CarPhysics` no trecho local e escolher
   um alvo mais adiante, com distancia de antecipacao proporcional a velocidade
   e menor em curvas. So avancar o progresso ao cruzar o trecho no sentido
   correto; wrap da volta deve ser explicito.
3. Calcular erro lateral e de rumo no plano XZ, respeitando a convencao de yaw
   do carro. Um controlador deterministico (pure pursuit ou erro de rumo +
   lateral) produz esquerda/direita com zona neutra e histerese.
4. Criar testes host para reta, curva em ambos os sentidos, hairpin, retorno da
   volta, ponto perdido e trechos vizinhos proximos no espaco. O controlador
   nao pode oscilar entre esquerda/direita a cada quadro.

## Fase 3 - Velocidade e comandos reais do carro

1. Calcular velocidade-alvo por curvatura e antecipacao de frenagem; usar
   velocidades conservadoras primeiro. O piso reportado pela fisica reduz a
   meta na grama/area de escape e limita a agressividade da direcao.
2. Converter erro de velocidade em `GameplayInputSnapshot`: acelerar abaixo
   da banda, aliviar no centro e frear acima. Definir histerese para evitar
   alternancia a cada frame e limitar a frenagem perto de zero para nao
   engatar a re. Reusar a caixa automatica existente.
3. No `BuildGameplayFrameState`, selecionar **entrada do jogador ou entrada da
   IA** antes de `CarSystem::PrepareGameplayFrameState`. Na IA fisica, nao
   passar `autoLapEnabled=true` ao ramo que suprime comandos; reservar esse
   ramo exclusivamente para o prototipo de movimento direto.
4. Ao ativar/desativar a IA com Y, manter a pose e a velocidade da fisica.
   Resetar apenas integradores, histereses e indice local do controlador;
   nao reinicializar X/Z/Y ou yaw em um waypoint.

## Fase 4 - Solo, bordas, paredes e recuperacao

1. Remover da **nova** conducao as chamadas que escrevem X/Z/Y/yaw apos a
   simulacao. `GroundFollower` continua com seus probes e adesao vertical;
   a malha TCOL continua resolvendo altura, tipo de piso e paredes.
2. Alimentar a IA com o ultimo `GameplayFrameState` confirmado: velocidade,
   segmento, `groundSurfaceType`, suporte/mascara de rodas e colisao. Nao fazer
   novas consultas TCOL por waypoint em todo frame. Consultas extras devem ser
   locais, raras, medidas e justificadas por falha real de contato.
3. Sem suporte ou fora da rota: reduzir/acabar aceleracao, frear sem engatar
   re, procurar retorno numa janela curta e usar a resposta de parede da
   fisica. Nunca teletransportar o carro para a rota para esconder o erro.
4. O segmento que move a esteira vem do contato/progresso fisico confirmado,
   nao de `AdvanceAutoLapObservedSegmentIfAvailable`. Isso evita carregar
   cenario adiante de um carro parado por colisao.
5. Se a pose publicada for assincrona, o controlador usa a ultima pose
   confirmada; testar o atraso de um frame e, apenas se necessario, uma
   previsao curta limitada por velocidade. Nao esperar o Slave por padrao.

## Fase 5 - Integracao segura Master/Slave

Primeiro executar somente o controlador barato no Master, antes do pacote de
simulacao. O Slave continua fazendo a fisica existente; nao criar segundo job
Slave nem mover render/audio. Depois de medir custo e estabilidade, considerar
incluir o controlador no mesmo job de simulacao com rota imutavel e pacote de
entrada/saida fixo. Essa migracao e opcional, separada da mudanca funcional.

## Ordem de entrega e validacao

1. Auditoria de rota + testes `PATH.NYA`/TCOL; nenhuma mudanca de gameplay.
2. Helper puro de localizacao/direcao/velocidade com testes host.
3. Integracao de comandos, mantendo o prototipo anterior selecionavel; build
   SH-2, validadores passivos e boot do menu no emulador.
4. Volta lenta completa no emulador; depois aumentar velocidade por etapas.
   Testar largada, curvas apertadas, zebras, grama, muro, subida/descida e
   transicao de volta. Comparar FPS e RAM com o baseline.
5. O piloto fisico ficou selecionado por padrao para a rodada experimental,
   com retorno imediato ao prototipo por flag. A ISO compilada nao prova
   contato natural: exigir video/telemetria de uma volta e, depois, teste no
   Saturn antes de considerar o modo validado.

## Criterios de aceite

- O carro sai da largada sem salto, acelera/freia/esterça pela fisica e
  completa uma volta na ordem correta do `PATH.NYA`.
- Y, inclinacao e aderencia seguem TCOL/rodas inclusive em zebras e aclives;
  nao ha interpolacao de altura do `PATH.NYA` aplicada ao carro.
- Paredes bloqueiam o carro; a rota nao o arrasta atraves delas e a esteira
  nao avanca independentemente do contato real.
- Nao ha oscilacao persistente de volante/freio, perda crescente de FPS/RAM,
  alocacao por frame, busca global por frame ou novo trabalho Slave concorrente.
