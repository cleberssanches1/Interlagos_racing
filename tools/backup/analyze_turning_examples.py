#!/usr/bin/env python3
"""
analyze_turning_examples.py
============================
Faz duas coisas:

1. BUSCA nos projetos de exemplo como cada jogo calcula a curva (yaw/heading)
   e imprime os trechos de codigo relevantes com contexto.

2. SIMULA a fisica atual do Interlagos para mostrar frame-a-frame o que o
   modelo cinematico deveria produzir ao pressionar esquerda+B parado.

Uso:
    python tools/analyze_turning_examples.py
    python tools/analyze_turning_examples.py --simulate-only
    python tools/analyze_turning_examples.py --scan-only
"""

from __future__ import annotations
import argparse
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXAMPLES_ROOT = ROOT / "Projetos_Exemplos"
REPORT_DIR = pathlib.Path(__file__).resolve().parent / "reports"

# ---------------------------------------------------------------------------
# PARTE 1 – BUSCA NOS EXEMPLOS
# ---------------------------------------------------------------------------

# Grupos de padroes: nome, regex, descricao
SEARCH_GROUPS = [
    ("yaw_direto",
     re.compile(r"(?:yaw|heading|angulo)\s*[\+\-\*]?=\s*[^;]{0,100}", re.IGNORECASE),
     "Atualizacao direta de yaw/heading"),

    ("omega_formula",
     re.compile(r"(?:omega|yaw_rate|yawRate|angularVelocity)\s*[\+\-]?=\s*[^;]{0,100}", re.IGNORECASE),
     "Calculo de taxa de rotacao (omega/yaw rate)"),

    ("steer_to_yaw",
     re.compile(r"steer[A-Za-z_]*\s*\*\s*(?:speed|vel|v|dt|max)|"
                r"(?:speed|vel|v)\s*\*\s*steer[A-Za-z_]*\s*\*",
                re.IGNORECASE),
     "Steer * speed (formula bicicicleta/cinematica)"),

    ("sin_cos_posicao",
     re.compile(r"[xyz]\s*[\+\-]?=\s*[^;]*(?:sin|cos)\s*\([^)]*(?:yaw|heading|angle|psi|theta)[^)]*\)",
                re.IGNORECASE),
     "Posicao calculada com sin/cos do angulo"),

    ("alphafront_rear",
     re.compile(r"alpha[Ff]ront|alpha[Rr]ear|slip.?angle|vy\s*/\s*vx|vy\s*/\s*(?:speed|vel)",
                re.IGNORECASE),
     "Angulo de deslizamento (modelo de pneu)"),

    ("cinematic_arcade",
     re.compile(r"(?:yaw|heading)\s*[\+\-]?=\s*[^;]*steer[^;]*speed|"
                r"(?:yaw|heading)\s*[\+\-]?=\s*[^;]*input[^;]*",
                re.IGNORECASE),
     "Cinematica arcade: yaw += steer * algo"),
]

CODE_EXTENSIONS = {".c", ".cpp", ".cxx", ".cc", ".h", ".hpp"}

# Palavras que indicam que o arquivo e relevante para fisica de carro
RELEVANCE_KEYWORDS = ["steer", "yaw", "heading", "torque", "slip", "angular",
                      "omega", "turn", "curva", "bicycle", "kinematic"]


def is_relevant(text_lower: str) -> bool:
    return any(kw in text_lower for kw in RELEVANCE_KEYWORDS)


def extract_snippet(lines: list[str], line_idx: int, ctx: int = 3) -> str:
    start = max(0, line_idx - ctx)
    end = min(len(lines), line_idx + ctx + 1)
    parts = []
    for i in range(start, end):
        marker = ">>>" if i == line_idx else "   "
        parts.append(f"  {marker} {i+1:4d}: {lines[i].rstrip()}")
    return "\n".join(parts)


