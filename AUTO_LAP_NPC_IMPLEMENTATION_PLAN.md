# Auto Lap: dois carros NPC nas linhas 0 e 2

## Contrato

- O carro principal continua na linha 1 do `PATH.NYA`, usando a física atual.
- Cada NPC conserva índice de trecho, distância no trecho e velocidade próprios.
  Eles continuam avançando mesmo quando o carro principal está parado.
- A câmera, o segmento observado e a esteira continuam pertencendo apenas ao
  carro principal. NPCs não despacham novos jobs para o Slave SH-2.
- Nesta etapa, os NPCs seguem cinematicamente suas linhas. Não há colisão
  carro-carro nem física completa de roda/parede para eles.

## Implementação

1. `AUTO_LAP_NPCS=2` habilita os dois NPCs; `0` restaura o Auto Lap de um carro.
2. Preservar a construção da linha 1 e o contato inicial com a TCOL exatamente
   no caminho de um carro. Só depois de inicializar o piloto principal, ler
   `PATH.NYA` uma segunda vez para copiar as linhas 0 e 2 aos NPCs. São duas
   leituras no boot, nunca por quadro; isto isola o caminho estável do jogador.
3. Normalizar o sentido pelo rumo do carro principal, iniciar os NPCs a 600 e
   1200 unidades adiante e calibrar a altura inicial pela TCOL. Cada um usa
   seu próprio avanço por VBlank, aceleração limitada e velocidade menor nas
   curvas. O modelo e as texturas já carregados são compartilhados; somente
   renderizador/estado visual são instâncias separadas.
4. Em Auto Lap, reservar no limite de polígonos SGL o custo de três carros.
   Desenhar pista e carro principal primeiro. Desenhar cada NPC apenas dentro
   da distância visual e quando couber no orçamento restante.

## Validação e critérios de aceite

- `PATH.NYA` atual: três linhas de 321 pontos. Auditoria offline da TCOL atual:
  321/321 pontos e 321/321 pontos médios das linhas 0 e 2 estão sobre asfalto.
- Teste host do avanço independente, volta da rota, orientação e orçamento.
- Build SH-2, testes PATH/TCOL/esteira e validadores dos headers.
- Pendente em emulador/hardware: boot, `AUTO NPC ready:2`, ambos visíveis e em
  movimento com o principal parado, uma volta completa, FPS, memória e
  estabilidade de cenário. Se a altura visual variar nas elevações, a próxima
  etapa é corrigir Y em pontos da rota com amostras TCOL esparsas no Master.
- Se a largada ainda falhar, o HUD mostra `AUTO START fail:N`: 1=carro/pista,
  2=construção do PATH, 3=rota inconsistente/fallback, 4=ponto mais próximo,
  5=contato estrito de asfalto na TCOL. Esse código identifica a etapa exata
  antes de uma nova alteração no caminho da física.
- Auditoria do erro `fail:5`: o ponto inicial da linha 1 (índice 106) está
  dentro da face 39 de asfalto da TCOL no segmento 1, inclusive na célula
  espacial correspondente. Se a consulta de início ainda falhar, somente a
  pose inicial pode usar o Y do PATH, desde que a TCOL esteja residente, o ID
  da rota seja válido e o guia esteja a até 1024 unidades do carro. As rodas
  continuam consultando a TCOL normalmente. O HUD sinaliza `AUTO START PATH Y`.
