# Plano de Acao - Implementacao de Audio

Documento derivado de [AUDIO_ENGINE_SOUND_PLAN.MD](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\AUDIO_ENGINE_SOUND_PLAN.MD) e validado contra o codigo atual do projeto em 2026-06-07.

## 1. Leitura do estado atual

O projeto ja tem a espinha dorsal correta para integrar audio sem mexer no fluxo principal do jogo:

- `Game::IAudioEvents` existe em [src/interfaces.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\interfaces.hpp)
- `SimpleAudioEvents` existe em [src/simple_audio_events.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_audio_events.hpp)
- O dispatch de audio ja acontece dentro de `SimulationTask::Do()` em [src/game_loop_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\game_loop_system.hpp)
- O SRL ja oferece:
  - `WaveSound`
  - `PlayOnChannel`
  - `IsChannelFree`
  - `StopSound`
  - `SetVolumePan`
  - macros de pitch `PCM_CALC_OCT`, `PCM_CALC_FNS`, `PCM_SET_PITCH_WORD`
  em [../../saturnringlib/srl_sound.hpp](c:\saturn\SaturnRingLib-main\saturnringlib\srl_sound.hpp)

## 2. Ajustes necessarios no plano original

O documento original esta bom como direcao, mas precisa destes ajustes para bater com o runtime real:

### 2.1 Onde o audio roda

No documento original, o audio e tratado como "apos render submission". No codigo real, o audio roda dentro da simulacao:

- ordem atual:
  - `gameplayTick->Tick`
  - `carPhysics->Step`
  - `audioEvents->OnFrame`

Isso e bom. O sistema de audio deve continuar puramente derivado do `GameplayFrameState`, sem acessar renderer, HUD ou CD em runtime.

### 2.2 Dados que ja existem no `GameplayFrameState`

Ja existem:

- `throttle`
- `braking`
- `wheelsSpinning`
- `debugEngineRpm`
- `debugGear`
- `debugSpeedKmh`
- `groundSurfaceType`
- `groundFamilyId`
- `debugWallHit`

Logo, a Fase 1 nao precisa inventar um modelo de RPM se `debugEngineRpm` e `debugGear` ja estiverem coerentes. O caminho mais barato e usar esses campos primeiro.

### 2.3 Dados que ainda faltam para o plano completo

Nao encontrei hoje no `GameplayFrameState`:

- `lateralG`
- `onKerb`

Esses dois nao devem bloquear a implementacao inicial. Devem entrar numa fase posterior.

### 2.4 Risco principal

O maior risco nao e CPU. E fluxo de assets + Sound RAM:

- formatos WAV aceitos pelo SRL
- tamanho real em Sound RAM
- estabilidade ao carregar tudo antes da corrida

## 3. Estrategia recomendada

Implementar em 3 camadas, do menor risco para o maior valor:

1. infraestrutura de audio PCM com 1 loop de motor
2. motor dinamico com pitch em tempo real
3. expansao para pneus, cambio e ambiencia

Nao recomendo entrar direto em crossfade de 2 samples + multiple one-shots + CDDA no mesmo passo. Isso aumenta superficie de falha sem necessidade.

## 4. Plano de implementacao

### Fase A - Infraestrutura minima de audio

Objetivo: provar que o jogo carrega WAV, toca loop, atualiza pitch e nao quebra o runtime.

Entregaveis:

- novo arquivo `src/car_audio_system.hpp`
- implementacao `CarAudioSystem : public Game::IAudioEvents`
- carregar 1 sample de motor via `WaveSound`
- tocar no canal PCM 0
- atualizar `Pcm::Channels[0].pitch` frame a frame
- substituir `SimpleAudioEvents` em [src/main.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\main.cxx)

Decisoes:

- usar apenas 1 sample de motor nesta fase
- sem pneus
- sem cambio
- sem ambiencia
- sem CDDA

Fonte de dados do audio:

- `debugEngineRpm` como principal
- fallback para `debugSpeedKmh`

Criterio de aceite:

- o jogo sobe
- o sample toca em loop
- o pitch sobe e desce com aceleracao/freio
- nao ha alocacao por frame

### Fase B - Telemetria e validacao

Objetivo: tornar o sistema observavel antes de aumentar complexidade.

Entregaveis:

- snapshot de audio em `CarAudioSystem`
- overlay com:
  - RPM usado
  - canal ativo
  - pitch word
  - volume

Criterio de aceite:

- conseguimos validar no overlay se o problema e no dado de gameplay ou no audio

### Fase C - Motor GT2 simplificado

Objetivo: sair do sample unico e ir para 2 loops de motor.

Entregaveis:

- `ENGINE_LO.WAV`
- `ENGINE_HI.WAV`
- logica de selecao por RPM
- troca de sample com transicao simples

Recomendacao pragmatica:

- primeiro fazer troca dura com pequeno mute de seguranca
- so depois tentar crossfade

