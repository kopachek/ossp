/* Usage: counter.exe [--mode mutex|atomic|none] [--procs N] [--ops K] [--runs R]
 *               [--timeout MS] [--csv FILE] [--check] [--quiet]
 * Код возврата: 0 - успех, 1 - нарушения в режимах mutex/atomic, 2 - ошибка аргументов, 3 - системная ошибка.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "common.h"

static LARGE_INTEGER g_freq;
static LONGLONG      g_origin;
static int           g_verbose = 1;

static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static long ms_since(LONGLONG origin) { return (long)((qpc() - origin) * 1000 / g_freq.QuadPart); }

static void logev(const char *fmt, ...)
{
    if (!g_verbose) return;
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    printf("[%06ld ms] %s\n", ms_since(g_origin), buf);
    fflush(stdout);
}


/* worker */
static int run_worker(int id, const char *tag, Mode mode, long ops, DWORD timeout, int check)
{
    char name[128];
    int rc = 3;
    long i = 0;
    HANDLE hMap = NULL, hMutex = NULL, hReady = NULL, hStart = NULL;
    SharedData *sh = NULL;
    LONGLONG t0, t1;

    obj_name(name, sizeof name, "shm", tag);
    hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!hMap) { fprintf(stderr, "worker %d: OpenFileMapping error %lu\n", id, GetLastError()); goto out; }
    sh = (SharedData *)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sh) { fprintf(stderr, "worker %d: MapViewOfFile error %lu\n", id, GetLastError()); goto out; }
    g_origin = sh->t_origin;

    obj_name(name, sizeof name, "rdy", tag);
    hReady = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
    obj_name(name, sizeof name, "go", tag);
    hStart = OpenEventA(SYNCHRONIZE, FALSE, name);
    if (mode == MODE_MUTEX) {
        obj_name(name, sizeof name, "mtx", tag);
        hMutex = OpenMutexA(SYNCHRONIZE, FALSE, name);
    }
    if (!hReady || !hStart || (mode == MODE_MUTEX && !hMutex)) {
        fprintf(stderr, "worker %d: cannot open sync objects, error %lu\n", id, GetLastError());
        goto out;
    }

    logev("WORKER %d | PID=%lu | STATE=WAITING", id, GetCurrentProcessId());
    if (!ReleaseSemaphore(hReady, 1, NULL)) { fprintf(stderr, "worker %d: ReleaseSemaphore error\n", id); goto out; }
    if (WaitForSingleObject(hStart, timeout) != WAIT_OBJECT_0) { fprintf(stderr, "worker %d: start timeout\n", id); goto out; }

    logev("WORKER %d | PID=%lu | STATE=WORKING", id, GetCurrentProcessId());
    t0 = qpc();
    rc = 0;

    if (mode == MODE_ATOMIC) {
        for (i = 0; i < ops; i++) InterlockedIncrement(&sh->counter);
    } else if (mode == MODE_NONE) {
        for (i = 0; i < ops; i++) sh->counter++;
    } else {
        for (i = 0; i < ops; i++) {
            DWORD w = WaitForSingleObject(hMutex, timeout);
            if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) { fprintf(stderr, "worker %d: mutex wait failed (%lu)\n", id, w); rc = 4; break; }
            if (w == WAIT_ABANDONED) InterlockedIncrement(&sh->violations);
            if (check && InterlockedIncrement(&sh->in_cs) != 1) InterlockedIncrement(&sh->violations);
            sh->counter = sh->counter + 1;
            if (check) InterlockedDecrement(&sh->in_cs);
            if (!ReleaseMutex(hMutex)) { rc = 4; break; }
        }
    }

    t1 = qpc();
    sh->done[id] = i;
    sh->elapsed_us[id] = (LONG)((t1 - t0) * 1000000 / g_freq.QuadPart);
    logev("WORKER %d | PID=%lu | STATE=FINISHED ops=%ld", id, GetCurrentProcessId(), i);

out:
    if (sh) UnmapViewOfFile(sh);
    if (hMutex) CloseHandle(hMutex);
    if (hReady) CloseHandle(hReady);
    if (hStart) CloseHandle(hStart);
    if (hMap) CloseHandle(hMap);
    return rc;
}


/* координатор */
typedef struct { Mode mode; int procs; long ops; int runs; DWORD timeout; const char *csv; int check; } Config;
typedef struct { double time_ms; long final_value, expected; int violations, timed_out, ok; } Result;

