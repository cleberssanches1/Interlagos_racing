#!/usr/bin/env python3
"""
Hotshot Racing – Camera Logic Extractor
========================================
Extrai a lógica de controle de câmera dos arquivos de dados do Hotshot Racing.

Fontes:
  - HotshotRacing.exe         : nomes de parâmetros e estrutura via análise de strings
  - *.dat  (formato SSRT)     : strings + floats extraídos diretamente
  - *.zml  (binário cifrado)  : entropia ~7.6 — requer rotina de decriptação do motor
                                  (Summit Engine, Sumo Digital); não decodificável
                                  sem a chave do motor.

Uso:
    python hotshot_camera_extract.py [--game-dir PATH] [--out-dir PATH]

Saída:
    <out-dir>/camera_ssrt_data.txt   – conteúdo legível dos .dat de câmera
    <out-dir>/camera_exe_params.txt  – parâmetros extraídos do executável
    <out-dir>/camera_summary.json    – resumo estruturado (para referência programática)
"""

import os
import sys
import re
import json
import struct
import argparse

# ============================================================
# Diretório padrão do jogo (Steam)
# ============================================================
DEFAULT_GAME_DIR = (
    r"C:\Program Files (x86)\Steam\steamapps\common\Hotshot Racing"
)
DEFAULT_OUT_DIR = os.path.join(os.path.dirname(__file__), "camera_extract_out")