def scan_examples() -> dict[str, list[dict]]:
    """Retorna {projeto: [{file, group_name, group_desc, line_no, snippet}]}"""
    if not EXAMPLES_ROOT.exists():
        print(f"AVISO: Pasta de exemplos nao encontrada: {EXAMPLES_ROOT}")
        return {}

    results: dict[str, list[dict]] = {}

    for proj_dir in sorted(EXAMPLES_ROOT.iterdir()):
        if not proj_dir.is_dir():
            continue
        proj_name = proj_dir.name
        hits = []

        for fpath in proj_dir.rglob("*"):
            if fpath.suffix.lower() not in CODE_EXTENSIONS:
                continue
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception:
                continue
            if not is_relevant(text.lower()):
                continue

            lines = text.splitlines()
            for group_name, pattern, group_desc in SEARCH_GROUPS:
                seen_lines: set[int] = set()
                for i, line in enumerate(lines):
                    if pattern.search(line) and i not in seen_lines:
                        seen_lines.add(i)
                        hits.append({
                            "file": str(fpath.relative_to(EXAMPLES_ROOT)),
                            "group": group_name,
                            "desc": group_desc,
                            "line": i + 1,
                            "snippet": extract_snippet(lines, i),
                        })

        results[proj_name] = hits

    return results


def print_scan_report(results: dict[str, list[dict]]) -> None:
    sep = "=" * 78

    # Identifica abordagem por projeto
    approach_map: dict[str, set[str]] = {}
    for proj, hits in results.items():
        groups = {h["group"] for h in hits}
        if "cinematic_arcade" in groups or "yaw_direto" in groups:
            approach_map[proj] = {"CINEMATICA-DIRETA"}
        elif "alphafront_rear" in groups:
            approach_map[proj] = {"MODELO-SLIP-PNEU"}
        elif "steer_to_yaw" in groups:
            approach_map[proj] = {"CINEMATICA-BICICLETA"}
        else:
            approach_map[proj] = {"DESCONHECIDA"}

    print(f"\n{sep}")
    print("RESUMO DOS PROJETOS DE EXEMPLO")
    print(sep)
    for proj, approaches in sorted(approach_map.items()):
        nhits = len(results.get(proj, []))
        print(f"  {proj:30s}: {', '.join(approaches)}  ({nhits} ocorrencias)")

    for proj_name, hits in sorted(results.items()):
        if not hits:
            print(f"\n{sep}")
            print(f"PROJETO: {proj_name}  — nenhum trecho relevante encontrado")
            continue

        print(f"\n{sep}")
        print(f"PROJETO: {proj_name}  ({len(hits)} ocorrencias)")
        print(sep)

        # Agrupa por arquivo e grupo
        by_file: dict[str, list[dict]] = {}
        for h in hits:
            by_file.setdefault(h["file"], []).append(h)

        for fpath, fhits in sorted(by_file.items()):
            print(f"\n  ARQUIVO: {fpath}")
            by_group: dict[str, list[dict]] = {}
            for h in fhits:
                by_group.setdefault(h["group"], []).append(h)

            for group_name, ghits in by_group.items():
                group_desc = ghits[0]["desc"]
                print(f"\n    [{group_desc.upper()}] — {len(ghits)} ocorrencia(s)")
                for h in ghits[:3]:  # maximo 3 por grupo por arquivo
                    print(f"    --- linha {h['line']} ---")
                    print(h["snippet"])
                if len(ghits) > 3:
                    print(f"    ... e mais {len(ghits)-3} ocorrencias neste arquivo")


# ---------------------------------------------------------------------------
# PARTE 2 – SIMULACAO FRAME-A-FRAME DO MODELO ATUAL
# ---------------------------------------------------------------------------
# Replica o comportamento de IntegratePlanar + StepOnce do Interlagos
# em Python puro, para diagnosticar o que deveria acontecer ao pressionar
# esquerda+B com o carro parado.

class FixedPoint:
    """Aproxima logica 16.16 Fxp com float para simulacao."""
    SCALE = 65536.0

    @staticmethod
    def raw(v: float) -> float:
        return v

    @staticmethod
    def clamp(v: float, lo: float, hi: float) -> float:
        return max(lo, min(hi, v))

    @staticmethod
    def normalize_percent(p: int) -> float:
        clamped = max(-100, min(100, p))
        return clamped / 100.0

    @staticmethod
    def normalize_yaw(y: int) -> int:
        y = y % 360
        if y < 0:
            y += 360
        return y


