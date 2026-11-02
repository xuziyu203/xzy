param(
    [string]$Port = 'COM15',
    [string]$GoldSol = 'C:\Users\Administrator\Desktop\1\three_renderer_async_quad_20261006\validation\gold_shield_flow_20261025\sol',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe",
    [switch]$VerifyOnly,
    [switch]$Preview
)
& "$PSScriptRoot\RUN_QT_PANEL.ps1" -Port $Port -Root $GoldSol -Python $Python -VerifyOnly:$VerifyOnly -Preview:$Preview
exit $LASTEXITCODE
