# Plano: distorção de faces perto da câmera (declive S)

## Sintoma
Segmento de asfalto que **sai da tela pela base** (perto da lente) fica esticado/distorcido.  
O carro já acompanha melhor o asfalto; o problema é de **framing da câmera**.

## Causa (Saturn / affine 3D)
1. **Look-at no solo próximo** — pitch do carro + lookAhead curto → o ponto de mira cai na laje sob o FOV inferior.  
2. **Raio de look com pitch cheio** — `Y' = Y cos + Z sin` empurra o look **para dentro** da pista no declive.  
3. **Altitude do boom** — se a câmera não sobe com a inclinação, o ângulo relativo ao solo aumenta o warp.  
4. **FOV** já está em 30° (razoável); não é o principal.

Não é “bug de textura” da laje: é **perspectiva + look/altitude**.

## Estratégia (arcade F1/PS1)

| # | Ação | Efeito |
|---|------|--------|
| 1 | Look **mais longe** na pista | Foco no meio da pista, não na laje sob os pés |
| 2 | Look pitch **menor** que o boom (ex. 30–40%) | Inclina a vista sem cravar no asfalto |
| 3 | **Lift** extra do look no nose-down | Compensa `Z·sin` |
| 4 | Clamp: look não mais “fundo” que o carro + margem | Evita mirar dentro da malha |
| 5 | `BuildSafeLookTarget` | Evita look quase sob a câmera |
| 6 | Boom um pouco **mais alto** + clearance no declive | Menos raspagem no near plane |
| 7 | FOV: manter ~30° (só afinar se necessário) | |

## Validação
- Chase near no S: faces da base da tela **sem stretch** forte.  
- Ainda se sente a inclinação (pitch leve).  
- Boom sem “torre” no céu.

## Implementação
Ver `camera_system.hpp` / `camera_system.cxx`.
