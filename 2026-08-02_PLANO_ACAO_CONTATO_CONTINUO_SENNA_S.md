# Plano de ação — contato contínuo no S do Senna

> **Atualização após análise do vídeo:** o plano operacional mais recente está
> em `2026-08-02_ANALISE_VIDEO_PLANO_DECLIVE.md`. A captura priorizou a validação
> do fallback externo e do consenso das rodas antes da adjacência topológica.

## Estado desta revisão

A alteração anterior de predição persistente de Y e slew descendente foi
revertida. Este documento propõe a próxima implementação; ela ainda não foi
aplicada ao runtime.

## Evidência observada

As imagens 2, 4, 6 e 8 mostram o carro parcial ou totalmente abaixo do asfalto.
Isso não é apenas oscilação de câmera ou suspensão: a posição física do chassi
está atravessando ou adotando outra camada da pista.

O auditor offline confirmou que as junções 27→42 estão soldadas em altitude.
Por outro lado, há superfícies empilhadas na região 26→27. No runtime atual,
`FindSurfaceYByFamilySet` classifica faces que contêm o mesmo XZ e escolhe a
altura mais próxima do Y do probe. O cache guarda uma única face global, mas não
há relação topológica entre a face anterior e a próxima. Assim, uma face de
outra camada pode vencer quando o carro cruza uma borda, especialmente em uma
descida forte.

## Pesquisa sobre Interlagos

- A Formula 1 descreve a volta começando com um mergulho no S do Senna e seguindo
  em descida até a curva 4. A trajetória é uma sequência fluida sobre mudanças
  de elevação e cambagem:
  https://www.formula1.com/en/information/brazil-autodromo-jose-carlos-pace-sao-paulo.5z2RfrmiTTfEP6Wnxv1yIW
- A FIA registra uma diferença de aproximadamente 40 m entre a Junção/T12 e o
  ponto de frenagem da T1:
  https://www.fia.com/sites/default/files/2017_brazilian_preview.pdf
- A análise onboard oficial da Formula 1 permite observar o chassi acompanhando
  continuamente a pista, enquanto frenagem e esterço alteram sua carga/atitude:
  https://www.formula1.com/en/latest/article/good-lap-vs-great-lap-watch-how-kimi-raikkonen-nails-a-lap-of-interlagos.BHuqlr3AjBBXIzh8DbDJQ
- Em dinâmica veicular, frenagem transfere carga para o eixo dianteiro; isso deve
  afetar grip e atitude, não teleportar a origem do carro entre alturas:
  https://www.racecar-engineering.com/articles/tech-explained-chassis/

## Princípio da solução

O carro deve permanecer na **mesma folha topológica de asfalto**. Ao atravessar
uma aresta, o contato migra para a face vizinha conectada. A altura do chassi é
então calculada diretamente no plano dessa face para o novo XZ. Como dois planos
soldados têm a mesma altura na borda, o Y resultante é contínuo por construção —
não precisa ser fabricado por lerp, snap ou predição baseada no frame anterior.

## Plano de implementação

### Fase 1 — provar a troca incorreta

1. Acrescentar ao trace existente, sem `Print` por frame, os pares
   `(segmentId, faceIndex, surfaceY)` de FL/FR/RL/RR e do centro do chassi.
2. Registrar somente eventos: mudança de face, salto de Y maior que 0,25,
   perda de suporte e troca de segmento.
3. Reproduzir 22→42 em três velocidades e em três linhas laterais.
4. Confirmar nos frames de penetração se o `faceIndex` muda para outra camada.

### Fase 2 — gerar adjacência offline

1. Estender `tools/analyze_steep_track_mesh.py` para enumerar arestas das faces
   dirigíveis e construir componentes conexos.
2. Soldar arestas equivalentes também entre NYAs consecutivos, usando XZ/Y e
   tolerância fixed-point explícita.
3. Marcar componentes como pista principal, pit/escape ou superfície isolada.
4. Gerar uma tabela compacta por face com até quatro vizinhos. Estimativa:
   quatro `int16_t` por face, sem geometria duplicada.
5. Falhar o build de assets quando uma costura da pista principal não possuir
   vizinho ou ligar alturas incompatíveis.

### Fase 3 — `SurfaceContactTracker`

1. Manter uma chave de suporte por roda: segmento, face e componente.
2. No frame seguinte, testar primeiro a face atual.
3. Se o XZ sair dela, testar apenas as faces ligadas às arestas cruzadas.
4. Permitir busca local/global somente quando a cadeia conectada perder suporte.
5. Na reaquisição, exigir:
   - mesmo componente sempre que possível;
   - face voltada para cima e dirigível;
   - distância vertical dentro de uma janela física;
   - rejeição de superfícies abaixo/atrás atravessando o plano atual.
6. Substituir o cache global compartilhado por quatro contatos persistentes ou
   por dois contatos de eixo no perfil Saturn de menor custo.

### Fase 4 — movimento sobre o plano

1. Integrar XZ normalmente.
2. Caminhar a face de suporte até aquela que contém o novo XZ.
3. Resolver `Y = planeY(X,Z) + rideHeight` diretamente na face conectada.
4. Usar as quatro alturas para construir a normal/atitude do chassi.
5. Filtrar apenas a **rotação da normal** e o curso visual da suspensão; não
   filtrar a posição física contra uma superfície contínua.
6. Projetar a velocidade longitudinal no plano tangente quando o grade estiver
   estabelecido. Gravidade de aclive/declive pode permanecer arcade e limitada.
7. Manter uma correção anti-penetração unilateral: ela só empurra o carro para o
   lado dirigível da face atual e nunca seleciona uma camada inferior.

### Fase 5 — câmera

1. Alimentar câmeras 2 e 3 com o centro do plano de suporte do chassi.
2. Suavizar o boom da câmera separadamente; não usar a câmera para mascarar
   erro de contato físico.

## Orçamento do Saturn

- Caminhar face→vizinhos custa normalmente uma face atual mais 1–3 vizinhas por
  roda, potencialmente menos que as varreduras locais atuais.
- A adjacência é gerada offline; não há construção de grafo no SH2.
- A busca ampla vira recuperação excepcional, não caminho comum.
- A primeira entrega deve usar dois contatos de eixo e ser comparada com quatro
  rodas antes de escolher o perfil final.

## Critérios de aceite

- Nenhuma troca de componente de superfície entre 22 e 42.
- Nenhum frame com o centro do chassi abaixo do plano de suporte.
- `surfaceY` contínuo nas 15 costuras soldadas de 27→42.
- Pitch acompanha o grade e roll acompanha a cambagem sem alterar a folha de
  suporte escolhida.
- Câmera não entra no asfalto e não mascara penetração do carro.
- Custo medido de faces consultadas por frame igual ou menor que o baseline.
- Build/link SH2, headers, testes host e teste visual em emulador aprovados.

## Ordem segura de entrega

1. Trace de eventos e teste reproduzível.
2. Gerador/validador de adjacência offline.
3. Tracker passivo, comparando sua decisão com a consulta atual.
4. Ativação somente para o contato central/duas linhas de eixo.
5. Expansão opcional para quatro rodas após medir custo e estabilidade.
