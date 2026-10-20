// Linked into the thread tests by make PARKER=event: NtCRT waits on an event per thread as on Windows 7 and earlier, in
// place of NtWaitForAlertByThreadId of Windows 8 and later. Set by an initializer, before the first wait.

extern long _NtCRT_ParkerMode;

static int __cdecl ParkerEvent(void)
{
    _NtCRT_ParkerMode = 2; /* NTCRT_PARKER_EVENT */
    return 0;
}

__attribute__((section(".CRT$XIB"), used)) static int(__cdecl* const ParkerEventPtr)(void) = ParkerEvent;