# Constantes do carro (de car_physics_shared.hpp e car_system.hpp)
K_MAX_YAW_RATE      = 2.25    # deg/frame  (kMaxYawRateDegPerFrame)
K_KIN_THRESHOLD     = 2.5     # units/frame (kLaunchKinematicSpeedThreshold)
K_GEAR1_ACCEL       = 0.48    # units/frame
K_THROTTLE_STEP     = 8       # percent (kThrottleStep)
K_STEERING_MAX      = 100     # percent
K_BRAKE_DECEL       = 0.340   # units/frame (kBrakeDecelPerFrame)
K_MAX_FORWARD       = 7.125   # units/frame
K_TARGET_TOP_KMH    = 300.0
K_STEER_RESPONSE    = 0.20
K_MAX_STEER_DEG     = 12.0    # deg
K_DEG_TO_RAD        = math.pi / 180.0
K_RAD_TO_DEG        = 180.0 / math.pi
K_CA_FRONT          = 2.40
K_CA_REAR           = 2.80
K_WB_FRONT          = 0.95
K_WB_REAR           = 0.95
K_SLIP_DENOM_MIN    = 0.35
K_YAW_MOMENT_GAIN   = 0.4375
K_YAW_RATE_RESP     = 0.50
K_YAW_DAMPING       = 0.1875
K_LAT_CAP_BASE      = 2.50
K_LAT_DAMP          = 0.50
K_YAW_COUPLING      = 0.16
K_FWDSTEER_THRESH   = 0.25    # kForwardSteerLaunchSpeedThreshold
K_FWDSTEER_ACCEL    = 0.1875  # kForwardSteerLaunchAccelPerFrame

SPEED_TO_KMH = K_TARGET_TOP_KMH / K_MAX_FORWARD  # ~42.1 km/h per unit/frame


