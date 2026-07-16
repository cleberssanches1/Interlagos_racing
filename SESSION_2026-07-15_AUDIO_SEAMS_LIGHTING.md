# Sessão de trabalho — 2026-07-15

**Projeto:** Interlagos_racing (SaturnRingLib / Sega Saturn)  
**Branch:** `feature/sh2`  
**Objetivo geral:** manter o jogo bootável e estável no chão, com áudio de motor confiável, costuras de asfalto sem overdraw no carro, e iluminação/atitude do chassi estáveis nas emendas de segmento.

---

## 1. Boot e memória (contexto inicial)

### Problema
- Master SH2 com **invalid opcode** no boot, em torno do caminho de som (`Sound::Initialize` / heap HWRAM).
- Heap de High Work RAM apertado (~26 KB) insuficiente para `new[]` de assets grandes de som (ex.: SDDRVS.TSK).

### O que foi feito
- Staging de load de som em **Low Work RAM** (`Memory::Zone::LWRam`) em `saturnringlib/srl_sound.hpp` (`Hardware::Initialize`), com checagens de null.
- Evitar alocações grandes no heap HWRAM durante o boot de áudio.

### Resultado
- Boot estável o suficiente para retomar o loop de jogo e testes de áudio/render.

---

## 2. Áudio do motor (SCSP loop)

### Problemas observados
1. Motor em ponto morto inconsistente / falhas de restart.
2. Após acelerar, som sumia ou volume muito baixo.
3. Clique/glitch por restarts em software (`slPCMOn` one-shot / `slPCMParmChange`).
4. Tentativa de dual-layer (idle + rev) com dois loops SCSP: som “estranho” / freeze → **revertido**.

### Caminho correto (inspirado no SlaveDriver)
- Análise do exemplo SlaveDriver (registro SCSP direto, loop em hardware).
- Implementação de **loop SCSP em hardware** em `src/scsp_engine_loop.hpp`:
  - WAV carregado via stage LWRAM
  - Slot dedicado (ex.: voice 30)
  - SoundRAM base (ex.: `0x40000`)
  - `Start` / `Update` (pitch + volume) / `Stop` sem re-key a cada frame
- `src/car_audio_system.hpp` prefere `ScspEngineLoop` com `ENGSD.WAV`, com fallback SGL se necessário.

### Decisões
| Abordagem | Status |
|-----------|--------|
| Loop SGL software restart | Glitches / volume instável |
| Dual idle+rev SCSP | Revertido (comportamento ruim) |
| **Single SCSP HW loop (ENGSD)** | **Mantido — OK** |

### Resultado
- Um único loop de motor em hardware, pitch/volume atualizados por registro, sem re-disparar o sample a cada frame.

---

## 3. Costuras de asfalto (z-fighting / overdraw no carro)

### Problema
- Asfalto desenhava **por cima do carro** nas emendas de segmento.
- Causas típicas no Saturn/SGL:
  - VDP1 **sem Z-buffer** (só sort por polígono)
  - Sort por Z médio / vértice de referência
  - Faces grandes de asfalto saindo da câmera com chave de sort “errada”

### Tentativas e aprendizado
1. **Lift visual do carro** (subir o mesh) e bias de profundidade estilo REDRIVER2 OT  
   - Lift grande → carro “voando” (ruim).  
   - Micro-lift insuficiente sozinho.
2. **Near-pass na pista** (`track_system.cxx`): sink + depth push ao longo câmera→segmento, rank ±2, distância XZ, posição do carro  
   - Ajuda nas faces próximas, não resolve sort errado de faces grandes.
3. **Insight do usuário:** quando a face de asfalto **sai da câmera**, o sort falha e o asfalto vence o carro.

### Solução adotada (grounded)
| Peça | Valor | Arquivo / local |
|------|--------|------------------|
| Lift visual do carro | **0** | `game_loop_system.hpp` (`kCarVisualLiftUnits`) |
| Sort do carro | **SORT_MIN** (vértice mais próximo) | `main.cxx` → `ForceSortMode(Minimum)` |
| Sort da pista | **SORT_MAX** (vértice mais distante) | `track_renderer.hpp` + attrs texturizados |
| Near-pass pista | **removido** (causava gaps) | `track_system.cxx` — offset único por segmento |

### Follow-up (2026-07-16): espaços entre segmentos
Causa: two-pass near com **Y sink (2)** + **depth push (6)** só nos segmentos próximos do carro. Vizinhos near/far e near/near com vetores de push diferentes abriam frestas.

Ajuste: todos os segmentos desenhados com o **mesmo `trackOffset`**; seam do carro fica só com SORT_MIN/MAX.

### Resultado
- Feedback do usuário: **“Ficou muito bom”** para o overdraw de asfalto no carro.

---

## 4. Iluminação do carro nas emendas de segmento

### Problema
- Brilho/tom do carro **mudava ao passar por segmentos** (efeito separado do sort).

### Causas identificadas
1. **Luz SGL compartilhada**  
   - `slPutPolygonX` / `DrawSmoothMesh` pode **mutar** o vetor de luz.  
   - Pista e carro no mesmo pipeline de luz/gouraud.
2. **Tabela gouraud compartilhada**  
   - Carro em offset `kCarGouraudOffset = 4096`.  
   - Pool dinâmico iniciado em `0` precisava cobrir a faixa do carro sem invadir/confundir o layout.
3. **Pitch/roll do chassi**  
   - Probes de solo (frente/trás) saltam nas emendas → pitch muda → normais giram → parece mudança de luz.
4. Cópia gouraud: callback VBlank desligado por estabilidade; cópia manual no fim do frame (`EnableManualGouraudCopy` quando smooth lighting está ativo).

### Correções implementadas