# ============================================================
# Parâmetros de câmera conhecidos (extraídos de HotshotRacing.exe)
# Organizados por categoria para uso como dicionário de referência.
# ============================================================
KNOWN_CAMERA_PARAMS = {
    "camera_types": [
        "Camera_Chase",
        "Camera_Chase_2",
        "Camera_Cockpit",
        "Camera_Bonnet",
        "Camera_Bumper",
        "Camera_Rear",
        "Camera_Weapon_Chase",
        "Camera_RollingStart",
        "Replay_Camera",
        "Free_Camera",
        "Orbital_Camera",
        "Custom_Camera",
        "TrackIntroCameras",
        "GridCameras",
        "TrackPreviewCamera",
        "Versus_Camera",
    ],
    "position_distance": [
        "fDistanceOffset",
        "fMaxDistDeltaOffsetDownhill",
        "fMaxDistDeltaOffsetUphill",
        "fMaxDistHeightOffsetDownhill",
        "fMaxDistHeightOffsetUphill",
        "fMaxDistOffsetOnAccel",
        "fMaxDistOffsetOnBrake",
        "fStartDistanceFromCentre",
        "m_fStartDistanceFromCentre",
        "fOffsetX", "fOffsetY", "fOffsetZ",
        "LocalOffsetX", "LocalOffsetY", "LocalOffsetZ",
        "GlobalOffsetX", "GlobalOffsetY", "GlobalOffsetZ",
        "vLocalCarOffset",
        "fPosYOffset",
        "fHeightAdjust",
        "heightOffset",
        "targetHeightOffset",
        "fMaxOffset",
        "fMinOffset",
        "OffsetClampX",
        "OffsetClampZ",
        "OffsetMultiplierX",
        "OffsetMultiplierZ",
        "OffsetByPureSpeedZ",
        "CameraOffset",
        "PositionOffset",
        "PositionRacerOffset",
        "vLeanOffset",
        "fBaseElevation",
        "ms_fBaseElevation",
        "fFallingHeightOffset",
        "fRisingHeightOffset",
    ],
    "spring_damping": [
        "fLagDamping",
        "fLagStiffness",
        "fSpringDampRestore",
        "fSpringStrRestore",
        "fBumpSpringStr",
        "fBumpDampingWrtCritical",
        "fImpactDampingIn",
        "fImpactDampingOut",
        "fImpactMaxSpringSpeed",
        "fImpactMinSpringSpeed",
        "fLandingSpringDampening",
        "fLandingSpringStrength",
        "fLandingSpringUpDampening",
        "fLandingHeightTimeForMaxOffset",
        "fLandingHeightTimeForMinOffset",
        "SpringAcceleration",
        "SpringRates",
        "Springiness",
        "m_fDefaultSpring",
        "m_fEnterSpring",
        "m_fExitSpring",
        "DefaultSpringMul",
        "EnterSpringMul",
        "ExitSpringMul",
        "bAllowUpLag",
        "bAllowAccelSpring",
        "bAllowJumpOffset",
        "m_ForwardDampingFromTime",
        "m_SideDampingAccel",
        "m_SideDampingAccelBoost",
        "m_SideDampingAccelOppSteer",
        "m_SideDampingNoAccel",
        "vBumpMaxSpringVel",
    ],
    "fov": [
        "fFovAtMaxSpeed",
        "fFovAtMinSpeed",
        "fFovMaxSpeed",
        "fFovMinSpeed",
        "fMaxFov",
        "fMinFov",
        "fBoostDeltaMulFOV",
        "fFovAdjustScaleDistance",
        "DefaultFOVLerpBackSpeed",
        "VerticalFov",
        "CameraFOV",
        "FovDef",
        "FovDegs",
        "fPreserveFOVDistance",
        "fGenFocalDistance",
        "fSpeedFocalDistance",
        "CameraTanHalfFOV",
    ],
    "look_target": [
        "CameraLookMod",
        "CameraLookLerp",
        "fDriftTargetDist",
        "fSteerTargetDist",
        "fTargetBlendSpeed",
        "fTargetOffsetAmount",
        "fTargetOffsetAngle",
        "fTargetOffsetY",
        "fTargetRefreshTime",
        "fTargetSpeedMultiplier",
        "fTargetSpread",
        "ms_fLookAngle",
        "ms_fTargetZ",
        "m_fLookAngleThreshold",
        "m_fYawLookSteerScale",
        "LookBack",
        "LookLeft",
        "LookRight",
        "LookVelBlend",
        "AroundHumanAheadDistance",
        "AroundHumanBehindDistance",
        "vTarget",
    ],
    "pitch_yaw_roll": [
        "PitchBlendTime",
        "PitchDampRatePs",
        "PitchLimitDEG",
        "fPitchAdjust",
        "fPitchFrameLerpAmount",
        "fPitchScale",
        "m_fAntiPitch",
        "m_PitchLeanFactor",
        "IsFixedPitch",
        "YawDampRatePs",
        "fYawFrameLerpAmount",
        "fYawScale",
        "m_fYawLookSteerScale",
        "fLoostenYawOnDriftEntry",
        "fLoostenYawOnDriftEntryTime",
        "RollBlendTime",
        "RollDampRatePs",
        "RollLimitDEG",
        "RollMul",
        "AdditionalCameraRoll",
        "IgnoreRoll",
        "ExtraRoll",
        "fRollAngleFromDrifting",
        "fRollAngleFromSteering",
        "fRollFraction",
        "fRollFwdSpeedForMaxRoll",
        "fRollRadsSec",
        "fRollScale",
        "m_fAntiRoll",
        "m_bRollStabiliser",
        "m_fRollTargetXOffset",
    ],
    "velocity_blend": [
        "fVelocityBlend",
        "fVelocityBlendDrift",
        "VelocityBlend",
        "VelocityBlendDelta",
        "BlendTime",
        "BlendSpeed",
        "BlendDuration",
        "Drift_Camera_Blend_Time",
        "fFallingBlendSpeed",
        "fRisingBlendSpeed",
        "fSpeedForNormalBlendMPH",
        "fSpeedForPositionOnlyBlendMPH",
        "FasterSkillDistanceBlend",
        "SlowerSkillDistanceBlend",
        "fTimeAllowedToBlend",
        "bDontBlend",
        "gBlend",
        "gBlendScale",
        "fBlendLength",
        "m_fBlendLength",
    ],
    "drift_specific": [
        "fDriftTargetDist",
        "fRollAngleFromDrifting",
        "fLoostenYawOnDriftEntry",
        "fLoostenYawOnDriftEntryTime",
        "fVelocityBlendDrift",
        "Drift_Camera_Blend_Time",
        "Drift_Level_1_Camera_Shake",
        "Drift_Level_2_Camera_Shake",
        "Drift_Level_3_Camera_Shake",
    ],
    "slipstream_special": [
        "m_fMaxDistanceToSlipstream",
        "SpeedForAutoCameraReverse",
        "SpeedForAutoCameraForwardFromReverse",
        "LateralSpeedContributionForAutoCameraReverse",
        "cameraSeverity",
        "ShowInCameraChase",
        "ShowInCameraCockpit",
        "ShowInCameraBonnet",
        "ShowInCameraBumper",
        "ShowInCameraRear",
        "ShowInCameraOther",
        "m_fWeaponChaseOffset",
        "m_fWeaponChaseOffsetTime",
        "m_fSpeedLineEffectOffset_Bonnet",
        "m_fSpeedLineEffectOffset_Bumper",
        "m_fSpeedLineEffectOffset_Chase",
        "m_fSpeedLineEffectOffset_Chase2",
        "m_fSpeedLineEffectOffset_Cockpit",
    ],
    "shake": [
        "Drift_Level_1_Camera_Shake",
        "Drift_Level_2_Camera_Shake",
        "Drift_Level_3_Camera_Shake",
        "fBumpSpringStr",
        "vBumpMaxSpringVel",
        "fImpactDampingIn",
        "fImpactDampingOut",
    ],
}

