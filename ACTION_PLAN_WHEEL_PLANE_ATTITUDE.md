# Plano — atitude do carro pelo plano das rodas

## Pesquisa (resumo)

| Fonte | Lógica |
|-------|--------|
| **REDRIVER2** | 4 rodas: `FindSurfaceD2` em cada; compressão + força na **normal**; orientação do corpo via torque (pitch/roll reais). |
| **Arcade / kinematics** | Altura de contato por roda; pitch ≈ atan((Yfrente−Ytrás)/L); roll ≈ atan((Ydir−Yesq)/T); altura corpo = média. |
| **Roll axis / chassis** | Inclinação lateral = diferença de compressão L/R; longitudinal = F/R. |

## Contrato (Interlagos, performance)

```
FL,FR,RL,RR = MapHeight(canto da roda)   // top surface em (X,Z)

Yf = avg(FL,FR)   Yr = avg(RL,RR)
Yl = avg(FL,RL)   Yd = avg(FR,RR)

Y_corpo = avg(válidos) + ride
pitch   = atan((Yf − Yr) / wheelbase)     // Y-down: Yf>Yr ⇒ nariz baixo
roll    = atan((Yd − Yl) / track)         // Yd>Yl ⇒ lado direito mais baixo
```

- **4 samples/frame** (REDRIVER2 também usa 4). Cache de face mantém o custo baixo.
- Pitch/roll **não** vêm do CM — só das diferenças F/R e L/R.
- Suspensão visual por roda: offset ∝ (Y_roda − Y_plano_corpo).

## Critério de saída

- Declive: nariz desce (pitch ≠ 0).
- Banco/inclinação lateral: roll ≠ 0 se L≠R.
- Subida: sem afundar (top surface).
- FPS aceitável (4 MapHeight + cache).

## Status — implementado

- `kEnableFourWheelPlaneProbes = true` → 4 cantos FL/FR/RL/RR.
- Corpo Y = média / corda F–R.
- Pitch = atan((Yf−Yr)/L) com gain 57, máx 28°, filtro rápido.
- Roll de pista = atan((Yd−Yl)/T) + leve lean de steer.
- Suspensão visual por roda = offset F/R + L/R.
- Overlay: `OVR wh yF/yR` e distâncias por canto.