#### 4.1 Luz fixa reaplicada a cada frame
- `GameLoopSystem::RestoreDirectionalLight()`  
  - `SetDirectionalLight` + `LightSetColor` com cópia local.
- Chamado **antes da pista** e **antes do carro** em `RenderFrame`.
- `CarSystem::SetLightDirection` + rebind no `MeshRenderer` todo frame no `RenderCar`.
- `Context.lightColor` guardado junto com `lightDirection`.

#### 4.2 Isolamento gouraud pista / carro (`main.cxx`)
- Layout documentado:
  - pista (se smooth): índices baixos `[0 .. trackCap)`
  - carro: `[kCarGouraudOffset .. + carFaces)`
- Capacidade do pool = `kCarGouraudOffset + carFaces` quando o carro é smooth.
- Track cap clampado para não cruzar o offset do carro.
- Re-set de luz após `LightInitGouraudTable`.
- Cópia gouraud: manual no `SynchronizeFrameCore` (já ligada via `enableSmoothLighting`); VBlank continua off.

#### 4.3 Cópia fresca de luz por mesh da pista (`track_renderer.hpp`)
- Antes: um único `lightCopy` mutado por mesh.
- Agora: **cópia nova** de `lightSource` em cada `DrawSmoothMesh`.

#### 4.4 Suavização de pitch/roll nas emendas (`car_wheel_rig`)
| Parâmetro | Antes | Depois |
|-----------|--------|--------|
| Filtro de pitch | 1/8 | **1/16** |
| Pitch freio/parado | 1/16 | **1/32** |
| Roll | 1/8 | **1/16** |
| Low-pass ΔY | 1/4 | **1/8** |
| Deadzone de grade | 6 | **10** |
| Pitch máx. | 14° | **12°** |
| Spike de emenda | — | clamp `|ΔY jump|` ≤ **18** |
| Rate limit pitch | — | ≤ **~0.75°/frame** |

### Resultado esperado
- Brilho do carro mais estável entre segmentos.
- Menos “solavanco” visual de pitch nas juntas da pista.
- Luz direcional do mundo consistente mesmo com muitos draws smooth da pista.

---

## 5. Arquivos principais tocados (sessão)

| Área | Arquivos |
|------|----------|
| Som / HW loop | `saturnringlib/srl_sound.hpp`, `src/scsp_engine_loop.hpp`, `src/car_audio_system.hpp` |
| Sort / seams | `src/main.cxx`, `src/modelObject.hpp`, `src/track_renderer.hpp`, `src/track_system.cxx`, `src/game_loop_system.hpp` |
| Luz | `src/game_loop_system.hpp`, `src/main.cxx`, `src/car_system.hpp`, `src/track_renderer.hpp`, `src/mesh_renderer.cxx` / `modelObject.hpp` (lightCopy) |
| Pitch/seams | `src/car_wheel_rig.hpp`, `src/car_wheel_rig.cxx` |
| Build ISO | `compile.bat` → `BuildDrop/Interlagos_racing.cue` |

---

## 6. Decisões de design mantidas

1. **Carro no chão** — sem lift visual artificial para “vencer” o asfalto.
2. **Sort, não altitude** — SORT_MIN carro / SORT_MAX pista.
3. **Um loop SCSP de motor** — dual-layer adiado até calibração dedicada.
4. **Gouraud VBlank callback off** — cópia manual no fim do frame quando smooth lighting está ativo.
5. **Luz direcional fixa** `(0.35, -0.15, 0.35)` reaplicada por frame.
6. **Sem gouraud real-time compartilhado pista/carro** — carro em UseLight plano; pista sem CL_Gouraud (evita re-shade quando segmento sai da câmera).

---

## 7. Pendências / próximos passos sugeridos

- [ ] Validar em emulador (Mednafen/Kronos) e hardware: sombreamento do carro com segmentos saindo da câmera (deve permanecer estável).
- [x] Forçar shading plano estável no carro + strip gouraud na pista (follow-up: pool compartilhado).
- [ ] Dual-layer motor (idle + rev) SCSP — só após calibração de volumes/pitch.
- [ ] Calibração fina de volume SGL/SCSP (“outro dia”, se ainda necessário).
- [ ] Revisar budget HWRAM do workTable gouraud se `kCarGouraudOffset + faces` apertar o boot.

---

## 8. Como validar rapidamente

1. `compile.bat` → abrir `BuildDrop/Interlagos_racing.cue`.
2. **Áudio:** idle estável; acelerar/frear sem clique de restart; volume coerente.
3. **Seams:** câmera chase; cruzar várias juntas de segmento — asfalto não deve cobrir o carro.
4. **Luz:** observar o tom do body/paint enquanto segmentos entram/saem da câmera — sem “piscar” de sombreamento.
5. **Pitch:** rampa suave; nas emendas planas o nariz não deve dar solavanco.

### Follow-up: sombreamento ao sair da câmera
Causa 1: gouraud real-time (`UseGouraud` + `CL_Gouraud`) em tabela compartilhada.  
Causa 2 (residual): o carro ainda usava **`slPutPolygonX`** (`DrawSmoothMesh`), o caminho SGL de luz/gouraud — mesmo sem flags, o pool/estado global mudava com o número de faces da pista.

Correção final:
- Carro e pista: **unlit** (`ForceUnlitKeepTextures` — sem UseLight/UseGouraud/CL_Gouraud)
- Desenho: **`DrawAsFlat` → `slPutPolygon` (PDATA)**, nunca `slPutPolygonX`
- `LightInitGouraudTable` / `LightCopyGouraudTable` **desligados**
- Reassert unlit no carro a cada frame em `RenderCar`

---

*Documento gerado/atualizado na sessão de 2026-07-15: áudio SCSP, costuras (sort), iluminação e sombreamento estável nas emendas.*