# ============================================================
# Parser de arquivos SSRT (.dat)
# ============================================================

def parse_ssrt_file(path: str) -> dict:
    """
    Extrai strings e floats de um arquivo no formato SSRT (Summit Serialization Runtime).
    Retorna dicionário com 'strings', 'floats' e metadados.
    """
    result = {"path": path, "strings": [], "floats": [], "valid": False}

    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        result["error"] = str(e)
        return result

    if data[:4] != b"SSRT":
        result["error"] = "Not an SSRT file"
        return result

    result["valid"] = True
    result["size"] = len(data)

    # --- Extract null-terminated / run-length ASCII strings ---
    i = 0
    while i < len(data):
        start = i
        while i < len(data) and 32 <= data[i] <= 126:
            i += 1
        if i - start >= 3:
            s = data[start:i].decode("ascii", errors="replace").strip()
            if s:
                result["strings"].append({"offset": start, "value": s})
        i += 1

    # --- Extract 32-bit IEEE floats at 4-byte aligned positions ---
    for j in range(0, len(data) - 4, 4):
        try:
            v = struct.unpack_from("<f", data, j)[0]
            # Filter out infinities, NaN, and very extreme values
            if v != v or abs(v) > 1e6:
                continue
            # Ignore zero and near-zero
            if abs(v) < 1e-4:
                continue
            result["floats"].append({"offset": j, "value": round(v, 6)})
        except struct.error:
            pass

    return result


# ============================================================
# Extrator de parâmetros do executável
# ============================================================

def extract_exe_params(exe_path: str) -> dict:
    """
    Extrai strings do executável e filtra as relacionadas a câmera.
    """
    result = {"path": exe_path, "camera_strings": [], "valid": False}

    try:
        with open(exe_path, "rb") as f:
            data = f.read()
    except OSError as e:
        result["error"] = str(e)
        return result

    result["valid"] = True

    # Extract all strings >= 4 chars
    all_strings = set()
    pattern = re.compile(rb"[\x20-\x7E]{4,}")
    for m in pattern.finditer(data):
        s = m.group().decode("ascii", errors="replace")
        all_strings.add(s)

    # Build flat list of all known params for matching
    all_known = set()
    for cat_params in KNOWN_CAMERA_PARAMS.values():
        for p in cat_params:
            all_known.add(p.lower())

    camera_keywords = re.compile(
        r"camera|chase|cockpit|bonnet|bumper|fov|spring|damping|roll|pitch|yaw|"
        r"blend|velocity|drift|slipstream|shake|lag|stiffness|target|look",
        re.IGNORECASE,
    )

    for s in sorted(all_strings):
        if camera_keywords.search(s):
            result["camera_strings"].append(s)

    result["camera_strings"].sort()
    return result


