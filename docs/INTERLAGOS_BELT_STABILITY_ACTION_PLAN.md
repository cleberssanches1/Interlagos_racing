# Plano de ação: Esteira + texturas estáveis (Interlagos)

**Base:** `docs/INTERLAGOS_SEGMENT_PIPELINE_AND_BELT_TECHNICAL_NOTES.md`  
**Foco:** admit/pin/ownership da esteira — atacar faces vermelhas, degeneração do asfalto próximo e cliff de FPS no slide.  
**Fora de escopo agora:** reduzir janela 16, mudar budget 8, UV-unwrap massivo, rewrite offline grande.

**Constraints já fechadas:** janela `2+8+6=16`; upload/frame `8`; TBK nominal 64/64/32.

---

## Problema (do vídeo + doc)

1. Faces sem textura (vermelho/magenta) = `No_Texture` + `baseColor` sólida.
2. Asfalto dos ranks 0–1 degenera após N slides = retire/reuse de slot VDP1 em family **compartilhada**.
3. FPS cai no avanço = prefetch miss + decode/upload + soft admit + recovery empilhada.

Hipótese central: **soft admit + evicção precoce de families de alto ref-count (asfalto)** sob working-set/retire atuais.

---

## Objetivos mensuráveis

| # | Meta | Critério de aceite |
|---|------|--------------------|
| G1 | Sem faces sólidas vermelhas nos ranks 0–3 | Overlay: soft-admit faces missing ≈ 0 após 2 frames do slide |
| G2 | Asfalto próximo estável após 30+ slides | Sem “sumir/degenerar” no mesmo trecho do vídeo |
| G3 | FPS sem cliff no slide | ΔFPS no frame de slide < limiar acordado (ex. −20% vs média) |
| G4 | Telemetria legível | Contadores HUD/log: softAdmit, uploadFail, ownedMismatch, pinnedKeep |

---

## Fases

### Fase A — Telemetria de esteira (1–2 dias)  [P0] ✅ IMPLEMENTADA

**Por quê:** provar a hipótese antes de mudar política.

HUD (flag `kEnableBeltTelemetryOverlay = true` em `track_system.cxx`):

| Linha | Formato | Significado |
|-------|---------|-------------|
| 16 `BLT` | `sa om uf df mf` | softAdmit, ownedMismatch, uploadFail, decodeFail, missFamily |
| 17 `BL2` | `ms up/budget sl pfHit/Miss` | missing face slots na janela, uploads, slides, prefetch |
| 18 `BL3` | `hwr rq rf` | HWR free, retired queued/flushed |

**Done when:** rebuild + replay do vídeo correlacionando slide → `sa`/`om`/`ms` com asfalto.

---

### Fase B — Pin de families críticas (2–3 dias)  [P0 visual]

**Por quê:** asfalto (surfaceTypeId=1) é compartilhado; não pode ser retired enquanto qualquer segmento da janela (ou ranks 0–3) ainda referencia.

1. Marcar families pinned via `SFMAP` / `surfaceTypeId == asphalt` (+ opcional grass/escape).
2. Em `MergeCurrentWindowFamilies` / `ReleaseUnusedFamilyResourcesEndFrame`: **não** enfileirar retire se pinned e `refCount > 0` na janela.
3. Grace frame atual: estender para pinned (ex. 2–4 frames) ou “pin até sair da janela”.
4. Teste: forçar muitos slides; asfalto ranks 0–1 permanece texturizado.

Arquivos: `track_system.cxx` (merge/release/working-set), leitura SFMAP se ainda não no hot path.

**Done when:** G2 passa no trecho do vídeo.

---

### Fase C — Admit gate (sem soft-admit cego) (2–3 dias)  [P0 visual + FPS]

**Por quê:** soft admit coloca geometria com slots `-1` → vermelho e depois storm de uploads.

