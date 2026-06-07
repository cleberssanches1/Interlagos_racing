# Plano de Implementação de Som — Interlagos Racing (Sega Saturn)

> Documento criado em 2026-06-07. Atualizado em 2026-06-07 com análise da técnica Gran Turismo 2.
> Cobre: som de motor com variação por RPM/marcha, pneus, câmbio e ambiência.

---

## 0. Referência de Indústria — A Técnica do Gran Turismo 2

### O que o documentário mostrou: pitch-shifting de sample único

Gran Turismo 2 (PS1, 1999, Polyphony Digital) ficou famoso por um sistema de
engine sound eficiente em memória. A técnica **é real e bem documentada** pela
comunidade de modding do GT2, e é a base do nosso design.

### Como GT2 realmente funcionava

**Não era um único sample absoluto** — eram **3 a 5 samples por carro**, cada um
gravado em "steady-state" (motor em rotação estável, em ponto morto, sem carga)
a cada ~1.000 RPM de intervalo. O pitch era então deslocado pelo hardware SPU do
PS1 para cobrir a faixa entre os pontos gravados.

```
Exemplo hipotético de um carro GT2:
  Sample A gravado a 1.500 RPM  →  pitch shift cobre 800 – 2.200 RPM
  Sample B gravado a 3.000 RPM  →  pitch shift cobre 2.200 – 4.000 RPM
  Sample C gravado a 5.500 RPM  →  pitch shift cobre 4.000 – 7.000 RPM
```

O formato interno dos arquivos de motor era **ENGN (.es)**, com containers de
loops Sony HEVAG (ADPCM, compressão ~3.5:1). Cada sample tinha dois parâmetros
chave: `RPM Pitch` (offset 0x32) e `Sample Rate` (offset 0x38 em Hz/10).

### Limite de qualidade do pitch shift

Modders descobriram que amostras começam a soar "esticadas" e artificiais quando
pitch-shiftadas além de **~500 RPM de distância** do ponto gravado. Por isso
Polyphony usava samples a cada 500–1.000 RPM e não um único sample para toda a
faixa. A crítica histórica de que motores de GT1–GT3 soavam como "aspiradores"
vem exatamente dos artefatos de pitch shift extremo nessas faixas de transição.

Modders também confirmaram que Polyphony **gravava os samples deliberadamente
mais lentos** e os acelerava via pitch no jogo — foi descoberto que aplicar um
fator de **2.5× na reprodução** produzia o som correto a 8.000 RPM, indicando
que o sample base era gravado ao redor de 3.200 RPM.

### Por que economiza memória

| Abordagem | Samples necessários | RAM (sem compressão) |
|---|---|---|
| Sample discreto a cada 500 RPM (800–8000) | ~15 samples | ~500 KB |
| Técnica GT2: poucos samples + pitch shift | 3–5 samples | ~100–165 KB |
| Sample único + pitch shift total | 1 sample | ~33 KB (qualidade ruim) |

### Diferença Saturn vs PS1

O PS1 controlava pitch via registrador `VxPitch` (1000h = 44.100 Hz), com range
teórico até 705 kHz. O Saturn SCSP usa OCT+FNS com fórmula equivalente. **O
mecanismo é idêntico conceptualmente** — a diferença é que:

- PS1 tinha **ADPCM 3.5:1** nativo → samples 3.5× menores em disco/RAM
- Saturn **não tem compressão** → cada byte de sample ocupa 1 byte de Sound RAM

Isso torna a técnica de poucos samples + pitch shift **ainda mais valiosa no
Saturn**: com menos samples, economizamos Sound RAM diretamente, sem compressão
que nos salve.

### Adoção para o Interlagos Racing

Adotaremos a mesma técnica com **2 samples de motor** (não 3 como planejado
originalmente), gravados a ~1.500 e ~5.000 RPM, com pitch shift cobrindo toda a
faixa. Isso reduz o budget de som do motor de ~99 KB para **~66 KB**, liberando
~33 KB extras para outros efeitos ou qualidade maior nos samples.

---

## 1. Contexto e Objetivo

O Interlagos Racing já possui a interface `IAudioEvents` e a implementação base
`SimpleAudioEvents` que recebe `GameplayFrameState` a cada frame (executada na
**Slave SH2**, lockstep). O objetivo deste plano é implementar um sistema de
áudio completo e performático para o hardware do Sega Saturn, priorizando:

- Som de motor variável por RPM e marcha (principal)
- Pneus (derrapagem, asfalto, kerb)
- Câmbio / gear shift
- Ambiência (vento, multidão)

---

## 2. Hardware — Restrições e Capacidades do SCSP

### 2.1 Especificações do Yamaha YMF292 (SCSP)

