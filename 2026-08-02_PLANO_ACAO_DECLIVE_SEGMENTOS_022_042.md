# Plano de ação — declive dos segmentos 22–42

> **Status:** implementação de runtime revertida em 02/08/2026 após regressão
> visual. O analisador e os relatórios permanecem apenas como diagnóstico. O
> plano substituto está em `2026-08-02_PLANO_ACAO_CONTATO_CONTINUO_SENNA_S.md`.

## Objetivo

Eliminar o movimento vertical em degraus sem aumentar o número de consultas de
colisão executadas pelo SH2.

## Diagnóstico da malha

Foi criado `tools/analyze_steep_track_mesh.py` para interpretar os NYA, cruzar o
texture/material ID de cada face com `segments_map.json`, descartar paredes acima
do limite dirigível de 70 graus e medir as bordas entre segmentos.

Resultado da execução em 22–42:

- 27→28 até 41→42: junções soldadas, com diferença de altitude igual a zero.
- 22→26: junções coerentes; 22→23 possui apenas 0,0156 unidade de erro máximo.
- 26→27: alerta de 44,3754 unidades devido a superfícies empilhadas/pit; requer
  revisão de asset separada caso o carro use essa ramificação.
- Inclinação máxima dirigível detectada: `|tan| = 0,9986` no segmento 40.

Assim, o efeito de escada observado no declive principal não nasce de desnível
nas costuras. Ele era produzido pelo runtime: qualquer diferença vertical de
0,5 unidade acionava cópia integral e instantânea do novo `MapHeight`.

## Implementação

1. Ativar a predição de Y pelo grade já filtrado e pelo deslocamento longitudinal.
2. Persistir a predição em `surfaceYFiltered`, para que ela não seja descartada
   pela adesão ao final do mesmo frame.
3. Limitar a predição a 16 unidades por frame, suficiente para `tan≈0,25` em
   velocidade de corrida e ainda protegido contra valores extremos.
4. Remover o snap descendente baseado em diferença de 0,5 unidade.
5. Tratar toda diferença restante com slew de no máximo 4 unidades por frame;
   em trecho quase plano, usar metade desse orçamento.
6. Colar diretamente apenas resíduos menores ou iguais a 0,125 unidade.
7. Não adicionar probes, raycasts, busca de faces, alocação ou estado por frame.

## Critérios de aceite

- A altitude progride junto com XZ ao atravessar faces e segmentos do declive.
- Não há teletransporte vertical de uma altura de face para a seguinte.
- O carro permanece aderente em alta velocidade sem atravessar o asfalto.
- A junção 26→27 é testada separadamente, pois contém mais de uma superfície em
  altitude distinta.
- Build/link SH2, headers passivos e testes host continuam aprovados.

## Validação executada

- Auditoria 27–42: 15/15 junções soldadas, sem descontinuidade de altitude.
- Compilação e link SH2: aprovados.
- Headers passivos e de observabilidade: aprovados.
- Testes host de frame reuse: aprovados.
- ISO/CUE: geradas; o envelope continua pendente devido aos assets locais
  (16.025.600 bytes contra 4.134.912 esperados pelo validador histórico).
