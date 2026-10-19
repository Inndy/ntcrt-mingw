// Linked into the thread tests by make PARKER=event: NtCRT waits on an event per thread as on Windows 7 and earlier, in
// place of NtWaitForAlertByThreadId of Windows 8 and later. Set by an initializer, before the first wait.

extern long _NtCRT_ParkerMode;

static void __cdecl ParkerEvent(void)
{
    _NtCRT_ParkerMode = 2; /* NTCRT_PARKER_EVENT */
}

__attribute__((section(".CRT$XIB"), used)) static void(__cdecl* const ParkerEventPtr)(void) = ParkerEvent;
