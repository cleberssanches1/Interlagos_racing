param(
    [string]$ProjectRoot = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing",
    [string]$PipeName = "\\.\pipe\kronos_trace",
    [string]$TraceLevel = "2",
    [string]$WatchFile = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\trace_watch_min.yaml",
    [string]$BridgeOutput = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\live_trace_min.jsonl",
    [string]$TraceDump = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\session_min.ktrace",
    [string]$BridgeScript = "C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\tools\kronos_trace_bridge.py",
    [string]$KronosExe = "",
    [string]$KronosArgs = ""
)

$ErrorActionPreference = "Stop"

function Test-RequiredFile {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label nao encontrado: $Path"
    }
}

Test-RequiredFile -Path $BridgeScript -Label "Bridge script"
Test-RequiredFile -Path $WatchFile -Label "Watch file"

$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCmd) {
    throw "Python nao encontrado no PATH."
}

$bridgeDir = Split-Path -Parent $BridgeScript
$bridgeCmd = "python `"$BridgeScript`" --pipe `"$PipeName`" -o `"$BridgeOutput`""

Write-Host "[1/3] Iniciando bridge em nova janela..."
Start-Process -FilePath "powershell.exe" -ArgumentList @(
    "-NoExit",
    "-Command",
    "Set-Location -LiteralPath `"$bridgeDir`"; $bridgeCmd"
) -WorkingDirectory $bridgeDir

Write-Host "[2/3] Configurando variaveis de ambiente desta sessao..."
$env:KRONOS_TRACE_LEVEL = $TraceLevel
$env:KRONOS_TRACE_WATCH_FILE = $WatchFile
$env:KRONOS_TRACE_PIPE = $PipeName
$env:KRONOS_TRACE_OUT = $TraceDump

Write-Host "KRONOS_TRACE_LEVEL=$env:KRONOS_TRACE_LEVEL"
Write-Host "KRONOS_TRACE_WATCH_FILE=$env:KRONOS_TRACE_WATCH_FILE"
Write-Host "KRONOS_TRACE_PIPE=$env:KRONOS_TRACE_PIPE"
Write-Host "KRONOS_TRACE_OUT=$env:KRONOS_TRACE_OUT"

if ([string]::IsNullOrWhiteSpace($KronosExe)) {
    Write-Host "[3/3] KronosExe nao informado. Abra o Kronos manualmente nesta sessao."
    Write-Host "Bridge output: $BridgeOutput"
    exit 0
}

if (-not (Test-Path -LiteralPath $KronosExe)) {
    throw "KronosExe nao encontrado: $KronosExe"
}

Write-Host "[3/3] Abrindo Kronos..."
if ([string]::IsNullOrWhiteSpace($KronosArgs)) {
    Start-Process -FilePath $KronosExe -WorkingDirectory (Split-Path -Parent $KronosExe)
} else {
    Start-Process -FilePath $KronosExe -ArgumentList $KronosArgs -WorkingDirectory (Split-Path -Parent $KronosExe)
}

Write-Host "Sessao iniciada."
