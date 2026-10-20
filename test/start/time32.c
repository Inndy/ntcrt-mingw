/*
 * The sizes of time_t: the names of msvcrt without a size (time, localtime, _stati64, _findfirst, ...) take the
 * time_t of the architecture, 32 bits on x86 and 64 on x64, which the headers of msvcrt default to. They are called by
 * their import pointers, as a program built with other headers (tcc) calls them. On x86 the functions that the headers
 * call with a 32-bit time_t (_stat32i64, _findfirst32i64, the C11 functions of <threads.h>) agree with the 64-bit ones.
 */

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/utime.h>
#include <threads.h>
#include <time.h>

static int g_Fails;

#define CHECK(Expr)                                                  \
    do                                                               \
    {                                                                \
        if (!(Expr))                                                 \
        {                                                            \
            printf("FAIL time32 line %d: %s\n", __LINE__, #Expr);    \
            g_Fails++;                                               \
        }                                                            \
    } while (0)

/* The import pointers of the names of msvcrt */
#ifdef _WIN64
#define IMPORT(Name) extern void* __imp_##Name
#else
#define IMPORT(Name) extern void* __imp_##Name __asm__("__imp__" #Name)
#endif
IMPORT(time);
IMPORT(localtime);
IMPORT(mktime);
IMPORT(_stati64);
IMPORT(_findfirsti64);
IMPORT(_findfirst);
IMPORT(_findclose);

#define T (sizeof(time_t))

int main(void)
{
    static const char Name[] = "ntcrt_time32.tmp";
    unsigned char Buffer[512];
    struct _stat64 St64;
    struct __finddata64_t Find64;
    FILE* File;
    intptr_t Handle;
    __time64_t Now = _time64(NULL);

    File = fopen(Name, "w");
    fputs("12345", File);
    fclose(File);
    CHECK(_stat64(Name, &St64) == 0 && St64.st_size == 5);

    /* time writes a time_t of the size of the architecture */
    memset(Buffer, 0xAA, sizeof(Buffer));
    ((void (*)(void*))__imp_time)(Buffer);
    CHECK(Buffer[T] == 0xAA && Buffer[T - 1] != 0xAA);
    CHECK(T == 8 ? *(long long*)Buffer - Now <= 1 : *(int*)Buffer - (int)Now <= 1);

    /* localtime reads one: the bytes beyond are not a part of it */
    {
        struct tm* (*Localtime)(const void*) = (struct tm * (*)(const void*)) __imp_localtime;
        long long Wide = Now;
        struct tm Tm;

        memset(Buffer, 0x7F, sizeof(Buffer));
        memcpy(Buffer, &Wide, T);
        Tm = *Localtime(Buffer);
        CHECK(Tm.tm_year + 1900 >= 2026 && Tm.tm_year + 1900 < 2100);
        Tm.tm_isdst = -1;
        memset(Buffer, 0xAA, sizeof(Buffer));
        if (T == 8)
        {
            long long (*Mktime)(struct tm*) = (long long (*)(struct tm*))__imp_mktime;

            CHECK(Mktime(&Tm) == Now);
        } else
        {
            int (*Mktime)(struct tm*) = (int (*)(struct tm*))__imp_mktime;

            CHECK(Mktime(&Tm) == (int)Now);
        }
    }

    /* _stati64: struct _stat32i64 on x86, _stat64 on x64 */
    {
        int (*Stati64)(const char*, void*) = (int (*)(const char*, void*))__imp__stati64;
        size_t Size = T == 8 ? sizeof(struct _stat64) : sizeof(struct _stat32i64);
        struct _stat32i64 St32;

        memset(Buffer, 0xAA, sizeof(Buffer));
        CHECK(Stati64(Name, Buffer) == 0 && Buffer[Size] == 0xAA);
#ifndef _WIN64
        memcpy(&St32, Buffer, sizeof(St32));
        CHECK(St32.st_size == 5 && St32.st_mtime == (__time32_t)St64.st_mtime);
        memset(&St32, 0, sizeof(St32));
        CHECK(_stat32i64(Name, &St32) == 0 && St32.st_size == 5 && St32.st_mtime == (__time32_t)St64.st_mtime);
#else
        (void)St32;
#endif
    }

    /* _findfirsti64 and _findfirst */
    {
        intptr_t (*Findfirsti64)(const char*, void*) = (intptr_t (*)(const char*, void*))__imp__findfirsti64;
        intptr_t (*Findfirst)(const char*, void*) = (intptr_t (*)(const char*, void*))__imp__findfirst;
        int (*Findclose)(intptr_t) = (int (*)(intptr_t))__imp__findclose;
        size_t Sizei64 = T == 8 ? sizeof(struct __finddata64_t) : sizeof(struct _finddata32i64_t);
        size_t Size = T == 8 ? sizeof(struct _finddata64i32_t) : sizeof(struct _finddata32_t);

        CHECK((Handle = _findfirst64(Name, &Find64)) != -1 && Find64.size == 5);
        _findclose(Handle);
        memset(Buffer, 0xAA, sizeof(Buffer));
        CHECK((Handle = Findfirsti64(Name, Buffer)) != -1 && Buffer[Sizei64] == 0xAA);
        Findclose(Handle);
        memset(Buffer, 0xAA, sizeof(Buffer));
        CHECK((Handle = Findfirst(Name, Buffer)) != -1 && Buffer[Size] == 0xAA);
        Findclose(Handle);
#ifndef _WIN64
        {
            struct _finddata32i64_t Find;

            CHECK((Handle = _findfirst32i64(Name, &Find)) != -1 && Find.size == 5 &&
                  Find.time_write == (__time32_t)Find64.time_write && strcmp(Find.name, Name) == 0);
            _findclose(Handle);
        }
#endif
    }

    /* C11 with the time_t of the headers */
    {
        struct timespec Ts, Start;
        mtx_t Mtx;
        cnd_t Cnd;
        long long Ms;

        CHECK(timespec_get(&Ts, TIME_UTC) == TIME_UTC && Ts.tv_sec - (time_t)Now <= 1 && Ts.tv_nsec < 1000000000);
        CHECK(mtx_init(&Mtx, mtx_timed) == thrd_success && cnd_init(&Cnd) == thrd_success);
        CHECK(mtx_lock(&Mtx) == thrd_success);
        timespec_get(&Start, TIME_UTC);
        Ts = Start;
        Ts.tv_nsec += 50000000;
        if (Ts.tv_nsec >= 1000000000)
        {
            Ts.tv_sec++;
            Ts.tv_nsec -= 1000000000;
        }
        CHECK(cnd_timedwait(&Cnd, &Mtx, &Ts) == thrd_timedout);
        timespec_get(&Ts, TIME_UTC);
        Ms = (long long)(Ts.tv_sec - Start.tv_sec) * 1000 + (Ts.tv_nsec - Start.tv_nsec) / 1000000;
        CHECK(Ms >= 40 && Ms < 5000);
        CHECK(mtx_unlock(&Mtx) == thrd_success);
        Ts.tv_sec = 0;
        Ts.tv_nsec = 10000000;
        CHECK(thrd_sleep(&Ts, NULL) == 0);
        cnd_destroy(&Cnd);
        mtx_destroy(&Mtx);
    }

    remove(Name);
    printf("time32: %s (time_t of %d bytes)\n", g_Fails == 0 ? "ok" : "FAILED", (int)T);
    return g_Fails != 0;
}
