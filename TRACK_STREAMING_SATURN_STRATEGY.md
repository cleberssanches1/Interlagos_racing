# Track Streaming Saturn Strategy

## Contexto

O runtime atual ja provou que a janela logica de `20` segmentos funciona.
O problema nao esta no contrato:

- sai `1`
- entra `1`
- `4x64`
- `5x32`
- `5x16`
- `6x8`

O problema esta no custo acumulado ao redor disso:

- textura
- preparo
- estado persistente
- draw pesado demais para o hardware

## Leitura pratica do Saturn

O Saturn aguenta muito bem um modelo hibrido:

- `VDP2` para fundo/estrada/chao
- `VDP1` para carro, laterais, muros, placas e objetos proximos

Esse caminho e mais natural para o hardware do que manter a estrada principal
como uma esteira longa de poligonos texturizados no `VDP1`.

## Caminho recomendado

### Manter

- janela logica de `20` segmentos
- slide `N -> N+20`
- `PATH`
- colisao
- streaming
- culling

### Mudar

- estrada base para `VDP2`
- detalhes 3D e objetos de beira de pista para `VDP1`

## Beneficios esperados

- menos upload de textura em corrida
- menos churn de slot e palette
- menos pressao de `HighWorkRam`
- menos draw cost por frame
- maior chance real de `30 FPS`

## Se quisermos insistir no piso em VDP1

Entao a regra deve ser esta:

1. `20` ativos
2. `1` scratch
3. `1` prefetch
4. nenhum outro estado persistente duplicando slide ou textura
5. nenhuma reconstrucao global de residencia durante corrida

Sem isso, o sistema continua correto em logica mas pesado em runtime.
