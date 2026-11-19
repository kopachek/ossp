#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <stdio.h>

#define MAX_PROCS 16

typedef enum { MODE_NONE = 0, MODE_MUTEX = 1, MODE_ATOMIC = 2 } Mode;

typedef struct {
    LONGLONG      t_origin;               /* QPC-отсчет начала эксперимента */
    volatile LONG counter;                /* общий счетчик */
    volatile LONG in_cs;                  /* сколько процессов сейчас в критической секции */
    volatile LONG violations;             /* число обнаруженных нарушений инвариантов */
    volatile LONG done[MAX_PROCS];        /* сколько операций выполнил каждый воркер */
    volatile LONG elapsed_us[MAX_PROCS];  /* время работы каждого воркера, мкс */
} SharedData;

/* Имя именованного объекта "shm", "mtx", "rdy", "go" */
static void obj_name(char *buf, size_t n, const char *kind, const char *tag)
{
    snprintf(buf, n, "Local\\lab04_%s_%s", kind, tag);
}

static const char *mode_name(Mode m)
{
    return m == MODE_MUTEX ? "mutex" : (m == MODE_ATOMIC ? "atomic" : "none");
}

#endif