Motivo:

- o SRL exposto aqui tem 4 canais de alto nivel
- crossfade ocupa mais estado e mais chance de glitch
- a troca dura e suficiente para validar range de RPM e assets

Criterio de aceite:

- sample baixo domina RPM baixo
- sample alto domina RPM alto
- nao ha clique evidente na transicao

### Fase D - Cambio

Objetivo: adicionar feedback de subida/descida de marcha.

Entregaveis:

- `SHIFT_UP.WAV`
- `SHIFT_DN.WAV`
- usar `debugGear` para detectar mudanca de marcha
- tocar one-shot no canal 2

Criterio de aceite:

- som toca uma vez por mudanca real
- nao repete continuamente com marcha estavel

### Fase E - Pneus

Objetivo: adicionar o primeiro comportamento de atrito sem depender de novos campos caros.

Entregaveis:

- `TIRE_SLD.WAV`
- heuristica inicial baseada em:
  - `wheelsSpinning`
  - `braking`
  - `debugSpeedKmh`
  - opcional: `debugYawRateDeg` e `debugSteerDeg`

Observacao:

- nao esperar `lateralG` nesta fase
- usar uma heuristica simples e barata

Exemplo de heuristica inicial:

- ativar skid se:
  - `debugSpeedKmh > limiar`
  - e (`wheelsSpinning` ou `braking`)
  - e `abs(debugYawRateDeg)` acima de limiar ou steer sob freio

Criterio de aceite:

- derrapagem aparece em frenagem forte e curvas mais agressivas
- nao fica ligada o tempo todo

### Fase F - Kerb e superficie

Objetivo: usar os dados de pista ja existentes.

Entregaveis:

- mapear `groundSurfaceType`
- tocar `TIRE_KRB.WAV` ou vibracao curta quando entrar em familia/tipo de superficie especifico

Dependencia:

- definir quais `groundSurfaceType`/`familyId` representam kerb na pista

Criterio de aceite:

- trigger ocorre em pontos de kerb reais
- nao dispara em asfalto comum

### Fase G - Ambiencia

Objetivo: adicionar som constante de pista sem contaminar a fase critica do motor.

Entregaveis:

- `CROWD.WAV` no canal 3
- volume fixo inicialmente

Criterio de aceite:

- loop estavel
- sem impactar motor/cambio/pneu

### Fase H - CDDA

Objetivo: musica de fundo por faixa de CD.

Entregaveis:

- tocar CDDA no boot, menu ou corrida
- pausa/retomada

Importante:

- esta fase deve vir por ultimo
- primeiro estabilizar PCM

## 5. Mudancas de codigo previstas

### 5.1 Novos arquivos

- `src/car_audio_system.hpp`

Opcional depois:

- `src/car_audio_tuning.hpp`

### 5.2 Arquivos a alterar

- [src/main.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\main.cxx)
  - trocar `SimpleAudioEvents` por `CarAudioSystem`
- [src/interfaces.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\interfaces.hpp)
  - so se precisarmos adicionar campos novos ao `GameplayFrameState`
- [src/game_loop_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\game_loop_system.hpp)
  - opcional, apenas para overlay/telemetria
- [src/simple_audio_events.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_audio_events.hpp)
  - manter temporariamente como fallback

## 6. Campos de gameplay: ordem correta de uso

Usar nesta ordem:

1. `debugEngineRpm`
2. `debugGear`
3. `debugSpeedKmh`
4. `throttle`
5. `braking`
6. `wheelsSpinning`
7. `groundSurfaceType`

Adicionar novos campos somente se o comportamento ficar insuficiente.

## 7. Recomendacao de canais

- canal 0: motor
- canal 1: pneu/skid
- canal 2: cambio/kerb one-shot
- canal 3: ambiencia

Isso bate com a limitacao de 4 canais exposta pelo SRL de alto nivel.

## 8. Ordem recomendada de execucao

1. carregar 1 WAV de motor e tocar em loop
2. variar pitch por `debugEngineRpm`
3. criar overlay de audio
4. trocar para 2 samples de motor
5. adicionar cambio
6. adicionar skid
7. adicionar kerb
8. adicionar crowd
9. adicionar CDDA

## 9. Criticos a verificar antes de implementar

- formato WAV aceito pelo SRL
- tamanho final dos assets em Sound RAM
- se o jogo muda de diretorio de CD antes de carregar audio
- se `debugEngineRpm` e `debugGear` estao sempre validos durante a corrida

## 10. Conclusao

O plano original e viavel, mas a implementacao correta para este projeto nao deve comecar pela versao completa.

O caminho mais seguro para o Saturn e:

- primeiro provar PCM loop + pitch
- depois estabilizar 2 samples de motor
- so entao expandir para pneus, cambio e CDDA

Isso minimiza risco de regressao, facilita debug e usa melhor a estrutura que o projeto ja possui.