1. Definir readiness:
   - **Hard:** 100% faces com familyId>0 têm slot owned no LOD desejado; ou
   - **Prático v1:** ≥95% + **100% das faces asphalt/driveable**.
2. No `ExecuteDeterministicStabilizedSlide`:
   - Tentar prefetch + upload (bypass budget só para asphalt + N faces head).
   - Se não ready: **adiar slide 1 frame** (cap de stall frames, ex. 3) em vez de soft admit imediato.
   - Se stall esgotado: soft admit **apenas non-driveable**; driveable usa placeholder cinza (não vermelho) até recovery.
3. Recovery: `ProcessPendingStabilizedWindowLodChanges` prioriza ranks 0–3.

**Done when:** G1; menos frames com faces vermelhas pós-slide.

---

### Fase D — Prioridade de upload + anti-cliff FPS (1–2 dias)  [P1]

Manter budget **8**, mudar **ordem**:

1. Ranks 0–3 (near) primeiro.
2. Asphalt pinned.
3. Boundary promo ranks 8/9.
4. Far (10–15) só com sobra.

No slide frame:

- Limitar boundary prepare a 1 se prefetch miss no mesmo frame.
- Contador `slideCostUnits`; se > limiar, defer boundary.

**Done when:** G3 sem regredir G1/G2.

---

### Fase E — Owner token / reuse hardening (1–2 dias)  [P1]

1. Garantir todo draw path usa `IsVdp1TextureSlotActiveAndOwned` (nunca só “slot live”).
2. Aumentar `kReusableTrackSlotReuseDelayFrames` para families high-ref (ou global 3–4 se telemetria mostrar tear).
3. Log `ownedMismatchClears` — meta → 0 em volta estável.

**Done when:** mismatch ~0; sem flash de textura errada no asfalto.

---

### Fase F — Validação (contínua)

1. Replay do mesmo vídeo-trecho (Screen Recording 2026-09-20).
2. 2–3 voltas longas: sem decay de FPS por ratchet (demobilize já ajuda).
3. Overlay: softAdmit↓, uploadFail≈0, asfalto estável.
4. A/B flag: `kEnableBeltAdmitGate` / `kEnableCriticalFamilyPin` para rollback rápido.

---

## Ordem de execução

```
A (telemetria) → B (pin asfalto) → C (admit gate) → D (upload priority) → E (reuse harden) → F (validação)
```

B e C são os maiores ganhos visuais; D ataca o cliff de FPS; E fecha regressões sutis.

---

## Arquivos principais

- `src/track_system.cxx` / `.hpp` — slide, merge, release, load, demobilize
- `src/track_renderer.hpp` — `ApplyFaceTextureSlotsGlobal`
- `src/track_lod_config.hpp` — janela (não mudar neste plano)
- `docs/INTERLAGOS_SEGMENT_PIPELINE_AND_BELT_TECHNICAL_NOTES.md` — referência

## Não fazer neste plano

- Alterar `TRACK_LOD*` / budget 8 (salvo evidência pós-telemetria).
- Reintroduzir UV-unwrap em massa.
- Reescrever pipeline offline (prep/TBK já nominais).
- Ligar palette recycle end-frame (risco roxo).

## Riscos

| Risco | Mitigação |
|-------|-----------|
| Admit gate atrasa streaming | Cap stall 2–3 frames; bypass para asphalt |
| Pin segura demais VRAM | Pin só surfaceType asphalt (+ opcional); unpin ao sair da janela |
| Mais trabalho no slide | D limita boundary no mesmo frame do miss |

## Rollback

Flags compile-time:

- `kEnableCriticalFamilyPin`
- `kEnableBeltAdmitGate`
- `kEnableBeltUploadPriority`

Default ON após validação; OFF = comportamento atual.

---

## Critério de “plano completo”

Implementação A–E merged + F com vídeo de regressão anexo (mesmo percurso) mostrando: sem vermelho sistemático, asfalto próximo estável, FPS sem cliff óbvio no slide.
