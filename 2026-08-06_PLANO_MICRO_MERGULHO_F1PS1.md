# Plano — Micro-mergulhos no declive (pós `08-48-58`)

**Data:** 2026-08-06  
**Vídeo nosso:** `Yabause v0.9.14 2026-08-06 08-48-58.mp4` — **melhoria grande**; restam **pequenos mergulhos**  
**Alvo:** F1 PS1 1996 (Senna S) — plano contínuo, sem “tic” de nariz nas costuras  

---

## 1. Diagnóstico

| Já resolvido (J1–J3) | Ainda residual |
|----------------------|----------------|
| Embicada extrema na junção | Micro-dive / bob de pitch+heave |
| Cap hard de chord (10 u) | Cap ainda **alcançável num frame** se raw saltar 30→10 |
| Sem F/R sintético | Sem **low-pass temporal** do chord |
| Pitch 1°/f | Ainda reage a cada step do filter ΔY (4 u/f) |
| Heave deslize | Na costura, measured/target pode puxar Y em degrau suave |

**Causa residual (estilo PS1):** arcade não só limita o plano — **suaviza a taxa de mudança** do normal e do heave. Hard-cap sem LPF = o chord sobe em rampa até o teto e o nariz “mexe” a cada laje.

---

## 2. Alvo F1 96

- Pitch = rampa **contínua**, quase sem harmônicos de face  
- Heave colado sem “quique” vertical na junção  
- Câmera já OK o suficiente se o body parar de bobar  

---

## 3. Fases

| ID | Ação | Gate |
|----|------|------|
| **M1** | LPF + rate-limit do chord de atitude (estado `lastAttitudeChord`) | Sem step de pitch por laje |
| **M2** | Cap mais baixo (7 u / 3.5 u junção); face-id também = junção | Chord nunca “bate” no teto de uma vez |
| **M3** | Heave: limitar |Δtarget| por frame em costura | Sem bob vertical |
| **M4** | Wheel: ΔY jump 2 u, filter 4, dive 0.5°/f | Body inerte a seam |
| **M5** | Validar vídeo vs F1 96 | Micro-dive sumiu |

---

## 4. Status

| Fase | Estado |
|------|--------|
| **M1** | feito — LPF + rate-limit chord (1.5 u/f) |
| **M2** | feito — cap 7 u / 3.5 u; face-id = junção |
| **M3** | feito — ride target step max 4 u (2 u na junção); heave 1/4 residual |
| **M4** | feito — ΔY jump 2 u; filter 4; dive 0.5°/f; cap pitch 14° |
| M5 | rebuild + gravação vs F1 96 |

### Correção 09-05-19 (escada regrediu)

Over-smooth matou o deslize: rate 1.5 u/f no chord + heave 1/4 + ride cap 4 u → reta mid-segmento.

**Modelo (pesquisa game-dev):**  
1. *Project velocity on plane* → `dy = tanθ · speed` contínuo (heave)  
2. *Align to normal* → pitch de chord F−R / tan, LPF leve  
3. Rate-limit **só** saltos tipo escada (`|Δchord| > 16`), não a rampa real  

| Ajuste | Valor |
|--------|--------|
| max chord | 14 u (~10°) |
| stair reject | 16 u jump → step 3 u/f |
| rampa normal | blend 1/2 (sem step cap) |
| chord de grade | se F−R≈0 mas grade hold → reconstruir |
| heave | gradeDy + residual/4, sem cap contra a grade |
| pitch body | 2°/f down, filter 2, jump 8 u |

### Junção anti-embicada (2026-08-06 → reforço vídeo 12-11-18)

Quando `frontSeg ≠ rearSeg` **ou** raw F−R > grade+4u, raw spika (frente no N+1).

| Fix | Detalhe |
|-----|---------|
| `junctionAttitudeHoldFrames` | **16** frames |
| Chord na junção | **só gradeChord** (zero pull raw); step **0.5 u/f** |
| Spike mid-seg | `raw > grade + 4u` arma o mesmo hold |
| Fora da junção | dive max **1.5 u/f** chord; recover **4 u/f** |
| Pitch body | dive max **0.75°/f**; recover **2°/f** |
| Delta filter | jump max **3 u/f** |