| Item | Valor |
|---|---|
| Chip | Yamaha YMF292 (SCSP) |
| Sound RAM | **512 KB** (DRAM 16-bit) |
| Slots de voz | **32 slots** independentes |
| Canais via SRL (SGL driver) | **4 canais PCM** de alto nível |
| Taxa de amostragem máxima | 44.100 Hz |
| Profundidade PCM | 8-bit ou 16-bit linear |
| Compressão nativa | **Nenhuma** (sem ADPCM como PS1) |
| CPU de som (68EC000) | 11.3 MHz |
| DSP integrado (FH-1) | 128 passos, reverb/chorus/delay/pitch shift |

### 2.2 Budget de Sound RAM

Com 512 KB disponíveis, o plano reserva (técnica GT2 — 2 samples de motor):

| Asset | Taxa | Bits | Duração | Tamanho | Notas |
|---|---|---|---|---|---|
| Engine loop — baixo (~1.500 RPM) | 22.050 Hz | 8-bit mono | 1.5 s | ~33 KB | Cobre 800–3.500 RPM via pitch |
| Engine loop — alto (~5.000 RPM) | 22.050 Hz | 8-bit mono | 1.5 s | ~33 KB | Cobre 3.500–8.000 RPM via pitch |
| Gear shift (up) | 22.050 Hz | 8-bit mono | 0.5 s | ~11 KB | One-shot |
| Gear shift (down) | 22.050 Hz | 8-bit mono | 0.5 s | ~11 KB | One-shot |
| Pneu — derrapagem | 11.025 Hz | 8-bit mono | 1.5 s | ~16 KB | Loop condicional |
| Pneu — kerb/vibração | 11.025 Hz | 8-bit mono | 0.5 s | ~5 KB | One-shot |
| Ambiência — multidão loop | 11.025 Hz | 8-bit mono | 2.0 s | ~22 KB | Loop contínuo |
| **Total estimado** | | | | **~131 KB** | |

**Folga disponível: ~381 KB** — liberou ~33 KB vs. plano de 3 samples anterior.
Música fica em CD-DA (stream direto do CD, não consome Sound RAM).

> **Comparação com plano anterior (3 samples):**
> - Antes: ~164 KB de Sound RAM para o motor
> - Agora (técnica GT2, 2 samples): ~131 KB
> - Economia: ~33 KB — equivale a +1.5 segundos de SFX extra disponíveis

### 2.3 Controle de Pitch via SCSP (OCT + FNS)

O SCSP controla a frequência de playback de cada slot por um par de registros de 16 bits:

```
pitch_word = ((OCT & 0xF) << 11) | (FNS & 0x3FF)

onde:
  OCT = floor(log2(sample_rate_alvo / 44100))
  FNS = round((sample_rate_alvo / (44100 / 2^(-OCT)) - 1) * 1024)
```

O SRL encapsula isso via macros em `srl_sound.hpp`:

```cpp
uint16_t oct = PCM_CALC_OCT(target_hz);
uint16_t shift = PCM_CALC_SHIFT_FREQ(oct);
uint16_t fns = PCM_CALC_FNS(target_hz, shift);
Pcm::Channels[ch].pitch = PCM_SET_PITCH_WORD(oct, fns);
```

**Crucial:** o pitch pode ser atualizado frame a frame **sem reiniciar o sample**
via `slPCMParmChange()` (SGL), que o SRL chama internamente ao alterar
`Pcm::Channels[ch].pitch`. Isso é o mecanismo central do engine sound.

**Referência SlaveDriver:** `WEAPON.C:882-895` — `afterTouch->reg[8] = grunch`
atualiza pitch em tempo real escrevendo diretamente no registrador SCSP sem
retrigger da voz. O mesmo princípio se aplica via SRL.

---

## 3. Arquitetura da Solução

### 3.1 Estratégia de Engine Sound: Técnica Gran Turismo 2

Adotaremos a **Estratégia GT2 — 2 samples steady-state com pitch shift de hardware**,
inspirada diretamente no método documentado da Polyphony Digital:

| Estratégia | Qualidade | Memória | CPU | Escolha |
|---|---|---|---|---|
| A) 1 sample + pitch shift total | Baixa (chipmunk effect) | ~33 KB | Mínima | Não |
| **B) 2 samples + pitch shift (técnica GT2)** | **Boa** | **~66 KB** | **Mínima** | **✓ Sim** |
| C) 3 samples + pitch shift | Melhor | ~99 KB | Mínima | Reserva |
| D) Granular synthesis | Excelente | Alta | Inviável no SH2 | Não |

**Como funciona a Estratégia GT2:**

Dois samples são gravados em "steady-state" (rotação estável, motor em ponto morto,
sem carga — exatamente como Polyphony gravava). O hardware SCSP varia o pitch
continuamente para cobrir toda a faixa de RPM do carro:

```
Sample LOW  gravado a ~1.500 RPM  →  pitch shift cobre  800 – 3.500 RPM
Sample HIGH gravado a ~5.000 RPM  →  pitch shift cobre 3.500 – 8.000 RPM
```