def simulate_launch(
        steer_cmd: int = -100,          # -100=esquerda completa, +100=direita
        initial_fwd_speed: float = 0.0, # velocidade inicial (positivo=frente)
        num_frames: int = 30,
        label: str = "Cenario") -> list[dict]:
    """Simula N frames de fisica com B+steer_cmd pressionados."""

    fwd_speed    = initial_fwd_speed
    lat_speed    = 0.0
    yaw_rate     = 0.0
    steer_deg    = 0.0
    yaw_accum    = 0.0   # acumulador sub-grau (em graus, nao raw)
    yaw_deg      = 0     # int
    throttle     = 0     # percentual (0-100)
    frames: list[dict] = []

    for frame in range(num_frames):
        # --- Input processing (replica Accelerate + TickCommandState) ---
        throttle = min(100, throttle + K_THROTTLE_STEP)
        steering = steer_cmd  # snap imediato (launchSteerSnap ativo a baixa vel)

        throttle_norm  = FixedPoint.normalize_percent(throttle)
        steer_norm     = FixedPoint.normalize_percent(steering)
        has_steer      = (steering != 0)
        has_fwd_intent = (throttle > 0)   # nao braking

        # --- Throttle / reverse transition ---
        was_reversing = (fwd_speed < 0.0)
        fwd_speed += throttle_norm * K_GEAR1_ACCEL
        if fwd_speed < 0.0:
            fwd_speed += K_BRAKE_DECEL   # extra boost na transicao

        if was_reversing:
            lat_speed = 0.0
            yaw_rate  = 0.0
            yaw_accum = 0.0

        # Aero drag (simplificado)
        fwd_speed -= abs(fwd_speed) * fwd_speed * 0.00134
        fwd_speed -= fwd_speed * 0.0033
        fwd_speed = FixedPoint.clamp(fwd_speed, -K_KIN_THRESHOLD, K_MAX_FORWARD)

        speed_abs = abs(fwd_speed)

        # --- Steer response ---
        fwd_steer_snap = (speed_abs < K_FWDSTEER_THRESH and has_fwd_intent)
        target_steer   = steer_norm * K_MAX_STEER_DEG
        if fwd_steer_snap:
            steer_deg = target_steer
        else:
            steer_deg += (target_steer - steer_deg) * K_STEER_RESPONSE

        # --- Kinematic ou slip model ---
        use_kinematic = has_fwd_intent and has_steer and (speed_abs < K_KIN_THRESHOLD)
        was_kinematic = use_kinematic

        if use_kinematic:
            lat_speed = 0.0
            yaw_rate  = -steer_norm * K_MAX_YAW_RATE  # negate: yaw crescendo = curva esquerda
        else:
            # Slip model (mesmo sinal sem inversao quando nao braking)
            steer_authority = 1.0 - (speed_abs / K_MAX_FORWARD) * 0.70
            steer_authority = max(0.20, steer_authority)
            steer_gate = FixedPoint.clamp((speed_abs - 0.25) / 0.75, 0.0, 1.0)
            if has_fwd_intent:
                steer_gate = max(steer_gate, 0.35)
            steer_eff_deg = steer_deg * steer_authority * steer_gate

            steer_eff_rad = steer_eff_deg * K_DEG_TO_RAD
            yaw_rad = yaw_rate * K_DEG_TO_RAD
            vx_abs  = max(speed_abs, K_SLIP_DENOM_MIN)

            vy_front = lat_speed + (yaw_rad * K_WB_FRONT)
            vy_rear  = lat_speed - (yaw_rad * K_WB_REAR)
            alpha_f  = steer_eff_rad - (vy_front / vx_abs)
            alpha_r  = -(vy_rear / vx_abs)

            fy_cap   = K_LAT_CAP_BASE
            fy_f     = FixedPoint.clamp(-K_CA_FRONT * alpha_f, -fy_cap, fy_cap)
            fy_r     = FixedPoint.clamp(-K_CA_REAR  * alpha_r, -fy_cap, fy_cap)

            lat_accel = fy_f + fy_r + fwd_speed * yaw_rad * K_YAW_COUPLING
            lat_speed += lat_accel
            lat_speed -= lat_speed * K_LAT_DAMP

            yaw_moment = fy_f * K_WB_FRONT - fy_r * K_WB_REAR
            target_yr  = (yaw_moment * K_YAW_MOMENT_GAIN) * K_RAD_TO_DEG
            yaw_rate  += (target_yr - yaw_rate) * K_YAW_RATE_RESP
            yaw_rate  -= yaw_rate * K_YAW_DAMPING

        # Clamp yaw rate
        yaw_rate = FixedPoint.clamp(yaw_rate, -K_MAX_YAW_RATE, K_MAX_YAW_RATE)

        # Sign lock (forwardLaunchSignLock = has_fwd_intent)
        # Convencao: yaw crescendo = esquerda. Esquerda quer yaw_rate > 0.
        # Clamp residuo negativo (direita errada) quando steering esquerda.
        if has_fwd_intent and has_steer and speed_abs < K_KIN_THRESHOLD:
            if steering < 0 and yaw_rate < 0:
                yaw_rate = 0.0
                yaw_accum = 0.0
            elif steering > 0 and yaw_rate > 0:
                yaw_rate = 0.0
                yaw_accum = 0.0

        # Acumulador e passo de yaw (replica integer truncation)
        yaw_accum += yaw_rate
        yaw_step = int(yaw_accum)          # truncation toward zero
        yaw_accum -= yaw_step
        yaw_deg = FixedPoint.normalize_yaw(yaw_deg + yaw_step)

        # Posicao (simplificada: apenas calcula direcao do movimento)
        yaw_rad_move = math.radians(yaw_deg)
        sin_yaw = math.sin(yaw_rad_move)
        cos_yaw = math.cos(yaw_rad_move)
        dx = sin_yaw * fwd_speed
        dz = -cos_yaw * fwd_speed

        speed_kmh = abs(fwd_speed) * SPEED_TO_KMH

        frames.append({
            "frame":      frame + 1,
            "yaw":        yaw_deg,
            "fwd_speed":  fwd_speed,
            "yaw_rate":   yaw_rate,
            "yaw_step":   yaw_step,
            "kinematic":  was_kinematic,
            "speed_kmh":  speed_kmh,
            "dx":         dx,
            "dz":         dz,
            "correct":    (yaw_step > 0 if steering < 0 else yaw_step < 0) if yaw_step != 0 else True,
        })

    return frames


