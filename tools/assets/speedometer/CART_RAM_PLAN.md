# Plano: atlas do velocimetro no cartucho de 4 MB

## Estado atual e objetivo

O HUD usa `SPDHUD.TGA` (TGA indexado de 62.066 bytes). A captura `SPD:2`
confirmou que o carregador original parava na reserva de Work RAM. O caminho
atual ja le o TGA e converte `Bmp2Tile` dentro do Cart RAM: elimina o pico
temporario de aproximadamente 123 KiB de HWR e 64 KiB de LWR, mantendo
apenas o buffer de um setor do CD na HWR. A VRAM do VDP2 e a origem dos
pixels durante o jogo; nenhum pixel e recuperado do cartucho por quadro.

## Etapas

1. **Opcional: formato offline.** Acrescentar ao gerador `SPDHUD.BIN` (nome 8.3) com
   cabecalho de versao, dimensoes, modo P8, comprimentos, CRC, paleta RGB555,
   celulas 16x16 ja convertidas e mapa de indices. Gerar tambem validador de
   host que compara o pacote ao atlas TGA, inclusive indice transparente 0.
2. **Concluido: carga unica no Cart RAM.** Na inicializacao, verificar cartucho e espaco,
   alocar o tamanho validado via `CartRam::Malloc`, ler `SPDHUD.TGA` diretamente
   para la usando o caminho `Cd::File::Read` ja empregado no pipeline da pista,
   e validar cabecalho, paleta, dimensoes e tamanho antes de registrar o NBG2.
3. **Concluido: publicacao na VRAM.** Copiar paleta para CRAM, celulas e mapa para VRAM do
   NBG2 uma unica vez no Master SH-2. Manter em HWR apenas os pequenos indices
   de atlas/estado; durante a corrida, escrever so as entradas de mapa que
   mudarem. Manter o pacote no cartucho apenas se houver recarga real de VDP2;
   caso contrario, libera-lo apos a copia para evitar reserva inutil.
4. **Pendente: medicao em emulador.** Se faltar cartucho, espaco, arquivo, paleta ou VRAM,
   desativar somente o HUD e mostrar um codigo curto. Medir HWR, LWR e Cart RAM
   antes/depois do boot e verificar que nenhuma alocacao ou leitura de CD ocorre
   por quadro. Comparar a ISO atual com a nova no emulador: boot, ceu, pista,
   marcha N/R/1..8, RPM e velocidade, dia/chuva e algumas voltas.

## Criterios de aceite

- O HUD aparece e atualiza sem afetar ceu, pista, telemetria ou FPS.
- O pico de HWR/LWR do carregador TGA/Bmp2Tile desaparece; a reserva temporaria
  de Cart RAM e de cerca de 125 KiB e volta a zero apos a publicacao.
- O atlas e validado no Saturn antes de qualquer escrita de VRAM.
- Uma falha do HUD nao impede o jogo de iniciar ou renderizar a pista.
