/* The DLLs of test/dl/dl.c: dllib.dll, and dllocal.dll (-DLOCAL) for RTLD_LOCAL */

#ifdef LOCAL
__declspec(dllexport) int dl_local_only(void)
{
    return 7;
}
#else
__declspec(dllexport) int dl_value = 42;

__declspec(dllexport) int dl_add(int a, int b)
{
    return a + b;
}

/* The program exports one too, which RTLD_DEFAULT finds first and RTLD_NEXT skips */
__declspec(dllexport) int dl_shared(void)
{
    return 2;
}
#endif
