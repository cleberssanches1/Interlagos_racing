# Plano de Ação - Perfis de Áudio HQ/LQ com Flag

Documento criado em 2026-06-08 para introduzir dois perfis de áudio no projeto:

- `HQ`: qualidade atual, maior custo de RAM/dados
- `LQ`: custo reduzido, menor qualidade, voltado para estabilidade e budget

O objetivo é trocar o perfil sem alterar a lógica de gameplay, física ou eventos de áudio.

---

## 1. Objetivo

Permitir que o jogo rode com dois conjuntos de assets de áudio e uma flag de seleção:

- perfil `HQ` para teste de qualidade
- perfil `LQ` para teste de memória/estabilidade/performance

A seleção deve afetar apenas a camada de carregamento de assets, não a lógica de:

- RPM
- troca de marcha
- derrapagem
- channels
- `OnFrame()`

---

## 2. Estado atual

Hoje o projeto:

- usa `CarAudioSystem` em [src/car_audio_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\car_audio_system.hpp)
- carrega assets diretamente por nome fixo:
  - `ENGIDL.WAV`
  - `ENGLO.WAV`
  - `ENGHI.WAV`
  - `SHIUP.WAV`
  - `SHIDN.WAV`
  - `TIRSLD.WAV`
- usa `WaveSound`
- inicializa o áudio em [src/main.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\main.cxx)

Problema:

- a escolha do asset está acoplada à lógica do sistema
- não existe um perfil de build dedicado para áudio barato
- qualquer experimento de qualidade/memória exige renomear arquivos ou editar código

---

## 3. Estratégia recomendada

### 3.1 Fazer em duas camadas

Camada 1: seleção de perfil

- definir uma flag única de build
- centralizar a resolução do nome/path do asset

Camada 2: abstração do clip

- manter `WaveSound` como padrão no primeiro passo
- preparar a estrutura para aceitar `RawPcm` no futuro sem reescrever `CarAudioSystem`

### 3.2 Ordem pragmática

Implementação inicial:

1. `HQ` e `LQ` ambos em `WAV`
2. troca via flag de compile-time
3. mesma lógica de áudio, somente arquivos diferentes

Evolução posterior:

4. `LQ` opcional migrar para `RawPcm`
5. manter `HQ` em `WaveSound`

Isso reduz risco agora e preserva um caminho de otimização real depois.

---

## 4. Flag proposta

### 4.1 Nome da flag

Usar uma flag numérica simples:

```c
-DAUDIO_PROFILE=0
```

Semântica:

- `0` = `HQ`
- `1` = `LQ`
- `2` = `LQ_RAW` no futuro

### 4.2 Onde declarar

Arquivo:

- [makefile](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\makefile)

Proposta:

```make
AUDIO_PROFILE ?= 0
SRL_CUSTOM_CCFLAGS = ... -DAUDIO_PROFILE=$(AUDIO_PROFILE)
```

Isso permite:

- build normal: `AUDIO_PROFILE=0`
- build leve: `AUDIO_PROFILE=1`

Exemplo:

```bat
make AUDIO_PROFILE=1
```

ou adaptar `compile.bat` para um alias como:

```bat
compile_lq.bat
```

---

## 5. Organização dos assets

### 5.1 Opção recomendada

Usar sufixo de arquivo, sem mudar diretórios:

- `ENGIDL.WAV`
- `ENGIDL_LQ.WAV`
- `ENGLO.WAV`
- `ENGLO_LQ.WAV`
- `ENGHI.WAV`
- `ENGHI_LQ.WAV`
- `SHIUP.WAV`
- `SHIUP_LQ.WAV`
- `SHIDN.WAV`
- `SHIDN_LQ.WAV`
- `TIRSLD.WAV`
- `TIRSLD_LQ.WAV`

Vantagens:

- não exige reestruturação do CD layout
- facilita A/B rápido
- evita quebrar caminhos já usados em outros pontos

### 5.2 Alternativa

Pastas separadas:

- `cd/data/audio_hq/...`
- `cd/data/audio_lq/...`

Não recomendo agora. É mais limpo conceitualmente, mas adiciona risco no acesso por path e na montagem do disco.

---

## 6. Mudanças de código

### 6.1 Criar enum de perfil

Arquivo sugerido:

- `src/car_audio_profile.hpp`

Conteúdo esperado:

- enum com os perfis suportados
- helper compile-time para perfil ativo

Exemplo conceitual:

```cpp
enum class CarAudioProfile : uint8_t
{
    Hq = 0,
    Lq = 1,
    LqRaw = 2
};
```

### 6.2 Centralizar resolução de nomes

Arquivo alvo:

- [src/car_audio_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\car_audio_system.hpp)

Adicionar uma função única de resolução:

```cpp
static const char* ResolveAssetName(AudioCue cue);
```

Onde `AudioCue` representa:

- EngineIdle
- EngineLow
- EngineHigh
- ShiftUp
- ShiftDown
- TireSkid

Exemplo de comportamento:

- `HQ` -> `ENGLO.WAV`
- `LQ` -> `ENGLO_LQ.WAV`

Com isso, o carregamento deixa de ter strings hardcoded espalhadas.

### 6.3 Isolar a carga do clip

Ainda em `CarAudioSystem`, criar um helper:

```cpp
static SRL::Sound::Pcm::WaveSound* TryLoadWaveCue(AudioCue cue);
```

No futuro, esse helper poderá virar:

```cpp
static AudioClipHandle TryLoadCue(AudioCue cue);
```

e esconder se o backend é `WaveSound` ou `RawPcm`.