A transição entre os dois samples acontece próxima de 3.500 RPM via crossfade
de volume, exatamente como GT2 fazia entre seus containers ENGN.

**Por que funciona:** dentro de uma faixa de ~2.000–2.500 RPM em torno do ponto
gravado, o pitch shift via OCT+FNS do SCSP mantém qualidade aceitável. Além desse
range os artefatos surgem (como documentado pelos modders de GT2 que descobriram
o fator de 2.5×). Com 2 samples, cada um nunca precisa ser esticado mais do que
~2× em frequência — dentro do limite aceitável.

**Onde gravar os samples:** idealmente usar referências reais de carros de corrida
em estado estável. Ferramentas como Audacity permitem estabilizar o loop no
zero-crossing para eliminar o clique na repetição.

### 3.2 Alocação de Canais PCM

O SRL disponibiliza 4 canais PCM via SGL driver:

| Canal | Uso | Tipo |
|---|---|---|
| **0** | Motor (loop ativo com pitch dinâmico) | Loop contínuo |
| **1** | Pneu (derrapagem / kerb) | Loop condicional |
| **2** | Câmbio / impactos one-shot | One-shot |
| **3** | Ambiência (multidão loop) | Loop contínuo |

### 3.3 Modelo de RPM

O `GameplayFrameState` fornece `throttle` (0–100) e `speedKmh`. Precisamos de um
RPM proxy por marcha. A `SimpleAudioEvents` já tem `rpmProxy` calculado como:

```cpp
rpmProxy = 24 + (throttle * 2);  // range: 24–224 (8-bit proxy)
```

Estenderemos isso para um modelo mais realista com marchas:

```cpp
// RPM = rot. do motor = vel_roda * ratio_marcha * ratio_diferencial
// Aproximação: rpm_proxy = base_rpm[gear] + speed_factor * range_rpm[gear]

static constexpr uint16_t kRpmBase[6]  = { 800, 1200, 1500, 2000, 2500, 3000 };
static constexpr uint16_t kRpmRange[6] = {3200, 3500, 3800, 4000, 4000, 4200 };

uint16_t ComputeRpm(uint8_t gear, uint8_t speed_normalized) {
    // speed_normalized: 0–255 dentro da faixa da marcha atual
    return kRpmBase[gear] + (kRpmRange[gear] * speed_normalized) / 255;
}
```

---

## 4. Plano de Implementação — Fases

### Fase 0 — Preparação de Assets de Áudio

**Objetivo:** Produzir e converter os arquivos PCM para o formato Saturn.

#### 4.0.1 Gravação / Síntese dos Samples de Motor

