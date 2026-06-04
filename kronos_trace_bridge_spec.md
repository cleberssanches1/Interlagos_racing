# Kronos Trace Bridge Spec (Interlagos)

## Objetivo
Bridge local para consumir eventos de trace do Kronos e publicar simultaneamente em:
- named pipe (tempo real)
- arquivo binario `.ktrace` (arquivo canonico)
- JSONL opcional (debug/analise)

## Entradas e saidas
- Entrada offline: dump `.ktrace` com magic `KTRC`
- Entrada realtime: named pipe Windows com magic `KTS2` + eventos binarios de 32 bytes
- Saida bridge: JSONL (um evento por linha)

## Contrato minimo da bridge atual
1. Modo offline converte `.ktrace` para JSONL sem reordenar eventos.
2. Modo realtime recebe stream da named pipe e grava JSONL incremental.
3. Queda da pipe encerra sessao realtime sem corromper arquivo ja gravado.

## Formato `.ktrace`
- Header: `magic='KTRC'` + `count(u32 LE)`.
- Evento: struct fixa de 32 bytes.

## Formato JSONL
Um objeto por linha com campos base:
- `type` (ex: `KTRACE_EV_VDP1_CMD`)
- `frame`, `line`, `deciline`, `tick`
- `payload` (campos dependentes do tipo)

Exemplo:
```json
{"type":"KTRACE_EV_VDP1_CMD","frame":812,"line":144,"deciline":2,"tick":1559032,"payload":{"cmd_type":4,"cmd_addr":8192}}
```

## Pipeline implementado
1. Kronos escreve eventos no ring interno.
2. Opcionalmente faz stream realtime para pipe definida em `KRONOS_TRACE_PIPE`.
3. Bridge Python abre server da pipe e converte stream binaria para JSONL.
4. Em paralelo, dumps `.ktrace` podem ser convertidos via modo offline.

## Modos de execucao
- Offline: `python tools/kronos_trace_bridge.py sessao.ktrace`
- Realtime: `python tools/kronos_trace_bridge.py --pipe \\\\.\\pipe\\kronos_trace -o live.jsonl`

## Erros e resiliencia
- Backpressure na pipe: descartar apenas saida da pipe; manter `.ktrace`.
- Falha de escrita em JSONL: registrar alerta interno e continuar.
- Overflow do ring buffer: emitir evento `KTRACE_EV_ALERT` com contagem de perdas.
