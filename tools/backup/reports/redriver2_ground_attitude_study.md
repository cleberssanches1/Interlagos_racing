# Estudo REDRIVER2 — solo, altura e atitude (map → Interlagos)

**Fonte:** `Projects/Projetos_Exemplos/REDRIVER2-master/src_rebuild/Game/C/`  
**Arquivos-chave:** `wheelforces.c`, `dr2roads.c`, `handling.c`  
**Data:** 2026-08-07  
**Uso:** estudo de padrão (Driver 2 RE); não copiar código cegamente.

---

## 1. Arquitetura do solo no Driver 2

### 1.1 Superfície = **plano contínuo** por célula (BSP), não “F−R de lajes”

```c
// dr2roads.c — MapHeight / FindSurfaceD2
plane = sdGetCell(pos);           // BSP + cell lookup em XZ
height = sdHeightOnPlane(pos, plane);
// Plano: a*x + b*y + c*z + d ≈ 0  (fixed 12.4)
// normal = (a,b,c) normalizado approx (>>2)
```

- Cada amostra XZ devolve **um** `sdPlane` com coeficientes `(a,b,c,d)`.
- A altura é **analítica** no plano: `y = f(x,z)` do plano local.
- A normal vem do **mesmo** plano — pitch/roll do chão não são inventados com `Yf−Yr` entre eixos em segs diferentes.

### 1.2 Por roda: posição no mundo → superfície → mola → força + torque

```c
// wheelforces.c — AddWheelForcesDriver1 (loop 4 rodas)
gte_ldv0(&car_cos->wheelDisp[i]);   // offset local da roda
gte_rtv0tr();                       // × matriz do carro + posição
gte_stlvnl(wheelPos);               // wheelPos em mundo

FindSurfaceD2(wheelPos, surfaceNormal, surfacePoint, &SurfacePtr);
// surfacePoint.vy = altura no plano
// surfaceNormal   = normal do plano

// Compressão da mola (proyección na normal Y):
newCompression = FIXEDH((surfacePoint[1] - wheelPos[1]) * surfaceNormal[1]) + 14;
// clamp 0..42

// Mola + damper (arcade):
susForce = newCompression * 230 - oldCompression * 100;

// Força ao longo da NORMAL do plano (não só eixo Y mundo):
force.vy = FIXEDH(susForce * surfaceNormal[1] - velY * 12);
force.vx += susForce * surfaceNormal[0] ...
force.vz += susForce * surfaceNormal[2] ...

// Torque no monocoque: τ = r × F  (roda em relação ao CoG)
aacc[0] += FIXEDH(r_y * Fz - r_z * Fy);
aacc[1] += FIXEDH(r_z * Fx - r_x * Fz);
aacc[2] += FIXEDH(r_x * Fy - r_y * Fx);
```

### 1.3 Integração (handling.c)

```c
// Aceleração linear / angular acumulada nas rodas
linearVelocity  += acc;
angularVelocity += aacc;
// clamp avel

// Posição e quaternion de orientação
fposition += linearVelocity >> 8;
// Δquaternion de angularVelocity
orientation = normalize(orientation + δq);
// Matriz de desenho / próxima amostra de rodas = de orientation
```

**Pitch e roll não são `atan((Yf−Yr)/wb)`.**  
Eles **emergem** das 4 molas + torques: se a frente “afunda” no plano, a mola da frente empurra mais e o torque levanta o nariz / alinha o chassis ao plano **local de cada roda**.

### 1.4 Por que isso não “embica” na junção de lajes

| Situação | REDRIVER2 | Nosso risco (chord F−R) |
|----------|-----------|-------------------------|
| Frente no plano N+1, traseira no N | Cada roda amostra **seu** plano; força/torque por roda; rotação **gradual** | `rawChord = Yf−Yr` = **degrau virtual** → pitch = atan(degrau) → embicada |
| Mesma face contínua | Normal do plano = tan real | OK se F e R no mesmo plano |
| Snap de atitude | Não existe; spring + avel limitados | Snap 6°/f ou chord 20u/f = trêmulo |

Driver 2 ainda usa física discreta e “arcade”, mas o ponto crítico é:

> **Atitude = integração de forças nas rodas sobre a normal do plano, não ângulo geométrico F−R.**

---

## 2. Mapa 1:1 → Interlagos (`car_ground_follower` / `car_wheel_rig`)

| Conceito REDRIVER2 | Equivalente Interlagos | Gap atual |
|--------------------|------------------------|-----------|
| `sdGetCell` + `sdHeightOnPlane` | `TryProbeSurfaceY` / MapHeight por canto | OK em espírito; verificar se devolve face contínua ou multi-band errada |
| `FindSurfaceD2` → normal | Quase só usamos **Y**, pouco a normal | Falta normal da face para força/orientação |
| `wheelDisp` × body matrix | Probe em XZ + yaw (sem pitch do body) | Probes “planos” em yaw; body pitch é pós-processo |
| `susCompression` + `susForce` | Heave residual / adhesion | Heave é média + gradeDy, não 4 molas com torque |
| `aacc` = r × F | **Não existe** | Pitch = `atan(chord F−R)` filtrado |
| Quaternion + avel clamp | `bodyPitchDeg` rate limit | Rate limit no ângulo, não no torque |
| Plano por amostra | Chord F−R entre eixos | **Embicada na junção de segmentos** |