def print_simulation(label: str, frames: list[dict], steer_label: str) -> None:
    sep = "-" * 78
    print(f"\n{'=' * 78}")
    print(f"SIMULACAO: {label}")
    print(f"  Steer: {steer_label}  |  Frames: {len(frames)}")
    print(sep)
    print(f"  {'Fr':>3}  {'Yaw':>5}  {'Step':>5}  {'Mode':^10}  "
          f"{'FwdSpd':>8}  {'KMH':>6}  {'Correto?':^10}")
    print(sep)
    any_wrong = False
    for f in frames:
        mode  = "CINEMAT" if f["kinematic"] else "SLIP"
        ok    = "OK" if f["correct"] else "!!! ERRADO"
        if not f["correct"]:
            any_wrong = True
        print(f"  {f['frame']:3d}  {f['yaw']:5d}  {f['yaw_step']:+5d}  "
              f"{mode:^10}  {f['fwd_speed']:8.4f}  {f['speed_kmh']:6.1f}  {ok}")
    print(sep)
    if any_wrong:
        print("  RESULTADO: BUG DETECTADO — algum frame virou na direcao errada")
    else:
        print("  RESULTADO: CORRETO — todos os frames viraram na direcao certa")
    print()


def run_simulations() -> None:
    print("\n" + "=" * 78)
    print("SIMULACAO DA FISICA DO INTERLAGOS — CINEMATICA vs SLIP")
    print("=" * 78)
    print("""
Objetivo: verificar se o modelo cinematico produz curva correta desde o frame 1.
O codigo simulado aqui corresponde as mudancas em car_dynamics_model.hpp:
  - useLowSpeedKinematic usa hasForwardDriveIntent (nao hasForwardDriveCommand)
  - forwardLaunchSignLock usa hasForwardDriveIntent
  - Inversao de steer so quando braking=true
""")

    # Cenario 1: carro parado, esquerda
    f1 = simulate_launch(steer_cmd=-100, initial_fwd_speed=0.0,
                         num_frames=20, label="Parado, esquerda")
    print_simulation("B + ESQUERDA, carro parado (speed=0)", f1, "ESQUERDA (-100)")

    # Cenario 2: carro parado, direita
    f2 = simulate_launch(steer_cmd=+100, initial_fwd_speed=0.0,
                         num_frames=20, label="Parado, direita")
    print_simulation("B + DIREITA, carro parado (speed=0)", f2, "DIREITA (+100)")

    # Cenario 3: velocidade residual negativa (reverse leve)
    f3 = simulate_launch(steer_cmd=-100, initial_fwd_speed=-0.3,
                         num_frames=20, label="Reverse residual, esquerda")
    print_simulation("B + ESQUERDA, residuo de reverse (speed=-0.3)", f3, "ESQUERDA (-100)")

    # Cenario 4: velocidade residual negativa forte
    f4 = simulate_launch(steer_cmd=-100, initial_fwd_speed=-1.5,
                         num_frames=20, label="Reverse forte, esquerda")
    print_simulation("B + ESQUERDA, reverse forte (speed=-1.5)", f4, "ESQUERDA (-100)")

    # Cenario 5: simetria — sem o sign fix (inverte steer quando fwd < 0)
    print("\n" + "-" * 78)
    print("COMPARATIVO: O QUE ACONTECIA ANTES DO FIX (steer invertido quando fwd<0)")
    print("-" * 78)
    print("""
Com o bug original: se forwardSpeed < 0, steerEffDeg era invertido mesmo sem
o freio pressionado. Isso fazia o slip model virar para DIREITA ao pressionar
ESQUERDA na transicao reverse->forward.

Com a correcao atual (hasForwardDriveIntent + steer inversion so quando braking):
o cinematico assume o controle imediatamente e o slip nao e chamado.
Todos os cenarios acima devem mostrar CORRETO.
""")


