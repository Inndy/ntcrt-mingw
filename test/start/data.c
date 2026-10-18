/* Data exported by a DLL, built with the default runtime */
__declspec(dllexport) int dll_values[4] = { 11, 22, 33, 44 };
__declspec(dllexport) const char dll_text[] = "pseudo";

__declspec(dllexport) int dll_get(int i)
{
    return dll_values[i];
}
