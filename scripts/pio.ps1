param([Parameter(ValueFromRemainingArguments = $true)][string[]]$PioArguments)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $taskRoot 'scripts\pio.py') @PioArguments
exit $LASTEXITCODE
