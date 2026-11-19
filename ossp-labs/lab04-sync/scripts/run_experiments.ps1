# Usage: powershell -ExecutionPolicy Bypass -File scripts\run_experiments.ps1 -StudentId 101
param(
    [Parameter(Mandatory = $true)][int]$StudentId,
    [int]$Runs = 5,
    [string]$Exe = ""
)
if (-not $Exe) { $Exe = Join-Path $PSScriptRoot "..\build\counter.exe" }
if (-not (Test-Path $Exe)) { Write-Host "Не найден $Exe. Сначала соберите программу (build.bat)."; exit 1 }

$data = Join-Path $PSScriptRoot "..\data"
New-Item -ItemType Directory -Force $data | Out-Null
Remove-Item "$data\*.csv" -ErrorAction SilentlyContinue

$base = 50000
$individual = $base + $StudentId * 5

function Run($file, $mode, $procs, $ops) {
    & $Exe --mode $mode --procs $procs --ops $ops --runs $Runs --quiet --csv (Join-Path $data $file)
}

# стандартные параметры
foreach ($m in "mutex", "atomic") { foreach ($p in 1..8) { Run "scaling.csv" $m $p $base } }

# индивидуальные параметры
foreach ($m in "mutex", "atomic") { foreach ($p in 2, 3) { Run "individual.csv" $m $p $individual } }

# без синхронизации
foreach ($p in 2, 3, 4) { Run "race.csv" "none" $p $base }

Write-Host "Готово. Данные в $data."