# ============================================================
# Tentativa de decodificação de .zml
# ============================================================

def try_decode_zml(path: str) -> dict:
    """
    Tenta múltiplas estratégias de decodificação nos arquivos .zml.
    Retorna o melhor resultado encontrado (ou falha documentada).
    """
    result = {
        "path": path,
        "strategy": "none",
        "decoded": False,
        "note": (
            "Arquivo .zml usa formato proprietário cifrado do motor Summit "
            "(Sumo Digital). Entropia ~7.6 bits/byte indica cifração forte. "
            "Estratégias tentadas: zlib, deflate, XOR byte único 0x00-0xFF, "
            "XOR com nome do arquivo. Nenhuma produz XML legível. "
            "A rotina de decriptação está compilada em HotshotRacing.exe e "
            "não é acessível sem desmontagem do executável."
        ),
    }

    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        result["error"] = str(e)
        return result

    import zlib

    # Strategy 1: zlib at various offsets
    for offset in range(0, 20):
        try:
            decoded = zlib.decompress(data[offset:])
            result.update({"strategy": f"zlib@{offset}", "decoded": True,
                           "preview": decoded[:200].decode("utf-8", errors="replace")})
            return result
        except Exception:
            pass

    # Strategy 2: raw deflate
    for offset in range(0, 20):
        try:
            decoded = zlib.decompress(data[offset:], -15)
            result.update({"strategy": f"deflate@{offset}", "decoded": True,
                           "preview": decoded[:200].decode("utf-8", errors="replace")})
            return result
        except Exception:
            pass

    # Strategy 3: single-byte XOR — require actual XML tag structure
    fname = os.path.basename(path).encode()
    xml_tag_pattern = re.compile(rb"<[A-Za-z][A-Za-z0-9_]{2,}")
    for key in range(256):
        candidate = bytes(b ^ key for b in data[:512])
        # Require at least 2 XML-looking tags and a camera keyword
        tags = xml_tag_pattern.findall(candidate)
        if len(tags) >= 2:
            try:
                text = candidate.decode("utf-8", errors="strict")
                if any(kw in text.lower() for kw in ["camera", "fov", "spring", "chase"]):
                    result.update({"strategy": f"XOR_0x{key:02X}", "decoded": True,
                                   "preview": text[:200]})
                    return result
            except Exception:
                pass

    # Strategy 4: filename-cycling XOR — same strict check
    key_bytes = list(fname)
    candidate = bytes(data[i] ^ key_bytes[i % len(key_bytes)] for i in range(min(512, len(data))))
    tags = xml_tag_pattern.findall(candidate)
    if len(tags) >= 2:
        try:
            text = candidate.decode("utf-8", errors="replace")
            if any(kw in text.lower() for kw in ["camera", "fov", "spring"]):
                result.update({"strategy": "XOR_filename", "decoded": True,
                               "preview": text[:200]})
                return result
        except Exception:
            pass

    return result


