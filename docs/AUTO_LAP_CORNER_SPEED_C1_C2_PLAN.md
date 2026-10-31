# Auto Lap — curvas mais rápidas (C0–C2)

**Data:** 2026-10-30  
**Cue:** `BuildDrop/Interlagos_racing.cue` (DEBUG, `AUTO_LAP_NPC_RAILS=1`)  
**Abordagem C1:** offset runtime por curvatura (apex bias), sem novo guia PATH.NYA

## Entregue

### C2 — Envelope
| Parâmetro | Antes | Agora |
|-----------|-------|-------|
| `CornerLimitKmh` bands | 40/55/85/130/190/260 | 52/70/100/150/210/280 |
| `kBrakeKmhSquaredPerWorldUnit` | 24 | 28 |
| Cap scan curto | 82 km/h | 110 km/h (`kVisibleScanCapKmh`) |

### C1 — Racing line
- `ApplyRacingLineOffset` após `RouteLookahead` no ponto alvo
- Lateral = interior do canto (`-signedTurn * kLatPerDeg`), clamp ±48 u
- `kRacingLineSign` flipável se L/R cortar ao contrário
- Player e NPC partilham a mesma função

### C0 — HUD
- Linha `(0,22)`: `AL v:%d lim:%d t:%d` (planned, binding corner limit, upcomingTurn)

## Ficheiros
- `src/auto_lap_speed_policy.hpp`
- `src/game_loop_system.hpp` (call + HUD)
- NPC: sem mudança de call site (default `outBinding=nullptr`)

## Aceite reteste
1. Hard-quit Mednafen → abrir `BuildDrop\Interlagos_racing.cue`
2. Ver `AL v: lim: t:` junto de `AL g:m:a:s`
3. Curvas médias: carro mais para dentro; `v`/`s` mais altos
4. Hairpin: trava a tempo; sem void/parede
5. Se cortar para fora: pôr `kRacingLineSign = -1` e rebuild

## Seguinte (C3 / C1.1) se preciso
- Out–in–out completo (entry fora / apex dentro / exit fora)
- Flags entry/apex/exit por índice PATH
- Trail-brake leve
