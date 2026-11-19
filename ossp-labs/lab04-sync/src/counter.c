/* Launch counter.exe [procs] [ops] */
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <string.h>
#include "common.h"

static int worker(int id, const char *tag, long ops)
{
    char name[128];
    obj_name(name, sizeof name, "shm", tag);
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!hMap) { fprintf(stderr, "worker %d: OpenFileMapping error %lu\n", id, GetLastError()); return 3; }
    SharedData *sh = (SharedData *)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sh) { fprintf(stderr, "worker %d: MapViewOfFile error %lu\n", id, GetLastError()); CloseHandle(hMap); return 3; }

    printf("WORKER %d | PID=%lu | STATE=WORKING\n", id, GetCurrentProcessId());
    for (long i = 0; i < ops; i++) sh->counter++;
    sh->done[id] = ops;
    printf("WORKER %d | PID=%lu | STATE=FINISHED\n", id, GetCurrentProcessId());

    UnmapViewOfFile(sh);
    CloseHandle(hMap);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && strcmp(argv[1], "--worker") == 0)
        return worker(atoi(argv[2]), argv[3], atol(argv[4]));

    int procs = argc > 1 ? atoi(argv[1]) : 3;
    long ops  = argc > 2 ? atol(argv[2]) : 50000;
    if (procs < 1 || procs > MAX_PROCS || ops < 1) { fprintf(stderr, "usage: counter [procs 1..%d] [ops]\n", MAX_PROCS); return 2; }

    char tag[64], name[128], exe[MAX_PATH];
    snprintf(tag, sizeof tag, "%lu", GetCurrentProcessId());
    GetModuleFileNameA(NULL, exe, MAX_PATH);

    obj_name(name, sizeof name, "shm", tag);
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedData), name);
    if (!hMap) { fprintf(stderr, "CreateFileMapping error %lu\n", GetLastError()); return 3; }
    SharedData *sh = (SharedData *)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sh) { fprintf(stderr, "MapViewOfFile error %lu\n", GetLastError()); CloseHandle(hMap); return 3; }

    HANDLE hProc[MAX_PROCS];
    int started = 0;
    for (int i = 0; i < procs; i++) {
        char cmd[512];
        snprintf(cmd, sizeof cmd, "\"%s\" --worker %d %s %ld", exe, i, tag, ops);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "CreateProcess error %lu\n", GetLastError());
            break;
        }
        CloseHandle(pi.hThread);
        hProc[started++] = pi.hProcess;
    }

    if (started) WaitForMultipleObjects(started, hProc, TRUE, 60000);

    long expected = (long)procs * ops;
    printf("counter = %ld, expected = %ld, lost = %ld\n", (long)sh->counter, expected, expected - (long)sh->counter);

    for (int i = 0; i < started; i++) CloseHandle(hProc[i]);
    UnmapViewOfFile(sh);
    CloseHandle(hMap);
    return started == procs ? 0 : 3;
}
