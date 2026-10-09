# Plano de ação — slide da esteira em três quadros

## Evidência de partida

- A VRAM permanece estável (`k=201 KiB`, sem falhas de alocação).
- Os eventos longos coincidem principalmente com slides (`x=1`).
- O tempo total de renderização da pista chega perto de 90–99 ms, enquanto
  física e outras fases medidas permanecem menores.
- A implementação anterior decidia entre prefetch e slide, porém o slide ainda
  preparava geometria, slots de textura e publicava a janela no mesmo quadro.

## Causa

O caminho marcado como “staged” ainda chamava o slide determinístico monolítico.
Assim, o quadro de troca acumulava montagem do segmento, preparação das bordas,
remapeamento de faces, atualização da janela e consolidação de famílias.

## Implementação

1. Tornar a política host-testável com quatro estados:
   `None`, `PreparePrefetch`, `PrepareSlide` e `CommitSlide`.
2. No primeiro quadro, construir somente o renderer de prefetch.
3. No segundo, preparar o `SlideBackBuffer`, inclusive slots do segmento de
   entrada e transições de textura compatíveis com a mesma geometria.
4. No terceiro, publicar o back-buffer com swaps persistentes e atualizar a
   janela residente; nenhuma reconstrução do segmento ocorre nesse quadro.
5. Invalidar o back-buffer caso direção, segmento de entrada ou início da
   janela mudem antes do commit.
6. Impedir prefetch, recuperação de LOD e refresh do working set de se somarem
   aos quadros de preparação/publicação.
7. Adiar a consolidação das famílias para um quadro próprio após o commit.
8. Expor `b` na linha `L` da telemetria: `b=1` identifica preparação do
   back-buffer e `x=1` identifica sua publicação.

## Validação

- Teste host da política de quatro estados.
- Testes de streaming/observabilidade existentes.
- `git diff --check`.
- Compilação completa SH-2 e geração da ISO.
- No emulador: comparar `L... b1 x0`, `L... b0 x1`, FPS, `tr` e contagem de
  eventos longos após duas ou mais voltas.

## Rollback

Definir `kEnableStagedSlidePipeline` como `false` restaura o caminho síncrono.
