// The end of the process without exit, as Windows ends it: the last thread to end (pthread_exit, thrd_exit, a return,
// a thread of CreateThread) ends it with its status, ExitProcess with its code. The atexit functions run and the
// streams are flushed, as msvcrt does. make test-thread compares the output (one line, a pipe) and the status of each
// mode: main, c11, last, createthread, exitprocess, locked, lockedfd and mingwthr (and the files they write).

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <windows.h>
#include <winternl.h>

static pthread_key_t g_Key;
static volatile LONG g_Locked;

static void Destructor(void* Value)
{
    printf(" dtor:%s", (const char*)Value);
}

static void AtExit(void)
{
    printf(" atexit\n");
}

static void* Last(void* Arg)
{
    struct timespec Time = { 0, 100000000 };

    nanosleep(&Time, NULL);
    pthread_setspecific(g_Key, "last");
    printf(" return");
    return Arg;
}

static DWORD WINAPI Worker(void* Arg)
{
    Sleep(100);
    printf(" worker");
    return (DWORD)(ULONG_PTR)Arg;
}

/* Holds the lock of stdin, which the flush at the end meets before stdout, until ExitProcess ends it */
static DWORD WINAPI Locker(void* Arg)
{
    _lock_file(stdin);
    g_Locked = 1;
    Sleep(INFINITE);
    return (DWORD)(ULONG_PTR)Arg;
}

/* Blocks in a write to a full pipe that nobody reads, with the lock of the fd held, until ExitProcess ends it */
static int g_PipeWrite;

/* At the end, the fd of the pipe cannot be replaced: _dup2 fails with EBUSY */
static void AtExitDup2(void)
{
    int Result = _dup2(2, g_PipeWrite);

    printf(" dup2:%d,%d", Result, Result == -1 && errno == EBUSY);
}

static DWORD WINAPI PipeWriter(void* Arg)
{
    static char Data[65536];

    _write(g_PipeWrite, Data, sizeof(Data));
    return (DWORD)(ULONG_PTR)Arg;
}

/* The bytes in the pipe of the read handle (FilePipeLocalInformation) */
typedef struct
{
    ULONG Type, Configuration, MaximumInstances, CurrentInstances, InboundQuota, ReadDataAvailable, OutboundQuota,
        WriteQuotaAvailable, State, End;
} PIPE_LOCAL_INFORMATION;


static ULONG PipeData(int Fd)
{
    IO_STATUS_BLOCK Iosb;
    PIPE_LOCAL_INFORMATION Info;
    HANDLE Handle = (HANDLE)_get_osfhandle(Fd);

    if (NtQueryInformationFile(Handle, &Iosb, &Info, sizeof(Info), (FILE_INFORMATION_CLASS)24) < 0)
    {
        return 0;
    }
    return Info.ReadDataAvailable;
}

/* A destructor of libmingw32 (__mingwthr_key_dtor), which x64 runs at DLL_PROCESS_DETACH after the atexit functions */
int __mingwthr_key_dtor(DWORD Key, void (*Destructor)(void*));
static FILE* g_DestructorFile;

static void MingwDestructor(void* Value)
{
    fputs((const char*)Value, g_DestructorFile);
}

int main(int argc, char** argv)
{
    pthread_t Thread;
    const char* Mode = argc > 1 ? argv[1] : "main";

    atexit(AtExit);
    pthread_key_create(&g_Key, Destructor);
    pthread_setspecific(g_Key, "main");
    /* Buffered, a file or a pipe */
    printf("%s", Mode);
    if (strcmp(Mode, "c11") == 0)
    {
        thrd_exit(7);
    }
    if (strcmp(Mode, "last") == 0)
    {
        pthread_create(&Thread, NULL, Last, (void*)9);
    }
    if (strcmp(Mode, "createthread") == 0)
    {
        CloseHandle(CreateThread(NULL, 0, Worker, (void*)11, 0, NULL));
    }
    if (strcmp(Mode, "exitprocess") == 0)
    {
        ExitProcess(5);
    }
    if (strcmp(Mode, "locked") == 0)
    {
        CloseHandle(CreateThread(NULL, 0, Locker, NULL, 0, NULL));
        while (!g_Locked)
        {
            Sleep(1);
        }
        ExitProcess(6);
    }
    if (strcmp(Mode, "lockedfd") == 0 && argc > 2)
    {
        /* The stream of the pipe precedes that of the file: the flush leaves it, the file gets its text */
        int Pipe[2];
        FILE* Blocked;
        FILE* File;

        if (_pipe(Pipe, 4096, _O_BINARY) != 0 || (Blocked = _fdopen(Pipe[1], "wb")) == NULL ||
            (File = fopen(argv[2], "w")) == NULL)
        {
            return 1;
        }
        fputs("lost", Blocked);
        fputs(Mode, File);
        g_PipeWrite = Pipe[1];
        atexit(AtExitDup2);
        CloseHandle(CreateThread(NULL, 0, PipeWriter, NULL, 0, NULL));
        /* The write holds the lock of the fd once it wrote a byte, and blocks when the pipe is full */
        while (PipeData(Pipe[0]) < 4096)
        {
            Sleep(1);
        }
        Sleep(10);
        ExitProcess(8);
    }
    if (strcmp(Mode, "mingwthr") == 0 && argc > 2)
    {
        DWORD Slot = TlsAlloc();

        if ((g_DestructorFile = fopen(argv[2], "w")) == NULL || __mingwthr_key_dtor(Slot, MingwDestructor) != 0)
        {
            return 1;
        }
        TlsSetValue(Slot, (void*)Mode);
        ExitProcess(4);
    }
    pthread_exit((void*)7);
}