static int run_once(const Config *c, int run, Result *r)
{
    char tag[64], name[128], exe[MAX_PATH];
    HANDLE hMap = NULL, hMutex = NULL, hReady = NULL, hStart = NULL, hProc[MAX_PROCS] = {0};
    SharedData *sh = NULL;
    int started = 0, err = 0, bad_exit = 0, incomplete = 0, extra_viol = 0;
    DWORD w, ready_to;
    LONGLONG t0, t1;

    memset(r, 0, sizeof *r);
    r->expected = (long)c->procs * c->ops;
    snprintf(tag, sizeof tag, "%lu_%d", GetCurrentProcessId(), run);
    GetModuleFileNameA(NULL, exe, MAX_PATH);

    obj_name(name, sizeof name, "shm", tag);
    hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedData), name);
    if (!hMap) { fprintf(stderr, "CreateFileMapping error %lu\n", GetLastError()); return 3; }
    sh = (SharedData *)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sh) { fprintf(stderr, "MapViewOfFile error %lu\n", GetLastError()); err = 3; goto cleanup; }

    obj_name(name, sizeof name, "mtx", tag);
    hMutex = CreateMutexA(NULL, FALSE, name);
    obj_name(name, sizeof name, "rdy", tag);
    hReady = CreateSemaphoreA(NULL, 0, MAX_PROCS, name);
    obj_name(name, sizeof name, "go", tag);
    hStart = CreateEventA(NULL, TRUE, FALSE, name);
    if (!hMutex || !hReady || !hStart) { fprintf(stderr, "cannot create sync objects, error %lu\n", GetLastError()); err = 3; goto cleanup; }

    g_origin = qpc();
    sh->t_origin = g_origin;
    logev("RUN %d START mode=%s procs=%d ops=%ld", run, mode_name(c->mode), c->procs, c->ops);

    for (int i = 0; i < c->procs; i++) {
        char cmd[768];
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        snprintf(cmd, sizeof cmd, "\"%s\" --worker --id %d --tag %s --mode %s --ops %ld --timeout %lu --check %d%s",
                 exe, i, tag, mode_name(c->mode), c->ops, (unsigned long)c->timeout, c->check, g_verbose ? "" : " --quiet");
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "CreateProcess error %lu\n", GetLastError()); err = 3; goto cleanup;
        }
        CloseHandle(pi.hThread);
        hProc[started++] = pi.hProcess;
    }

    /* одновременный старт */
    ready_to = c->timeout < 10000 ? c->timeout : 10000;
    for (int i = 0; i < c->procs; i++)
        if (WaitForSingleObject(hReady, ready_to) != WAIT_OBJECT_0) { fprintf(stderr, "worker %d not ready\n", i); err = 3; goto cleanup; }

    t0 = qpc();
    SetEvent(hStart);
    w = WaitForMultipleObjects((DWORD)started, hProc, TRUE, c->timeout);
    t1 = qpc();
    r->time_ms = (double)(t1 - t0) * 1000.0 / (double)g_freq.QuadPart;
    if (w == WAIT_TIMEOUT || w == WAIT_FAILED) { r->timed_out = 1; logev("TIMEOUT: terminating workers"); goto cleanup; }

    for (int i = 0; i < started; i++) {
        DWORD code = 1;
        GetExitCodeProcess(hProc[i], &code);
        if (code != 0) bad_exit++;
        if (sh->done[i] != c->ops) incomplete++;
    }
    r->final_value = sh->counter;

    /* после завершения никто не удерживает мьютекс и никто не в критической секции */
    if (c->mode == MODE_MUTEX) {
        DWORD m = WaitForSingleObject(hMutex, 0);
        if (m == WAIT_OBJECT_0) ReleaseMutex(hMutex); else extra_viol++;
        if (sh->in_cs != 0) extra_viol++;
    }
    r->violations = (int)sh->violations + extra_viol;
    r->ok = !bad_exit && !incomplete && r->final_value == r->expected && r->violations == 0;
    logev("RUN %d END time=%.3f ms counter=%ld expected=%ld", run, r->time_ms, r->final_value, r->expected);

cleanup:
    for (int i = 0; i < started; i++) {
        if (r->timed_out || err) TerminateProcess(hProc[i], 99);
        CloseHandle(hProc[i]);
    }
    if (sh) UnmapViewOfFile(sh);
    if (hMutex) CloseHandle(hMutex);
    if (hReady) CloseHandle(hReady);
    if (hStart) CloseHandle(hStart);
    if (hMap) CloseHandle(hMap);
    return err;
}

