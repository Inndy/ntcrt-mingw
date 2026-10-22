/*
 * <dlfcn.h> (src/Dlfcn.cpp), run in the directory of dllib.dll and dllocal.dll (test/dl/lib.c): dlopen by name, by
 * path with slashes and by full path, the reference counts of dlclose, dlsym of a handle, of the program, of
 * RTLD_DEFAULT and of RTLD_NEXT, RTLD_LOCAL and its opens, dlerror (the messages, cleared only by dlerror, of each
 * thread) and dladdr.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static int Failures;

#define CHECK(Condition)                                            \
    do                                                              \
    {                                                               \
        if (!(Condition))                                           \
        {                                                           \
            printf("dl: line %d: %s\n", __LINE__, #Condition);      \
            Failures++;                                             \
        }                                                           \
    } while (0)

/* The program's own, before that of dllib.dll */
__declspec(dllexport) int dl_shared(void)
{
    return 1;
}

static int ErrorIs(const char* Expected)
{
    const char* Error = dlerror();

    if (Error == NULL || strcmp(Error, Expected) != 0)
    {
        printf("dl: dlerror() is \"%s\", expected \"%s\"\n", Error != NULL ? Error : "(null)", Expected);
        return 0;
    }
    return 1;
}

/* A thread sees only its own error */
static int Thread(void* Argument)
{
    (void)Argument;
    if (dlerror() != NULL || dlsym(RTLD_DEFAULT, "dl_no_such_symbol") != NULL)
    {
        return 1;
    }
    return !ErrorIs("\"dl_no_such_symbol\": The specified procedure could not be found.");
}

int main(void)
{
    void *Lib, *Again, *Local, *Program;
    int (*Add)(int, int);
    int (*Shared)(void);
    char Path[1024];
    Dl_info Info;
    thrd_t Other;
    int Result = 1;
    FILE* Bad;

    CHECK(dlerror() == NULL);

    /* By name, then by path with a slash and by full path: the same module, three references */
    Lib = dlopen("dllib.dll", RTLD_NOW);
    CHECK(Lib != NULL);
    Again = dlopen("./dllib.dll", RTLD_LAZY | RTLD_GLOBAL);
    CHECK(Again == Lib);
    CHECK(_fullpath(Path, "dllib.dll", sizeof(Path)) != NULL);
    CHECK(dlopen(Path, RTLD_NOW) == Lib);
    Add = (int (*)(int, int))dlsym(Lib, "dl_add");
    CHECK(Add != NULL && Add(2, 3) == 5);
    CHECK(dlsym(Lib, "dl_value") != NULL && *(int*)dlsym(Lib, "dl_value") == 42);
    CHECK(dlerror() == NULL);

    /* The program: dlopen(NULL), its own export first, RTLD_NEXT finds the one of dllib.dll */
    Program = dlopen(NULL, RTLD_NOW);
    CHECK(Program != NULL);
    Shared = (int (*)(void))dlsym(Program, "dl_shared");
    CHECK(Shared != NULL && Shared() == 1);
    Shared = (int (*)(void))dlsym(RTLD_DEFAULT, "dl_shared");
    CHECK(Shared != NULL && Shared() == 1);
    Shared = (int (*)(void))dlsym(RTLD_NEXT, "dl_shared");
    CHECK(Shared != NULL && Shared() == 2);
    CHECK(dlsym(RTLD_DEFAULT, "dl_add") == (void*)Add);
    CHECK(dlclose(Program) == 0);

    /* dladdr: the module and the nearest export */
    CHECK(dladdr((char*)Add + 1, &Info) != 0);
    CHECK(Info.dli_fbase == Lib);
    CHECK(Info.dli_fname != NULL && strlen(Info.dli_fname) >= 9 &&
          _stricmp(Info.dli_fname + strlen(Info.dli_fname) - 9, "dllib.dll") == 0);
    CHECK(Info.dli_sname != NULL && strcmp(Info.dli_sname, "dl_add") == 0 && Info.dli_saddr == (void*)Add);
    CHECK(dladdr(&Info, &Info) == 0);

    /* RTLD_LOCAL: only its handle finds its symbols, until it is opened without */
    Local = dlopen("dllocal.dll", RTLD_NOW | RTLD_LOCAL);
    CHECK(Local != NULL);
    CHECK(dlsym(Local, "dl_local_only") != NULL);
    CHECK(dlsym(RTLD_DEFAULT, "dl_local_only") == NULL);
    CHECK(ErrorIs("\"dl_local_only\": The specified procedure could not be found."));
    CHECK(dlopen("dllocal.dll", RTLD_NOW | RTLD_LOCAL) == Local);
    CHECK(dlclose(Local) == 0);
    CHECK(dlsym(RTLD_DEFAULT, "dl_local_only") == NULL);
    CHECK(ErrorIs("\"dl_local_only\": The specified procedure could not be found."));
    CHECK(dlopen("dllocal.dll", RTLD_NOW) == Local);
    CHECK(dlsym(RTLD_DEFAULT, "dl_local_only") != NULL);
    CHECK(dlclose(Local) == 0 && dlclose(Local) == 0);

    /* Errors, which a call that succeeds keeps until dlerror */
    CHECK(dlopen("dl_no_such.dll", RTLD_NOW) == NULL);
    CHECK(dlsym(Lib, "dl_add") != NULL);
    CHECK(ErrorIs("\"dl_no_such.dll\": The specified module could not be found."));
    CHECK(dlerror() == NULL);
    CHECK(dlopen("no/such/dir.dll", RTLD_NOW) == NULL);
    CHECK(ErrorIs("\"no\\such\\dir.dll\": The specified module could not be found."));
    CHECK(dlsym(Lib, "dl_nope") == NULL);
    CHECK(ErrorIs("\"dl_nope\": The specified procedure could not be found."));
    Bad = fopen("dlbad.dll", "w");
    CHECK(Bad != NULL && fputs("not a module", Bad) >= 0 && fclose(Bad) == 0);
    CHECK(dlopen("dlbad.dll", RTLD_NOW) == NULL);
    CHECK(ErrorIs("\"dlbad.dll\": The module is not a valid Win32 application."));
    CHECK(dlsym(Lib, "dl_nope") == NULL);
    CHECK(thrd_create(&Other, Thread, NULL) == thrd_success && thrd_join(Other, &Result) == thrd_success);
    CHECK(Result == 0);
    CHECK(ErrorIs("\"dl_nope\": The specified procedure could not be found."));

    /* Three references: the module stays until the last dlclose */
    CHECK(dlclose(Lib) == 0 && dlclose(Lib) == 0);
    CHECK(dladdr((void*)Add, &Info) != 0);
    CHECK(dlclose(Lib) == 0);
    CHECK(dladdr((void*)Add, &Info) == 0);
    CHECK(dlclose(Lib) != 0);
    CHECK(dlerror() != NULL);

    if (Failures == 0)
    {
        printf("dl: ok\n");
    }
    return Failures != 0;
}
