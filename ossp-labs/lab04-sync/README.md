# Lab 4. Взаимодействие процессов: взаимное исключение и синхронизация

**Вариант 23. Общий счетчик** (Shared Memory, Mutex, InterlockedIncrement)

## Описание
`counter.exe` (координатор) создает разделяемую память (`CreateFileMapping`/`MapViewOfFile`) с общим
счетчиком и именованные объекты (Mutex, Semaphore «готовность», Event «старт»), запускает N рабочих
процессов (`CreateProcess`, тот же exe с ключом `--worker`). Каждый воркер увеличивает счетчик `ops` раз.
Режимы:
- `mutex`  — `WaitForSingleObject` / `ReleaseMutex` вокруг обычного `counter = counter + 1`;
- `atomic` — `InterlockedIncrement`;
- `none`   — без защиты.

После завершения координатор проверяет `counter == procs * ops`, коды выхода, число выполненных операций,
отсутствие удерживаемого мьютекса, нарушения взаимного исключения (с `--check`) и пишет результат в CSV.

## Сборка
MSVC:  `build.bat`

## Параметры запуска
`build\counter.exe [--mode mutex|atomic|none] [--procs 1..16] [--ops K] [--runs R] [--timeout MS] [--csv FILE] [--check] [--quiet]`

| параметр | значение по умолчанию |
| `--mode` | atomic |
| `--procs` | 3 |
| `--ops` | 50000 (на процесс) |
| `--runs` | 1 |
| `--timeout` | 60000 мс |
| `--check` | выкл. |
| `--quiet` | выкл. (подробный журнал событий и состояний) |

## Тесты
`powershell -File tests\run_tests.ps1`