static void write_csv(const Config *c, int run, const Result *r)
{
    FILE *f;
    int exists;
    if (!c->csv) return;
    f = fopen(c->csv, "r");
    exists = f != NULL;
    if (f) fclose(f);
    f = fopen(c->csv, "a");
    if (!f) { fprintf(stderr, "cannot open %s\n", c->csv); return; }
    if (!exists) fprintf(f, "mode,procs,ops,run,time_ms,final,expected,lost,violations,ok\n");
    fprintf(f, "%s,%d,%ld,%d,%.3f,%ld,%ld,%ld,%d,%d\n", mode_name(c->mode), c->procs, c->ops, run,
            r->time_ms, r->final_value, r->expected, r->expected - r->final_value, r->violations, r->ok);
    fclose(f);
}

static int parse_mode(const char *s, Mode *m)
{
    if (!strcmp(s, "mutex"))  { *m = MODE_MUTEX;  return 1; }
    if (!strcmp(s, "atomic")) { *m = MODE_ATOMIC; return 1; }
    if (!strcmp(s, "none"))   { *m = MODE_NONE;   return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    Config c = { MODE_ATOMIC, 3, 50000, 1, 60000, NULL, 0 };
    int worker = 0, id = 0, bad = 0, fails = 0, total_err = 0;
    const char *tag = "";
    double sum = 0;

    QueryPerformanceFrequency(&g_freq);

    for (int i = 1; i < argc && !bad; i++) {
        const char *a = argv[i];
        int has = i + 1 < argc;
        if      (!strcmp(a, "--worker")) worker = 1;
        else if (!strcmp(a, "--quiet"))  g_verbose = 0;
        else if (!strcmp(a, "--mode")    && has) { if (!parse_mode(argv[++i], &c.mode)) bad = 1; }
        else if (!strcmp(a, "--procs")   && has) c.procs = atoi(argv[++i]);
        else if (!strcmp(a, "--ops")     && has) c.ops = atol(argv[++i]);
        else if (!strcmp(a, "--runs")    && has) c.runs = atoi(argv[++i]);
        else if (!strcmp(a, "--timeout") && has) c.timeout = (DWORD)atol(argv[++i]);
        else if (!strcmp(a, "--csv")     && has) c.csv = argv[++i];
        else if (!strcmp(a, "--check")) {
            if (has && (argv[i+1][0] == '0' || argv[i+1][0] == '1') && argv[i+1][1] == 0) c.check = atoi(argv[++i]);
            else c.check = 1;
        }
        else if (!strcmp(a, "--id")      && has) id = atoi(argv[++i]);
        else if (!strcmp(a, "--tag")     && has) tag = argv[++i];
        else bad = 1;
    }
    if (bad || c.procs < 1 || c.procs > MAX_PROCS || c.ops < 1 || c.runs < 1 || c.timeout < 100) {
        fprintf(stderr, "usage: counter [--mode mutex|atomic|none] [--procs 1..%d] [--ops K] [--runs R]\n"
                        "               [--timeout MS] [--csv FILE] [--check] [--quiet]\n", MAX_PROCS);
        return 2;
    }

    if (worker) return run_worker(id, tag, c.mode, c.ops, c.timeout, c.check);

    for (int run = 1; run <= c.runs; run++) {
        Result r;
        int e = run_once(&c, run, &r);
        if (e) { total_err = e; break; }
        write_csv(&c, run, &r);
        printf("run %d: mode=%s procs=%d ops=%ld time=%.3f ms counter=%ld expected=%ld lost=%ld violations=%d %s%s\n",
               run, mode_name(c.mode), c.procs, c.ops, r.time_ms, r.final_value, r.expected, r.expected - r.final_value,
               r.violations, r.ok ? "OK" : "FAIL", r.timed_out ? " (TIMEOUT)" : "");
        sum += r.time_ms;
        if (!r.ok) fails++;
    }
    if (total_err) return total_err;
    printf("SUMMARY: mode=%s procs=%d runs=%d avg_time=%.3f ms, failed runs=%d\n", mode_name(c.mode), c.procs, c.runs, sum / c.runs, fails);
    if (c.mode == MODE_NONE) return 0;
    return fails ? 1 : 0;
}
