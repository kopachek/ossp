# Lab 04. Взаимодействие процессов: взаимное исключение и синхронизация

**Вариант 23. Общий счетчик** (Shared Memory, Mutex, InterlockedIncrement)

Несколько рабочих процессов Windows увеличивают общий счетчик в разделяемой памяти
(`CreateFileMapping` / `MapViewOfFile`). Режимы: `mutex`, `atomic`, а также `none`
(без защиты, для демонстрации гонки).

## Структура
- `src/` — исходный код
- `diagrams/` — схемы (PlantUML)