# ---------------------------------------------------------------------------
# PARTE 3 – ABORDAGEM DOS JOGOS DE EXEMPLO
# ---------------------------------------------------------------------------

def print_approach_summary() -> None:
    print("""
ABORDAGEM DOS JOGOS DE EXEMPLO (VDrift / StunRally / TORCS):
=============================================================

Todos os exemplos usam simulacao completa de pneu (Pacejka magic formula).
Isso significa:
  - Forcas laterais calculadas por angulo de deslizamento (alpha)
  - Momento de yaw = fyFront * lf - fyRear * lr
  - Integracao de corpo rigido (momentum angular, quaternion)

PROBLEMA ao usar este modelo em baixa velocidade:
  alpha_front = steer_angle - (vy + lf*r) / vx
  Com vx ~= 0: divisao por zero (clampado a kSlipDenomMin)
  Com vy=0, r=0: alpha_front = steer_angle
  fyFront = -Ca * alpha_front  => positivo para steer negativo (esquerda)
  yawMoment = fyFront * lf = POSITIVO = vira para DIREITA

  => O modelo de slip gera yaw ERRADO no primeiro frame de baixa velocidade.

COMO JOGOS ARCADE (Saturn/PS1) RESOLVIAM:
  Ridge Racer, Daytona USA, Virtua Racing => CINEMATICA PURA:
    yaw += steer * maxYawRate           (ignora o slip model)
    position += sin(yaw) * speed        (sempre move na direcao que aponta)

  Mario Kart 64 => CINEMATICA com drift em alta velocidade:
    if speed < threshold:
      yaw += steer * baseYawRate
    else:
      yaw += steer * speed / wheelbase  (formula de bicicleta)
    position += heading_vector * speed

NOSSA SOLUCAO (modo cinematico em car_dynamics_model.hpp):
  useLowSpeedKinematic = hasForwardDriveIntent && steer && speed < 2.5
  => yawRate = steerNorm * kMaxYawRateDegPerFrame  (direto, sempre correto)
  => position override: pos = prePos + sin(yawPost) * forwardSpeed

  hasForwardDriveIntent (nao hasForwardDriveCommand) garante que o cinematico
  cobre a transicao reverse->forward quando forwardSpeed ainda e negativo.
""")


# ---------------------------------------------------------------------------
# MAIN
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--simulate-only", action="store_true",
                        help="Apenas roda a simulacao, sem varrer exemplos")
    parser.add_argument("--scan-only", action="store_true",
                        help="Apenas varre os exemplos, sem simular")
    args = parser.parse_args()

    do_scan     = not args.simulate_only
    do_simulate = not args.scan_only

    if do_scan:
        print(f"\nVarrendo exemplos em: {EXAMPLES_ROOT}")
        results = scan_examples()
        print_scan_report(results)
        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        # Gera relatorio markdown
        md_lines = ["# Relatorio: Logica de Curva nos Projetos de Exemplo\n"]
        for proj, hits in sorted(results.items()):
            md_lines.append(f"## {proj}\n")
            if not hits:
                md_lines.append("_Nenhum trecho relevante encontrado._\n")
                continue
            by_group: dict[str, list] = {}
            for h in hits:
                by_group.setdefault(h["desc"], []).append(h)
            for desc, ghits in by_group.items():
                md_lines.append(f"### {desc}\n")
                for h in ghits[:5]:
                    md_lines.append(f"**{h['file']}** linha {h['line']}:\n```\n{h['snippet']}\n```\n")
        rpt = REPORT_DIR / "turning_examples_report.md"
        rpt.write_text("\n".join(md_lines), encoding="utf-8")
        print(f"\nRelatorio salvo em: {rpt}")

    if do_simulate:
        run_simulations()
        print_approach_summary()

    return 0


if __name__ == "__main__":
    sys.exit(main())
