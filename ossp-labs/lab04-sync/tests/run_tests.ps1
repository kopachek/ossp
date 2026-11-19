# Тесты корректности. Запуск из lab04-sync:  powershell -File tests\run_tests.ps1
$exe = Join-Path $PSScriptRoot "..\build\counter.exe"
$csv = Join-Path $env:TEMP "lab04_test.csv"
$fail = 0
function Check($name, $cond) {
    if ($cond) { Write-Host "[ OK ] $name" } else { Write-Host "[FAIL] $name"; $script:fail++ }
}

# 1. mutex и atomic: итог == procs*ops, нет нарушений инвариантов
foreach ($mode in "mutex", "atomic") {
    foreach ($p in 1, 2, 3, 8) {
        Remove-Item $csv -ErrorAction SilentlyContinue
        & $exe --mode $mode --procs $p --ops 20000 --runs 3 --check --quiet --csv $csv | Out-Null
        $code = $LASTEXITCODE
        $rows = @(Import-Csv $csv)
        $bad = @($rows | Where-Object { $_.ok -ne "1" -or $_.violations -ne "0" -or $_.lost -ne "0" })
        Check "$mode procs=$p exit=0, 3 runs, no violations" ($code -eq 0 -and $rows.Count -eq 3 -and $bad.Count -eq 0)
    }
}

# 2. none: программа должна завершаться корректно (потери обновлений допустимы)
& $exe --mode none --procs 3 --ops 20000 --quiet | Out-Null
Check "none mode terminates, exit=0" ($LASTEXITCODE -eq 0)

# 3. неверные аргументы -> код 2
& $exe --procs 0 2>$null | Out-Null
Check "bad args (procs=0) -> exit 2" ($LASTEXITCODE -eq 2)
& $exe --mode foo 2>$null | Out-Null
Check "bad args (mode=foo) -> exit 2" ($LASTEXITCODE -eq 2)

# 4. тайм-аут не вешает программу (ограничение времени ожидания)
& $exe --mode mutex --procs 2 --ops 1000000000 --timeout 300 --quiet | Out-Null
Check "timeout is handled (no hang), exit=1" ($LASTEXITCODE -eq 1)

Remove-Item $csv -ErrorAction SilentlyContinue
if ($fail) { Write-Host "FAILED: $fail"; exit 1 } else { Write-Host "ALL TESTS PASSED" }
