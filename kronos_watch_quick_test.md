# Teste Rapido de Watchpoints (Realtime)

## 1) Iniciar bridge (Terminal A)
python C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\kronos_trace_bridge.py --pipe \\.\pipe\kronos_trace -o C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\live_trace_min.jsonl

## 2) Configurar ambiente e abrir Kronos (Terminal B)
$env:KRONOS_TRACE_LEVEL="2"
$env:KRONOS_TRACE_WATCH_FILE="C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\trace_watch_min.yaml"
$env:KRONOS_TRACE_PIPE="\\.\pipe\kronos_trace"
$env:KRONOS_TRACE_OUT="C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\session_min.ktrace"

# Abra o Kronos normalmente com seu fluxo atual.

## 3) Cenario de validacao (60-90s)
- Carro parado por 2s
- Acelerar reto por 5s
- Curva esquerda 3s
- Curva direita 3s
- Freio e re por 3s

## 4) Checagem rapida do JSONL
PowerShell:
Get-Content C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\live_trace_min.jsonl -Tail 20

Contagem de eventos WATCH:
(Select-String -Path C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\live_trace_min.jsonl -Pattern '"type": "WATCH"').Count

## 5) Critérios de sucesso
- Arquivo JSONL crescendo durante a execução
- Eventos WATCH presentes para ids 100,101,104,105,106,107
- Sem queda perceptível de FPS no jogo

## 6) Se não houver eventos WATCH
- Confirmar que o bridge foi iniciado antes do Kronos
- Confirmar KRONOS_TRACE_LEVEL=2
- Confirmar KRONOS_TRACE_WATCH_FILE aponta para trace_watch_min.yaml
- Confirmar pipe: \\.\pipe\kronos_trace