### 6.4 Não misturar perfil com lógica de engine

Os métodos abaixo não devem conhecer `HQ` ou `LQ`:

- `TickEngine()`
- `TickGearShift()`
- `TickTire()`
- `TickCrowd()`
- `ComputeEnginePitchWord()`

Eles devem operar apenas sobre clips já carregados.

Esse ponto é importante para manter o comportamento estável e respeitar separação de responsabilidades.

---

## 7. Caminho para `RawPcm` sem reescrever tudo

### 7.1 Por que não migrar direto

Migrar agora tudo para `RawPcm` aumenta risco em:

- pipeline de conversão
- metadata de sample rate
- escolha de mono/stereo
- pad/clamp de tamanho
- regressão no boot

### 7.2 Caminho seguro

Primeiro passo:

- `HQ`: `WaveSound`
- `LQ`: `WaveSound` com arquivos reduzidos

Segundo passo opcional:

- `LQ_RAW`: `RawPcm`

Para isso, criar uma pequena tabela por cue:

- filename
- tipo de backend
- channels
- bit depth
- sample rate

Exemplo conceitual:

```cpp
struct AudioAssetSpec
{
    const char* filename;
    uint16_t sampleRate;
    uint8_t channels;
    uint8_t bitDepth;
    bool useRawPcm;
};
```

Assim, a lógica de seleção de perfil continua em um único lugar.

---

## 8. Especificação inicial dos perfis

### 8.1 Perfil HQ

Manter próximo ao atual:

- motor: `16-bit mono 22050 Hz`
- shift: `16-bit mono 22050 Hz` ou `11025 Hz`
- skid: `16-bit mono 11025 Hz` ou `22050 Hz`

### 8.2 Perfil LQ

Redução com risco controlado:

- `ENGIDL/ENGLO/ENGHI`: `8-bit mono 22050 Hz`
- `SHIUP/SHIDN`: `8-bit mono 11025 Hz`
- `TIRSLD`: `8-bit mono 11025 Hz`

Essa é a melhor primeira comparação.

Não recomendo começar com `8000 Hz` nos loops do motor.

---

## 9. Plano de implementação

### Fase 1 - Infraestrutura de perfil

Arquivos:

- [makefile](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\makefile)
- novo `src/car_audio_profile.hpp`
- [src/car_audio_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\car_audio_system.hpp)

Ações:

- adicionar `AUDIO_PROFILE`
- criar enum/helper de perfil
- substituir nomes fixos por resolução centralizada

Resultado esperado:

- mesma ROM continua funcionando com `AUDIO_PROFILE=0`
- `AUDIO_PROFILE=1` apenas troca os arquivos carregados

### Fase 2 - Assets duplicados HQ/LQ

Arquivos:

- `cd/data/*.WAV`

Ações:

- gerar versões `_LQ`
- manter os nomes HQ atuais intactos

Resultado esperado:

- comparação A/B sem tocar na lógica

### Fase 3 - Overlay de validação

Arquivo alvo:

- HUD/telemetria existente

Ações:

- mostrar perfil ativo na tela
- opcionalmente mostrar tamanho agregado dos clips carregados

Resultado esperado:

- evitar dúvida sobre qual perfil está rodando

### Fase 4 - Backend `RawPcm` opcional

Arquivos:

- `src/car_audio_system.hpp`
- possível novo `src/car_audio_clip.hpp`
- scripts de conversão em `tools/`

Ações:

- criar tabela de assets
- suportar `WaveSound` e `RawPcm`
- ativar apenas para `AUDIO_PROFILE=2`

Resultado esperado:

- terceiro perfil ainda mais barato, sem quebrar HQ/LQ

---

## 10. Critérios de aceite

O trabalho estará correto quando:

- o build compilar com `AUDIO_PROFILE=0` e `AUDIO_PROFILE=1`
- ambos os perfis iniciarem sem assert de memória
- `CarAudioSystem` não precisar de `if` espalhado dentro da lógica de RPM
- o perfil ativo puder ser identificado em log ou overlay
- trocar de perfil não alterar comportamento de gameplay

---

## 11. Riscos e contenções

### Risco 1 - Arquivos LQ com nomes inconsistentes

Contenção:

- padronizar sufixo `_LQ`
- centralizar todos os nomes em uma única tabela

### Risco 2 - Misturar perfil com lógica do carro

Contenção:

- nenhuma decisão de perfil dentro de `TickEngine`, `TickGearShift` ou `TickTire`
- perfil só decide quais assets carregar

### Risco 3 - Queda excessiva de qualidade

Contenção:

- primeiro perfil LQ ainda em `22050 Hz` para o motor
- degradar agressivamente apenas `shift` e `skid`

### Risco 4 - Crescimento de código acoplado

Contenção:

- criar `AudioCue`
- criar `ResolveAssetName()`
- preparar backend abstrato antes de introduzir `RawPcm`

---

## 12. Recomendação final

A melhor abordagem para agora é:

1. implementar `HQ` e `LQ` ambos com `WaveSound`
2. trocar por `AUDIO_PROFILE`
3. usar arquivos `_LQ`
4. validar memória, boot e qualidade
5. só depois considerar `RawPcm`

Esse caminho entrega resultado rápido, reduz risco e não contamina a lógica do carro com decisões de formato.

---

## 13. Próximo passo sugerido

Implementar a Fase 1 e a Fase 2:

- adicionar a flag no `makefile`
- criar a resolução centralizada dos assets
- preparar o carregamento para `HQ/LQ`
- sem ainda migrar para `RawPcm`

