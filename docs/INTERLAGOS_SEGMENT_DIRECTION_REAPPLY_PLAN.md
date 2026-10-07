# Plano de ação: reaplicar correção de apresentação de segmentos (inversão)

**Fonte:** `C:\desenvolvimento\sega_saturn\Plano para correção de apresentação de seguimentos.txt`  
**Branch atual:** `feature/new_deteccao_solo` (HEAD ≈ `f518eba`)  
**Referência que já tinha o fix:** commit `1323917` (e sessão anterior na outra branch)

---

## Problema

Ao girar 180° (parado ou quase parado), a esteira **não** reconstrói o sentido oposto até o carro avançar de segmento. Câmera olha para segmentos não residentes → cenário “some”.

**Causa:** referência angular da câmera é sobrescrita enquanto há movimento; `ShouldAllowHeadingOnlyDirectionFlip` bloqueia inversão com deslocamento > 1 unidade/quadro.

## Solução (já validada antes)

1. **TDIR.BIN** — tangente X/Z Q15 + confiança por segmento (PATH.NYA / RDR)  
2. **`ResolveRouteRelativeDirection`** — dot câmera × tangente da pista  
3. Runtime: carregar TDIR uma vez; usar tangente no `UpdateCameraDrivenWindowDirection`  
4. Testes de política (180° sem progresso)  
5. `build_all` gera/valida TDIR  

**Fora de escopo deste reapply:** TCOL Cart, LOD0=0, SGL 2350 (causaram tela preta).

## Checklist de implementação

| # | Item | Estado |
|---|------|--------|
| 1 | `tools/build_track_direction_map.py` | Feito |
| 2 | `cd/data/TDIR.BIN` | Feito |
| 3 | `ResolveRouteRelativeDirection` em policy | Feito |
| 4 | Teste 180° sem progresso | Feito |
| 5 | Membros + `ResolveRouteDirectionTangent` + load TDIR + UpdateCamera… | Feito |
| 6 | Wiring mínimo `build_all` etapa TDIR (sem exigir TCOL) | Feito |
| 7 | Testes host + ISO | Feito (16/16 policy; ISO em `BuildDrop/`) |

## Critérios de aceite (emulador)

- Giro 180° parado: esteira começa a construir o sentido oposto no mesmo segmento  
- Giro lento em movimento: idem, sem esperar troca de segmento  
- Curva 147–159: sem falsa inversão  
- Sem regressão de FPS por spam de Debug::Print  
