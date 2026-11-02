param(
    [string]$Port = 'COM15',
    [Alias('GoldSol')][string]$Root = 'C:\Users\Administrator\Desktop\1\three_renderer_async_quad_20261006\validation\gold_shield_flow_20261025\sol',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe",
    [switch]$VerifyOnly,
    [switch]$Preview,
    [switch]$Install
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $PythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($PythonCommand) { $Python = $PythonCommand.Source }
    else { throw 'Python not found. Install Python 3.11+ or supply -Python with its full path.' }
}
& $Python -c 'import sys; sys.exit(0 if sys.version_info >= (3,11) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or newer is required.' }
if ($Install) {
    & $Python -m pip install -r "$PSScriptRoot\requirements.txt"
    exit $LASTEXITCODE
}
if (-not $VerifyOnly) {
    & $Python -c 'import PySide6.QtCore, PySide6.QtWidgets, serial'
    if ($LASTEXITCODE -ne 0) { throw 'Dependencies missing. Run INSTALL_QT.cmd first (or RUN_QT_PANEL.ps1 -Install).' }
}
$PanelArguments = @('-E', '-B', "$PSScriptRoot\render2d_console.py", '--root', $Root, '--port', $Port)
if ($VerifyOnly) { $PanelArguments += '--check-only' }
if ($Preview) { $PanelArguments += '--preview' }
& $Python @PanelArguments
exit $LASTEXITCODE
