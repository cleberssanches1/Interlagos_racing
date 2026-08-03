# Plano de ação — câmeras 2 e 3 em aclives e declives

## Pesquisa e referência de comportamento

Daytona USA e Virtua Racing oferecem múltiplas perspectivas, incluindo chase
views próximas e afastadas. Pela observação do arcade, essas câmeras preservam o
enquadramento do carro, mas o boom acompanha a topologia: sobe atrás do carro em
declives, desce em aclives e altera o ponto de observação sem travar em uma altura
mundial.

Uma referência técnica de câmera em terceira pessoa descreve a mesma estratégia:
posição focal mais offset, pitch automático para olhar morro acima/abaixo e
suavização para evitar acelerações abruptas. Para o Saturn, isso deve ser obtido
com a atitude já calculada pela física, sem raycasts adicionais.

Referências:

- Daytona USA arcade longplay: https://www.youtube.com/watch?v=X1V_2pvlAiE
- Manual oficial Daytona USA: https://segaretro.org/images/4/48/Daytonausa_sat_us_manual.pdf
- Game Developer, setembro de 2011, páginas 8–10:
  https://media.gdcvault.com/GD_Mag_Archives/GDM_September_2011.pdf
- Página oficial SEGA AGES de Virtua Racing:
  https://segaages.sega.com/project/virtua-racing/index.htm

## Diagnóstico no Interlagos_racing

1. A câmera já recebe `gradeTanX100` e `bodyPitchDeg`, sem consultar a pista.
2. O fallback convertia grade para pitch com `grade / 4`. A aproximação correta
   de baixo custo é `graus ~= gradeTanX100 * 57 / 100`; o fallback antigo
   representava menos da metade da inclinação real.
3. Câmera 2 seguia apenas 80% do pitch e câmera 3 apenas 70%, insuficiente em
   rampas fortes.
4. O grade lift acrescentava altura tanto em declives quanto em aclives. No
   aclive isso anulava parte do movimento para baixo produzido pelo boom.
5. O clearance do declive aguardava o pitch suavizado atingir seis graus, tarde
   demais numa quebra de topologia muito íngreme.

## Implementação

1. Usar o grade filtrado como fonte primária e recorrer ao body pitch apenas
   quando não houver grade estabelecido e a atitude superar cinco graus.
2. Converter `tan * 100` para graus com a aproximação fixed-point `*57/100`.
3. Elevar o limite de pitch da câmera de 22 para 28 graus.
4. Aplicar 100% de seguimento na câmera 2 e 90% na câmera 3.
5. Acionar o clearance de declive pelo grade filtrado, mesmo enquanto o pitch
   visual ainda converge.
6. Remover o lift de aclive que contrariava o deslocamento vertical para baixo.
7. Preservar lerp, rate limit e todos os caminhos de render no Master SH2.

## Critérios de aceite

- Câmeras 2 e 3 sobem em declive e descem em aclive, acompanhando o carro.
- A câmera não atravessa o asfalto atrás do carro em descidas fortes.
- O carro permanece enquadrado sem mudança brusca em costuras pequenas.
- Pista, background e carro continuam renderizando.
- Nenhum novo probe, raycast, alocação dinâmica ou crescimento relevante de
  estado por frame.

## Validação automatizada

- Compilação e link SH2: aprovados.
- Headers passivos e de observabilidade: aprovados.
- Testes host de frame reuse: aprovados.
- Envelope da ISO: permanece pendente por causa do conteúdo local de `cd/data`;
  foram gerados 16.025.600 bytes contra 4.134.912 esperados pelo validador.

## Refinamento anti-solavanco

O primeiro ajuste revelou que a atitude era avançada três vezes no mesmo frame e
que o Y usava o mesmo blend quase rígido de XZ. A estabilização final aplica:

1. Uma única atualização de pitch em `CameraLocation`; `LookTarget` e o offset
   reutilizam o mesmo valor no restante do frame.
2. Grade filtrado como fonte primária; body pitch só entra acima de cinco graus.
3. Histerese de grade em `tan * 100 >= 10` e deadzone visual de três graus.
4. Blend separado: XZ continua rápido, Y usa 0,20 sobre irregularidades e 0,35
   somente quando um aclive/declive real já está estabelecido.
5. Clearance final permanece depois do blend, preservando a proteção contra o
   asfalto sem fazer a câmera copiar cada movimento vertical das rodas; esse
   clamp usa somente grade filtrado, nunca um pico isolado de body pitch.