---

## 3. Lições acionáveis (prioridade)

### L1 — Nunca usar F−R cru como pitch quando eixos estão em “planos” diferentes  
(já no plano anti-embicada: junção → `gradeChord` / hold)

No REDRIVER2 isso é automático: cada roda tem seu plano; o monocoque gira por torque, não por `atan(Yf−Yr)`.

**No Saturn (custo baixo), proxy correto:**

```
se frontSeg ≠ rearSeg OU |rawChord − gradeChord| > limiar:
    pitchTarget = gradeTan * wheelbase   // plano de caminho contínuo
senão:
    pitchTarget = rawChord                 // mesma face
suavizar com rate 1–2°/f (não 6°)
```

### L2 — Heave = altura do plano sob o carro, não degrau de MapHeight  
REDRIVER2: mola com compressão limitada (0…42) e damper (`new*230 − old*100`).

**Proxy Saturn:**

- `rideTarget ≈ avg(4 Y) + offset`
- `bodyY += gradeDy + residual/2` com cap ~6u/f  
- **não** snap full-error 24u (gera “escada”)

### L3 — Atitude suavizada por “velocidade angular”, não por snap  
REDRIVER2 limita `angularVelocity` (hard cap) e integra.

**Proxy:** pitch step 1.5°/f dive, recover um pouco mais rápido; delta chord cap ~3–4u/f na face, **1u/f na junção**.

### L4 — (Opcional, mais fiel) 4 “molas” + pitch/roll de residual  
Se o orçamento SH2 permitir no futuro:

```
para cada roda i:
  err_i = surfaceY_i - supportY_i(bodyY, pitch, roll)
  F_i = k * err_i - d * (err_i - err_i_prev)
  bodyY  -= Σ F_i / 4
  pitch  += (F_front - F_rear) * scale   // torque em pitch
  roll   += (F_right - F_left) * scale
```

Isso é o `CornerContactSolver` que já existe no projeto (`car_corner_contact_solver.hpp`) — **ainda não integrado** no hot path do `GroundFollower`. REDRIVER2 valida que essa família de soluções é a certa.

### L5 — Amostra com o body já orientado (quando possível)  
REDRIVER2 transforma `wheelDisp` com a matriz atual.  
Hoje os probes usam sobretudo yaw. Em declive forte, o XZ da roda com pitch é um pouco diferente; efeito secundário vs L1/L2.

---

## 4. O que **não** copiar de Driver 2

- Quaternions + GTE full rigid body (caro no SH2 / SGL atual).
- 4× `FindSurface` com normal + lateral friction completa (orçamento).
- Grass roughness / wetness / tyre tracks (irrelevante para o S).

---

## 5. Checklist de implementação sugerido (ordem)

1. **Já em curso:** anti-embicada junção = grade contínuo, não F−R (L1).  
2. **Heave** tipo mola: residual 1/2 + cap 6u + ar ≤ 0.5u (L2).  
3. **Pitch rate** F1 PS1: ~1.5°/f, chord step face 3u / junção 1u (L3).  
4. **Próximo grande passo:** ligar `CornerContactSolver` (ou 4 residuals → Δpitch/Δroll) no place do `atan(chord)` puro (L4).  
5. **Offline:** validar MapHeight 27–50 só faces asfalto (multi-band polui o “plano” como no Driver 2 `sdGetCell` multi-level).

---

## 6. Referências de código (linhas aproximadas)

| Arquivo | Função | Papel |
|---------|--------|--------|
| `dr2roads.c` | `sdHeightOnPlane`, `sdGetCell`, `MapHeight`, `FindSurfaceD2` | Plano contínuo + normal |
| `wheelforces.c` | `AddWheelForcesDriver1` | 4 rodas, mola, força na normal, torque |
| `handling.c` | integração `acc`/`aacc` → vel → pos/quat | Atitude emergente |
| `handling.c` | `SetShadowPoints` | 4× MapHeight só para sombra (não física) |

---

## 7. Conclusão

REDRIVER2 **não “resolve embicada com um filtro mágico no F−R”** — ele **nem usa F−R como pitch**.  
Usa **plano por ponto + 4 contatos elásticos + torque**.

No Interlagos, o caminho fiel e barato é:

1. **Junção:** pitch = tan de caminho / grade, zero spike F−R.  
2. **Face:** pitch ≈ F−R suavizado.  
3. **Heave:** deslize contínuo colado ao avg das 4 amostras.  
4. **Evolução:** `CornerContactSolver` (4 molas → body + pitch + roll) como no Driver 2.

Isso alinha com Daytona/F1 96 no **efeito** (deslize contínuo, sem mergulho de nariz por laje), mesmo sem source da AM2.