Opções:
1. **Sintetizador online**: [freesound.org](https://freesound.org) — filtrar por "engine loop", "race car idle", "race car high rpm"
2. **Gerador de som de motor**: Gráficos de síntese com [Audacity](https://www.audacityteam.org/) — ton com harmônicos de motor a explosão
3. **Referência**: Samples do arquivo [VGMRips](https://vgmrips.net/packs/pack/sega-rally-championship-sega-saturn) do Sega Rally Saturn (estudo/referência)

**Pontos de loop:** Usar Audacity para identificar **zero-crossings** estáveis e
criar loops sem clique. O loop precisa ter pelo menos `0x900` bytes (limite mínimo do SRL).

#### 4.0.2 Conversão para Formato Saturn

```bash
# 1. Exportar do Audacity como WAV:
#    - Formato: PCM signed 16-bit big-endian
#    - Canais: Mono
#    - Taxa: 22050 Hz

# 2. Verificar com ffmpeg:
ffmpeg -i engine_low.wav -ar 22050 -ac 1 -f s16be engine_low_raw.pcm

# Alternativa: usar como .WAV diretamente (SRL::Sound::Pcm::WaveSound suporta)
```

**O SRL suporta WAV diretamente** via `WaveSound` — não é necessário converter
para raw PCM nesta fase. Economiza tempo de tooling.

#### 4.0.3 Organização no CD

```
cd/
  data/
    ENGINE_LO.WAV    # Loop idle/baixo RPM
    ENGINE_MD.WAV    # Loop médio RPM
    ENGINE_HI.WAV    # Loop alto RPM
    SHIFT_UP.WAV     # Gear shift up
    SHIFT_DN.WAV     # Gear shift down
    TIRE_SLD.WAV     # Derrapagem
    TIRE_KRB.WAV     # Kerb
    CROWD.WAV        # Ambiência multidão
```

---

### Fase 1 — Implementação do Gerenciador de Áudio

**Arquivo novo:** `src/car_audio_system.hpp`

#### 4.1.1 Estrutura de Dados

```cpp
#include "srl_sound.hpp"

// Técnica GT2: 2 samples steady-state + pitch shift de hardware (SCSP OCT+FNS)
class CarAudioSystem : public IAudioEvents {
public:
    // --- Configuração ---
    struct Config {
        // RPM de cruzamento entre sample LOW e HIGH (técnica GT2)
        // Abaixo -> engine_low, acima -> engine_high
        uint16_t rpmCrossover = 3500;

        // Crossfade de volume ao trocar de sample (em frames a 30 FPS)
        uint8_t crossfadeFrames = 8;

        // Volume mínimo do canal que está saindo durante crossfade
        uint8_t crossfadeMinVol = 0;

        // speedKmh mínimo para ativar som de pneu
        uint8_t skidThreshold = 30;
    };

    // --- Estado runtime ---
    struct State {
        uint8_t  activeEngineSample = 0;   // 0=low, 1=high  (técnica GT2)
        uint8_t  crossfadeFrame = 0;       // 0 = sem crossfade em andamento
        uint16_t currentRpm = 800;
        uint8_t  currentGear = 0;
        bool     skidActive = false;
    };

    // --- Snapshot para HUD/telemetria ---
    struct AudioSnapshot {
        uint16_t currentRpm;
        uint8_t  activeSample;       // 0=low, 1=high
        uint16_t enginePitchWord;    // valor raw OCT+FNS enviado ao SCSP
        bool     skidActive;
    };

    void Initialize();
    void OnFrame(const GameplayFrameState& frameState) override;
    const AudioSnapshot& GetSnapshot() const { return snapshot_; }

private:
    // Técnica GT2: apenas 2 samples de motor
    SRL::Sound::Pcm::WaveSound* engineSamples_[2] = {};  // [0]=low, [1]=high
    SRL::Sound::Pcm::WaveSound* shiftUp_   = nullptr;
    SRL::Sound::Pcm::WaveSound* shiftDown_ = nullptr;
    SRL::Sound::Pcm::WaveSound* tireSkid_  = nullptr;
    SRL::Sound::Pcm::WaveSound* tireKerb_  = nullptr;
    SRL::Sound::Pcm::WaveSound* crowd_     = nullptr;

    Config config_;
    State state_;
    AudioSnapshot snapshot_;

    // Sample rate (Hz) em que cada sample foi gravado — usado no pitch formula
    // Polyphony gravava mais lento e acelerava; aqui gravamos na frequência alvo
    static constexpr uint16_t kSampleBaseHz[2]  = { 22050, 22050 };
    // RPM em que cada sample foi gravado em steady-state
    static constexpr uint16_t kSampleBaseRpm[2] = { 1500,  5000  };

    void TickEngine(const GameplayFrameState& fs);
    void TickTire(const GameplayFrameState& fs);
    void TickGearShift(const GameplayFrameState& fs);

    uint16_t ComputeRpm(const GameplayFrameState& fs) const;
    uint8_t  SelectSample(uint16_t rpm) const;
    uint16_t ComputePitchWord(uint16_t rpm, uint8_t sampleIdx) const;
    void     SetEnginePitch(uint16_t pitchWord);
    void     CrossfadeSamples(uint8_t from, uint8_t to);
};
```

#### 4.1.2 Implementação do Loop de Motor

```cpp
// Técnica GT2: escolhe entre 2 samples conforme RPM cruza o limiar
uint8_t CarAudioSystem::SelectSample(uint16_t rpm) const {
    return (rpm >= config_.rpmCrossover) ? 1u : 0u;  // 0=low, 1=high
}

void CarAudioSystem::TickEngine(const GameplayFrameState& fs) {
    const uint16_t rpm          = ComputeRpm(fs);
    const uint8_t  targetSample = SelectSample(rpm);

    // Trocar sample se cruzou o limiar (técnica GT2: crossfade como ENGN containers)
    if (targetSample != state_.activeEngineSample && state_.crossfadeFrame == 0) {
        CrossfadeSamples(state_.activeEngineSample, targetSample);
        state_.activeEngineSample = targetSample;
    }

    // Calcular pitch proporcional ao RPM atual (núcleo da técnica GT2)
    // freq_alvo = freq_gravada * (rpm_atual / rpm_gravado)
    const uint16_t pitchWord = ComputePitchWord(rpm, state_.activeEngineSample);
    SetEnginePitch(pitchWord);

    snapshot_.currentRpm      = rpm;
    snapshot_.activeSample    = state_.activeEngineSample;
    snapshot_.enginePitchWord = pitchWord;
}

uint16_t CarAudioSystem::ComputeRpm(const GameplayFrameState& fs) const {
    // Modelo por marcha: RPM = base_marcha + (velocidade_na_faixa * range_marcha)
    // Aritmética inteira pura — sem float, seguro na Slave SH2
    const uint8_t gear = fs.currentGear;  // 0–5
    static constexpr uint16_t kBase[6]  = {  800, 1400, 2000, 2600, 3200, 3800 };
    static constexpr uint16_t kRange[6] = { 1700, 2100, 2500, 2900, 2900, 2600 };
    const uint16_t factor = (fs.speedKmh > 200u) ? 255u
                          : static_cast<uint16_t>((fs.speedKmh * 255u) / 200u);
    return kBase[gear] + (kRange[gear] * factor) / 255u;
}

uint16_t CarAudioSystem::ComputePitchWord(uint16_t rpm, uint8_t idx) const {
    // Fórmula GT2: estica ou comprime o sample proporcionalmente ao RPM
    //   targetHz = sample_base_hz * (rpm / sample_base_rpm)
    // Equivalente a: o SCSP reproduz o sample X vezes mais rápido/lento,
    // mudando o pitch percebido — exatamente o que Polyphony fazia.
    const uint32_t baseHz   = kSampleBaseHz[idx];
    const uint32_t baseRpm  = kSampleBaseRpm[idx];
    const uint32_t targetHz = (baseHz * rpm) / baseRpm;

    // Clamp: SCSP aceita até 44.100 Hz; mínimo 4.000 Hz para evitar overflow
    const uint32_t hz = (targetHz > 44100u) ? 44100u
                      : (targetHz < 4000u)  ? 4000u
                      : targetHz;

    // Macros SRL encapsulam a fórmula OCT+FNS do SCSP
    const uint16_t oct   = PCM_CALC_OCT(hz);
    const uint16_t shf   = PCM_CALC_SHIFT_FREQ(oct);
    const uint16_t fns   = PCM_CALC_FNS(hz, shf);
    return PCM_SET_PITCH_WORD(oct, fns);
}

void CarAudioSystem::SetEnginePitch(uint16_t pitchWord) {
    // Escreve diretamente na struct do SGL — sem reiniciar o loop.
    // slPCMParmChange() aplica ao hardware SCSP em SRL::Core::Synchronize().
    SRL::Sound::Pcm::Channels[0].pitch = pitchWord;
}
```

#### 4.1.3 Crossfade entre Samples de Motor

```cpp
void CarAudioSystem::CrossfadeSamples(uint8_t from, uint8_t to) {
    // Fade out do sample antigo (canal 0) e fade in do novo
    // Como só temos 4 canais e o motor ocupa o canal 0,
    // a transição é feita via volume gradual:
    // Frame 0-N: vol_from = 127*(N-f)/N, vol_to começa a tocar em canal 0

    // Para simplificar na Fase 1: troca imediata com volume em 0 por 1 frame
    SRL::Sound::Pcm::StopSound(0);
    engineSamples_[to]->PlayOnChannel(0, 127, 0);

    // NOTA: Em Fase 2 implementar crossfade real usando 2 subcycles de volume
}
```

---

### Fase 2 — Som de Pneus

```cpp
void CarAudioSystem::TickTire(const GameplayFrameState& fs) {
    const bool shouldSkid = (fs.wheelsSpinning && fs.speedKmh > config_.skidThreshold);

    if (shouldSkid && !state_.skidActive) {
        tireSkid_->PlayOnChannel(1, 100, 0);
        state_.skidActive = true;
    } else if (!shouldSkid && state_.skidActive) {
        SRL::Sound::Pcm::StopSound(1);
        state_.skidActive = false;
    }

    // Volume do pneu proporcional à intensidade do derrapamento
    if (state_.skidActive) {
        const uint8_t skidVol = static_cast<uint8_t>(
            (fs.lateralG * 127) / 100);  // fs.lateralG: 0–100
        SRL::Sound::Pcm::SetVolumePan(1, skidVol, 0);
    }

    // Kerb: detectar quando roda passa em balizador/kerb
    if (fs.onKerb) {
        if (SRL::Sound::Pcm::IsChannelFree(2)) {
            tireKerb_->PlayOnChannel(2, 80, 0);
        }
    }

    snapshot_.skidActive = state_.skidActive;
}
```

---

### Fase 3 — Som de Câmbio / Gear Shift

```cpp
void CarAudioSystem::TickGearShift(const GameplayFrameState& fs) {
    if (fs.currentGear == state_.currentGear) return;

    const bool upShift = (fs.currentGear > state_.currentGear);
    SRL::Sound::Pcm::WaveSound* sfx = upShift ? shiftUp_ : shiftDown_;

    // Canal 2 compartilhado com kerb — prioriza câmbio
    SRL::Sound::Pcm::StopSound(2);
    sfx->PlayOnChannel(2, 110, 0);

    state_.currentGear = fs.currentGear;
}
```

---

### Fase 4 — Integração com SimpleAudioEvents e Game Loop

#### 4.4.1 Substituir SimpleAudioEvents por CarAudioSystem

Em `src/main.cxx`, onde hoje está:

```cpp
// Antes:
SimpleAudioEvents audioEvents;
loopContext.audioEvents = &audioEvents;

// Depois:
CarAudioSystem audioSystem;
audioSystem.Initialize();
loopContext.audioEvents = &audioSystem;
```

#### 4.4.2 Initialize — Carregar Assets do CD

```cpp
void CarAudioSystem::Initialize() {
    // LWRAM para samples — economiza HWRAM para o sistema
    SRL::Sound::Pcm::SetMemAllocationBehaviour(
        SRL::Sound::Pcm::PcmMalloc::LwRam,
        SRL::Sound::Pcm::PcmMalloc::LwRam);

    // Técnica GT2: apenas 2 samples de motor gravados em steady-state
    // ENGINE_LO: gravado a ~1.500 RPM, cobre 800–3.500 RPM via pitch shift
    // ENGINE_HI: gravado a ~5.000 RPM, cobre 3.500–8.000 RPM via pitch shift
    engineSamples_[0] = lwnew SRL::Sound::Pcm::WaveSound("ENGINE_LO.WAV");
    engineSamples_[1] = lwnew SRL::Sound::Pcm::WaveSound("ENGINE_HI.WAV");

    shiftUp_   = lwnew SRL::Sound::Pcm::WaveSound("SHIFT_UP.WAV");
    shiftDown_ = lwnew SRL::Sound::Pcm::WaveSound("SHIFT_DN.WAV");
    tireSkid_  = lwnew SRL::Sound::Pcm::WaveSound("TIRE_SLD.WAV");
    tireKerb_  = lwnew SRL::Sound::Pcm::WaveSound("TIRE_KRB.WAV");
    crowd_     = lwnew SRL::Sound::Pcm::WaveSound("CROWD.WAV");

    // Inicia loops contínuos já na tela de loading
    engineSamples_[0]->PlayOnChannel(0, 127, 0);  // Motor: começa no sample low (idle)
    crowd_->PlayOnChannel(3, 50, 0);               // Ambiência ao fundo
    state_.activeEngineSample = 0;
    state_.currentGear        = 0;
    state_.currentRpm         = 800;
}
```

#### 4.4.3 OnFrame — Dispatcher Principal

```cpp
void CarAudioSystem::OnFrame(const GameplayFrameState& fs) {
    // Executado na Slave SH2 (lockstep) — budget de tempo limitado
    // Todas as operações devem ser O(1) e sem alocação
    TickEngine(fs);
    TickGearShift(fs);
    TickTire(fs);
    // TickCrowd é estático, crowd toca em loop — sem lógica extra na Fase 1
}
```

---

### Fase 5 — Música de Fundo via CD-DA

Para a trilha sonora em loop, usar CD-DA (stream direto do CD sem custo de Sound RAM):

```cpp
// Em main.cxx, após inicialização:
SRL::Sound::Cdda::SetVolume(6, 6);       // Volume L/R (0-7)
SRL::Sound::Cdda::PlaySingle(2, true);   // Faixa 2 do CD em loop

// Para pausar em menus:
SRL::Sound::Cdda::StopPause();

// Para retomar:
SRL::Sound::Cdda::Resume();  // Usa FAD salvo — retoma exatamente de onde parou
```

**Layout do CD sugerido:**
- Faixa 1: Dados (obrigatório — ISO 9660)
- Faixa 2: Trilha da corrida — loop principal
- Faixa 3: Trilha do menu
- Faixa 4: Trilha de vitória

---

## 5. GameplayFrameState — Campos Necessários

Os campos abaixo precisam existir em `GameplayFrameState` para alimentar o sistema
de áudio. Verifique quais já existem e adicione os faltantes:

| Campo | Tipo | Descrição | Status |
|---|---|---|---|
| `throttle` | `uint8_t` (0–100) | Posição do acelerador | Já existe |
| `speedKmh` | `uint16_t` | Velocidade atual | Já existe |
| `currentGear` | `uint8_t` (0–5) | Marcha engajada | Verificar |
| `wheelsSpinning` | `bool` | Roda girando sem aderência | Verificar |
| `lateralG` | `uint8_t` (0–100) | Força lateral (intensidade da derrapagem) | Verificar |
| `onKerb` | `bool` | Roda sobre balizador/kerb | Verificar |
| `resetRequested` | `bool` | Reset de posição | Já existe |

Se `currentGear` não existir no `GameplayFrameState`, pode ser calculado no
`CarAudioSystem::OnFrame` a partir de `speedKmh` usando uma tabela de ratio de marchas.

---

## 6. Considerações de Performance (Slave SH2)

O `OnFrame` roda na **Slave SH2 em lockstep**, compartilhando o processador com
physics/gameplay. O budget de tempo é restrito.

### 6.1 Regras de Implementação

1. **Sem malloc** em `OnFrame` — todos os samples são pré-alocados em `Initialize()`
2. **Sem floating point** se possível — usar aritmética inteira (Q8.8 ou escala 256)
3. **Pitch computation** é só multiplicação inteira + tabela `LogTable[]` já pré-computada
4. **Sem leitura de CD** em runtime — todos os samples devem estar em Sound RAM antes da corrida
5. **Loop detection** do SRL é via flag — `IsChannelFree()` é O(1)

### 6.2 Custo Estimado por Frame

| Operação | Custo estimado (ciclos SH2) |
|---|---|
| `ComputeRpm()` | ~20 ciclos (multiplicação inteira) |
| `ComputePitchWord()` | ~60 ciclos (mult + lookup tabela) |
| `SetEnginePitch()` | ~5 ciclos (write de registrador) |
| `TickTire()` | ~30 ciclos (branch + write vol) |
| `TickGearShift()` | ~10 ciclos (comparação + branch) |
| **Total estimado** | **~125 ciclos** |

Em 30 FPS / 200 MHz, o budget total da Slave é ~6.6M ciclos/frame. O custo de
~125 ciclos é desprezível.

### 6.3 Sincronização com SRL (slPCMParmChange)

A atualização de pitch via `Pcm::Channels[].pitch` é aplicada ao hardware SCSP
quando o SGL driver executa `slPCMParmChange()`. Isso ocorre automaticamente
durante `SRL::Core::Synchronize()` no Master — **não é necessário chamar nada
explicitamente**. O valor escrito na Slave estará visível ao Master via memória
compartilhada (sem race condition pois é lockstep).

---

## 7. Diagrama de Fluxo de Áudio por Frame

```
Slave SH2 (SimulationTask::Do):
  ├─ gameplayTick->Tick(frameState)    [já existente]
  ├─ carPhysics->Step(frameState)      [já existente]
  ├─ audioEvents->OnFrame(frameState)  [CarAudioSystem]
  │   ├─ ComputeRpm(fs)               -> rpm atual
  │   ├─ SelectSample(rpm)            -> 0/1/2 (low/mid/high)
  │   ├─ ComputePitchWord(rpm, idx)   -> pitch_word (OCT+FNS)
  │   ├─ Pcm::Channels[0].pitch = pw  -> write na struct do SGL
  │   ├─ TickGearShift(fs)            -> one-shot no canal 2
  │   └─ TickTire(fs)                 -> loop/stop no canal 1
  └─ output->frameState = state

Master SH2 (após drain da Slave):
  └─ SRL::Core::Synchronize()
      └─ slPCMParmChange()            -> aplica pitch_word ao SCSP hardware
```

---

## 8. Cronograma de Implementação

| Fase | Entregável | Estimativa |
|---|---|---|
| 0 | Assets de áudio criados e convertidos para WAV 22050/8-bit | 1–2 dias |
| 1 | `CarAudioSystem` com motor loop + pitch shift funcional | 2–3 dias |
| 2 | Som de pneus (derrapagem + kerb) | 1 dia |
| 3 | Sons de câmbio | 0.5 dia |
| 4 | Integração completa com game loop + testes | 1 dia |
| 5 | CD-DA música de fundo | 0.5 dia |
| 6 | Ajuste fino de volumes, curvas de RPM, crossfade | 1–2 dias |
| **Total** | | **~7–10 dias** |

---

## 9. Checklist de Implementação

### Assets
- [ ] Gravar / obter sample de motor baixo RPM (idle ~800-2500 rpm)
- [ ] Gravar / obter sample de motor médio RPM (~2500-5500 rpm)
- [ ] Gravar / obter sample de motor alto RPM (~5500-8000 rpm)
- [ ] Verificar que loops têm zero-crossings adequados (sem clique)
- [ ] Converter para WAV mono 22050 Hz 16-bit (SRL aceita WAV diretamente)
- [ ] Gravar shift up, shift down, tire skid, tire kerb, crowd
- [ ] Copiar assets para `cd/data/`

### Código
- [ ] Criar `src/car_audio_system.hpp`
- [ ] Implementar `Initialize()` com carregamento via LWRAM
- [ ] Implementar `ComputeRpm()` com modelo por marcha
- [ ] Implementar `ComputePitchWord()` com fórmula OCT+FNS
- [ ] Implementar `TickEngine()` com pitch update por frame
- [ ] Implementar `TickTire()` com skid e kerb
- [ ] Implementar `TickGearShift()` com one-shot
- [ ] Substituir `SimpleAudioEvents` por `CarAudioSystem` em `main.cxx`
- [ ] Verificar que `currentGear`, `wheelsSpinning`, `lateralG`, `onKerb` existem em `GameplayFrameState`
- [ ] Adicionar `AudioSnapshot` ao HUD de telemetria (rpm, pitchWord, activeSample)

### Validação
- [ ] Confirmar que pitch sobe com aceleração e desce ao soltar
- [ ] Confirmar que sample troca ao cruzar limiares de RPM sem clique
- [ ] Confirmar que câmbio toca one-shot correto (up/down)
- [ ] Confirmar que pneu para de tocar ao sair da derrapagem
- [ ] Medir custo em ciclos da Slave (via `simSlaveTicks` no HUD)
- [ ] Confirmar que Sound RAM não ultrapassa 512 KB

---

## 10. Referências

### Código Local

| Arquivo | Relevância |
|---|---|
| `saturnringlib/srl_sound.hpp` | API completa de áudio SRL (PCM, CDDA, pitch macros) |
| `Samples/Sound - PCM/src/main.cxx` | Exemplo de uso de WaveSound e RawPcm |
| `Samples/Sound - CDDA/src/main.cxx` | Exemplo de CDDA com análise de volume |
| `src/simple_audio_events.hpp` | Implementação base atual a ser substituída |
| `src/interfaces.hpp:272` | Interface `IAudioEvents` |
| `src/game_loop_system.hpp:1328` | Dispatch da simulação (onde OnFrame é chamado) |

### SlaveDriver Engine (Referência de Baixo Nível SCSP)

| Arquivo | Técnica relevante |
|---|---|
| `SOUND.C:267` | `initSoundRegs()` — estrutura completa de registro de voz SCSP |
| `SOUND.C:297` | `playSoundMegaE()` — retorna ponteiro para registrador, permite pitch em tempo real |
| `WEAPON.C:882` | Atualização de `reg[8]` (pitch) frame a frame sem retrigger |
| `AI.C:2166` | Modificação de octave isolado no pitch word |
| `UTIL/MAKESND.C:28` | Fórmula completa de encoding OCT+FNS a partir de sample rate |
| `AI2.C:120` | Ring buffer streaming de áudio (para futuro streaming de motor) |

### Documentação Técnica do SCSP

| Recurso | URL / Localização |
|---|---|
| SCSP User's Manual | https://docs.exodusemulator.com/Archives/SSDDV25/segahtml/xindex/hard/scsp/index.htm |
| Yamaha YMF292 — Especificações | https://grokipedia.com/page/yamaha_ymf292/ |
| SCSP — Yabause Wiki | https://wiki.yabause.org/index.php5?title=SCSP |
| Sega Saturn Architecture | https://www.copetti.org/writings/consoles/sega-saturn/ |
| PonèSound (driver 68K open-source) | https://github.com/ponut64/SCSP_poneSound |

### Técnicas de Engine Sound

| Recurso | Foco |
|---|---|
| Audiokinetic Blog — REV Engine Sound | https://blog.audiokinetic.com/make-racing-engine-sound-based-on-rev2/ |
| GameDev.net — Car Sound Pitch Shift | https://gamedev.net/forums/topic/278465-car-sound-pitch-shift/2734274 |
| Lost Chocolate Lab — Racing Game Sound Study | http://blog.lostchocolatelab.com/2012/05/racing-game-sound-study.html |

### Gran Turismo 2 — Referência Direta (Técnica Base Adotada)

| Recurso | Relevância |
|---|---|
| GTPlanet Forums — GT2 Sound Modding | https://www.gtplanet.net/forum/threads/gran-turismo-2-sound-modding.353348/ |
| GitHub — GranTurismoENGNEditor | https://github.com/TheAdmiester/GranTurismoENGNEditor |
| GT Modding Hub — Sound Editing | https://github.com/Nenkai/Gran-Turismo-Modding-Guides/blob/main/4.%20Sound%20Editing/Sound_Editing.md |
| GT2 Full File Documentation (PDF) | https://nenkai.github.io/gt-modding-hub/ps1/gt2/documents/Gran_Turismo_2_files_full_documentation.pdf |
| PS1 SPU — VxPitch e PMON | https://psx-spx.consoledev.net/soundprocessingunitspu/ |
| Kazunori Yamauchi — Future of GT Sounds | https://www.gtplanet.net/kazunori-yamauchi-future-gran-turismo-sounds-20251228/ |

> **Nota:** O formato ENGN (.es) do GT2 armazenava os parâmetros de pitch em
> `offset 0x32` (RPM Pitch) e `offset 0x38` (Sample Rate em Hz/10). O
> mecanismo é idêntico ao OCT+FNS do SCSP do Saturn — apenas com nomenclatura
> diferente. A tabela de lookup logarítmica do SRL (`Pcm::LogTable[]`) já
> encapsula o mesmo cálculo que Polyphony fazia manualmente.

---

## 11. Notas de Risco e Fallbacks

| Risco | Probabilidade | Mitigação |
|---|---|---|
| Sample loop com clique audível | Média | Testar ponto de loop com Audacity; usar fade-in de 1-2 ms |
| Pitch shift soa artificial em extremos | Alta | Limitar range de pitch a ±30%; adicionar 3° sample |
| `slPCMParmChange` não aplicando em tempo real | Baixa | Testar com pitch fixo primeiro; verificar chamada em Synchronize |
| 4 canais SRL insuficientes | Baixa | Usar acesso direto SCSP (32 slots) como SlaveDriver, se necessário |
| Custo de CPU na Slave | Baixa | Implementar versão inteira sem float; medir via HUD |
| Som de motor não sincroniza com marcha | Média | Garantir que `currentGear` vem do mesmo `frameState` que a física |
