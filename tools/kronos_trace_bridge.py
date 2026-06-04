#!/usr/bin/env python3
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC_DUMP = b"KTRC"
MAGIC_STREAM = b"KTS2"
EVENT_SIZE = 32
EVENT_STRUCT = struct.Struct("<QIHBB16s")

TYPE_NAMES = {
    0: "FRAME_BEGIN",
    1: "FRAME_END",
    2: "SH2",
    3: "SCU",
    4: "VDP1",
    5: "WATCH",
    6: "INPUT",
    7: "PERF",
}


def decode_payload(ev_type: int, payload: bytes):
    payload = payload.ljust(16, b"\x00")
    if ev_type in (0, 1):
        frame_id, scanline, cpu_cycles, reserved = struct.unpack("<IIII", payload)
        return {
            "frame_id": frame_id,
            "scanline": scanline,
            "cpu_cycles": cpu_cycles,
            "reserved": reserved,
        }
    if ev_type == 2:
        cpu_id, event_id, flags, pc, data0, data1 = struct.unpack("<BBHIII", payload)
        return {
            "cpu_id": cpu_id,
            "event_id": event_id,
            "flags": flags,
            "pc": pc,
            "data0": data0,
            "data1": data1,
        }
    if ev_type == 3:
        command, vector, data0, data1, data2 = struct.unpack("<HHIII", payload)
        return {
            "command": command,
            "vector": vector,
            "data0": data0,
            "data1": data1,
            "data2": data2,
        }
    if ev_type == 4:
        command, flags, command_addr, data0, data1 = struct.unpack("<HHIII", payload)
        return {
            "command": command,
            "flags": flags,
            "command_addr": command_addr,
            "data0": data0,
            "data1": data1,
        }
    if ev_type == 5:
        watch_id, access, access_size, address, value, extra = struct.unpack("<HBBIII", payload)
        return {
            "watch_id": watch_id,
            "access": access,
            "access_size": access_size,
            "address": address,
            "value": value,
            "extra": extra,
        }
    if ev_type == 6:
        port, device, buttons, value, frame_id, reserved = struct.unpack("<BBHIII", payload)
        return {
            "port": port,
            "device": device,
            "buttons": buttons,
            "value": value,
            "frame_id": frame_id,
            "reserved": reserved,
        }
    if ev_type == 7:
        tag, scope, start_cycles, end_cycles, delta_cycles = struct.unpack("<HHIII", payload)
        return {
            "tag": tag,
            "scope": scope,
            "start_cycles": start_cycles,
            "end_cycles": end_cycles,
            "delta_cycles": delta_cycles,
        }
    return {"raw_hex": payload.hex()}


def decode_event_blob(blob: bytes):
    timestamp, sequence, ev_type, level, payload_size, payload = EVENT_STRUCT.unpack(blob)
    payload = payload[: min(payload_size, 16)]
    return {
        "timestamp": timestamp,
        "sequence": sequence,
        "type": TYPE_NAMES.get(ev_type, f"UNKNOWN_{ev_type}"),
        "type_id": ev_type,
        "level": level,
        "payload_size": payload_size,
        "payload": decode_payload(ev_type, payload),
    }


def parse_ktrace(path: Path):
    with path.open("rb") as f:
        magic = f.read(4)
        if magic != MAGIC_DUMP:
            raise ValueError(f"Arquivo invalido: magic {magic!r}")
        (count,) = struct.unpack("<I", f.read(4))
        for _ in range(count):
            blob = f.read(EVENT_SIZE)
            if len(blob) < EVENT_SIZE:
                break
            yield decode_event_blob(blob)