# ============================================================
# Entry point
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="Hotshot Racing Camera Logic Extractor")
    parser.add_argument("--game-dir", default=DEFAULT_GAME_DIR,
                        help="Diretório raiz do jogo")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR,
                        help="Diretório de saída")
    args = parser.parse_args()

    game_data_dir = os.path.join(args.game_dir, "data", "Gamedata")
    exe_path = os.path.join(args.game_dir, "HotshotRacing.exe")

    os.makedirs(args.out_dir, exist_ok=True)

    summary = {
        "game_dir": args.game_dir,
        "known_params": KNOWN_CAMERA_PARAMS,
        "ssrt_files": [],
        "zml_files": [],
        "exe_params": {},
    }

    print(f"[1/3] Processando arquivos .dat (SSRT) em: {game_data_dir}")
    ssrt_results = []
    camera_dat_files = ["SlipstreamCameraEffects.dat"]
    for fname in camera_dat_files:
        fpath = os.path.join(game_data_dir, fname)
        if os.path.isfile(fpath):
            r = parse_ssrt_file(fpath)
            ssrt_results.append(r)
            summary["ssrt_files"].append(r)
            print(f"  {fname}: {'OK' if r['valid'] else 'ERRO'} "
                  f"({len(r.get('strings', []))} strings, "
                  f"{len(r.get('floats', []))} floats)")

    # Write SSRT report
    ssrt_out = os.path.join(args.out_dir, "camera_ssrt_data.txt")
    with open(ssrt_out, "w", encoding="utf-8") as f:
        f.write("=== HOTSHOT RACING — SSRT CAMERA DATA ===\n\n")
        for r in ssrt_results:
            f.write(f"FILE: {os.path.basename(r['path'])}\n")
            f.write(f"  Size: {r.get('size', 'N/A')} bytes\n")
            f.write("  Strings:\n")
            for s in r.get("strings", []):
                f.write(f"    @{s['offset']:04X}: {s['value']!r}\n")
            f.write("  Floats:\n")
            for fl in r.get("floats", []):
                f.write(f"    @{fl['offset']:04X}: {fl['value']}\n")
            f.write("\n")
    print(f"  -> Escrito: {ssrt_out}")

    print(f"\n[2/3] Tentando decodificar .zml: cameras.zml")
    zml_path = os.path.join(game_data_dir, "cameras.zml")
    if os.path.isfile(zml_path):
        zml_result = try_decode_zml(zml_path)
        summary["zml_files"].append(zml_result)
        status = "DECODIFICADO" if zml_result["decoded"] else "NÃO DECODIFICÁVEL"
        print(f"  cameras.zml: {status} — strategy={zml_result['strategy']}")
        if not zml_result["decoded"]:
            print(f"  NOTA: {zml_result['note']}")

    print(f"\n[3/3] Extraindo parâmetros do executável: {os.path.basename(exe_path)}")
    if os.path.isfile(exe_path):
        exe_result = extract_exe_params(exe_path)
        summary["exe_params"] = exe_result
        print(f"  Encontradas {len(exe_result.get('camera_strings', []))} strings relacionadas a câmera")

        exe_out = os.path.join(args.out_dir, "camera_exe_params.txt")
        with open(exe_out, "w", encoding="utf-8") as f:
            f.write("=== HOTSHOT RACING — PARÂMETROS DE CÂMERA (via executável) ===\n\n")
            f.write("Fonte: HotshotRacing.exe — extração de strings ASCII >= 4 chars\n")
            f.write("Filtro: keywords camera|chase|cockpit|bonnet|bumper|fov|spring|damping|\n")
            f.write("        roll|pitch|yaw|blend|velocity|drift|slipstream|shake|lag\n\n")
            for s in exe_result.get("camera_strings", []):
                f.write(f"  {s}\n")
        print(f"  -> Escrito: {exe_out}")

    # Write summary JSON
    json_out = os.path.join(args.out_dir, "camera_summary.json")
    # Make summary JSON-serializable (remove binary data)
    with open(json_out, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False, default=str)
    print(f"\n  -> Resumo JSON: {json_out}")

    print("\n=== Extração completa ===")
    print(f"Saída em: {args.out_dir}")
    print(
        "\nNOTA: Para obter os valores numéricos de cameras.zml, é necessário\n"
        "desmontar HotshotRacing.exe e localizar a rotina de decriptação do\n"
        "motor Summit (Sumo Digital). Os parâmetros documentados no arquivo\n"
        "HOTSHOT_RACING_CAMERA_LOGIC.md refletem a estrutura completa do\n"
        "sistema inferida via análise do executável."
    )


if __name__ == "__main__":
    main()