def open_named_pipe_server(pipe_name: str):
    if sys.platform != "win32":
        raise RuntimeError("Modo --pipe requer Windows")
    import ctypes
    from ctypes import wintypes

    PIPE_ACCESS_INBOUND = 0x00000001
    PIPE_TYPE_BYTE = 0x00000000
    PIPE_READMODE_BYTE = 0x00000000
    PIPE_WAIT = 0x00000000
    PIPE_UNLIMITED_INSTANCES = 255
    INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    CreateNamedPipeA = kernel32.CreateNamedPipeA
    CreateNamedPipeA.argtypes = [
        wintypes.LPCSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
    ]
    CreateNamedPipeA.restype = wintypes.HANDLE

    ConnectNamedPipe = kernel32.ConnectNamedPipe
    ConnectNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
    ConnectNamedPipe.restype = wintypes.BOOL

    h = CreateNamedPipeA(
        pipe_name.encode("ascii"),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        0,
        65536,
        0,
        None,
    )
    if h == INVALID_HANDLE_VALUE:
        raise OSError(f"CreateNamedPipeA falhou ({ctypes.get_last_error()})")
    if not ConnectNamedPipe(h, None):
        err = ctypes.get_last_error()
        if err != 535:  # ERROR_PIPE_CONNECTED
            raise OSError(f"ConnectNamedPipe falhou ({err})")
    return h


def read_pipe_bytes(pipe_handle, nbytes: int):
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    ReadFile = kernel32.ReadFile
    ReadFile.argtypes = [
        wintypes.HANDLE,
        wintypes.LPVOID,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID,
    ]
    ReadFile.restype = wintypes.BOOL

    read_total = bytearray()
    while len(read_total) < nbytes:
        chunk = nbytes - len(read_total)
        buf = (ctypes.c_ubyte * chunk)()
        read_now = wintypes.DWORD(0)
        ok = ReadFile(pipe_handle, ctypes.byref(buf), chunk, ctypes.byref(read_now), None)
        if not ok:
            err = ctypes.get_last_error()
            raise OSError(f"ReadFile falhou ({err})")
        if read_now.value == 0:
            break
        read_total.extend(bytes(buf[: read_now.value]))
    return bytes(read_total)


def stream_pipe(pipe_name: str, out_path: Path, max_events: int):
    print(f"Aguardando pipe: {pipe_name}")
    pipe = open_named_pipe_server(pipe_name)
    print("Pipe conectada.")

    with out_path.open("w", encoding="utf-8") as out:
        magic = read_pipe_bytes(pipe, 4)
        if magic != MAGIC_STREAM:
            raise RuntimeError(f"Magic de stream invalido: {magic!r}")
        emitted = 0
        while True:
            blob = read_pipe_bytes(pipe, EVENT_SIZE)
            if len(blob) < EVENT_SIZE:
                break
            ev = decode_event_blob(blob)
            out.write(json.dumps(ev, ensure_ascii=True) + "\n")
            emitted += 1
            if max_events > 0 and emitted >= max_events:
                break
    print(f"JSONL gerado: {out_path}")


def main():
    ap = argparse.ArgumentParser(description="Bridge de trace Kronos (.ktrace ou pipe em tempo real)")
    ap.add_argument("input", nargs="?", type=Path, help="arquivo .ktrace (modo offline)")
    ap.add_argument("-o", "--output", type=Path, default=None, help="arquivo .jsonl de saida")
    ap.add_argument("--pipe", type=str, default=None, help=r"named pipe Windows (ex: \\.\pipe\kronos_trace)")
    ap.add_argument("--max-events", type=int, default=0, help="limite de eventos no modo --pipe (0 = ilimitado)")
    args = ap.parse_args()

    if args.pipe:
        out = args.output or Path("kronos_pipe_trace.jsonl")
        stream_pipe(args.pipe, out, args.max_events)
        return

    if args.input is None:
        raise SystemExit("Informe um arquivo .ktrace ou use --pipe")
    out = args.output or args.input.with_suffix(".jsonl")
    with out.open("w", encoding="utf-8") as f:
        for ev in parse_ktrace(args.input):
            f.write(json.dumps(ev, ensure_ascii=True) + "\n")
    print(f"JSONL gerado: {out}")


if __name__ == "__main__":
    main()