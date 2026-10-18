/* msvcrt time functions */

#include "NtCRT.h"

#include <errno.h>
#include <limits.h>
#include <sys/timeb.h>
#include <time.h>

#define NTCRT_EPOCH_DIFFERENCE 116444736000000000LL
#define NTCRT_TICKS_PER_SECOND 10000000LL
/* 3000-12-31 23:59:59 PST, the maximum of msvcrt */
#define NTCRT_MAX_TIME64 32535244799LL
/* gmtime accepts the whole range localtime may need, like msvcrt */
#define NTCRT_MIN_LOCAL_TIME (-12 * 3600LL)
#define NTCRT_MAX_LOCAL_TIME (13 * 3600LL)
#define NTCRT_MAX_TIME32 0x7FFFFFFFLL

EXTERN_C DWORD WINAPI _K32_GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation);

/* Calendar helpers, see "chrono-Compatible Low-Level Date Algorithms" of Howard Hinnant */

static
long long
_NtCRT_DaysFromCivil(
    _In_ long long y,
    _In_ unsigned int m,
    _In_ unsigned int d)
{
    long long Era;
    unsigned int Yoe, Doy, Doe;

    y -= m <= 2;
    Era = (y >= 0 ? y : y - 399) / 400;
    Yoe = (unsigned int)(y - Era * 400);
    Doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    Doe = Yoe * 365 + Yoe / 4 - Yoe / 100 + Doy;
    return Era * 146097 + (long long)Doe - 719468;
}

static
void
_NtCRT_CivilFromDays(
    _In_ long long z,
    _Out_ long long* y,
    _Out_ unsigned int* m,
    _Out_ unsigned int* d)
{
    long long Era;
    unsigned int Doe, Yoe, Doy, Mp;

    z += 719468;
    Era = (z >= 0 ? z : z - 146096) / 146097;
    Doe = (unsigned int)(z - Era * 146097);
    Yoe = (Doe - Doe / 1460 + Doe / 36524 - Doe / 146096) / 365;
    Doy = Doe - (365 * Yoe + Yoe / 4 - Yoe / 100);
    Mp = (5 * Doy + 2) / 153;
    *d = Doy - (153 * Mp + 2) / 5 + 1;
    *m = Mp < 10 ? Mp + 3 : Mp - 9;
    *y = (long long)Yoe + Era * 400 + (*m <= 2);
}

static
bool
_NtCRT_IsLeapYear(
    _In_ long long y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static
unsigned int
_NtCRT_DaysInMonth(
    _In_ long long y,
    _In_ unsigned int m)
{
    static const unsigned char Days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && _NtCRT_IsLeapYear(y) ? 29 : Days[m - 1];
}

/* Break seconds since 1970 down to tm, no range check */
static
void
_NtCRT_BreakDown(
    _In_ long long t,
    _Out_ struct tm* tm)
{
    long long Days = t / 86400, Secs = t % 86400, Year;
    unsigned int Month, Day;

    if (Secs < 0)
    {
        Secs += 86400;
        Days--;
    }
    _NtCRT_CivilFromDays(Days, &Year, &Month, &Day);
    tm->tm_sec = (int)(Secs % 60);
    tm->tm_min = (int)(Secs / 60 % 60);
    tm->tm_hour = (int)(Secs / 3600);
    tm->tm_mday = (int)Day;
    tm->tm_mon = (int)Month - 1;
    tm->tm_year = (int)(Year - 1900);
    tm->tm_wday = (int)((Days % 7 + 11) % 7);
    tm->tm_yday = (int)(Days - _NtCRT_DaysFromCivil(Year, 1, 1));
    tm->tm_isdst = 0;
}

/* Time zone state */

static struct
{
    volatile LONG Initialized;
    BOOL HasDst;
    BOOL UseRules;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
} g_Tz;

extern "C"
{
    long _NtCRT_timezone = 8 * 3600;
    int _NtCRT_daylight = 1;
    long _NtCRT_dstbias = -3600;
    char* _NtCRT_tzname[2];
}

static char g_TzName[2][64] = { "PST", "PDT" };
static WCHAR g_TzNameW[2][64] = { L"PST", L"PDT" };

static
void
_NtCRT_SetTzName(
    _In_ int Index,
    _In_ PCWSTR Name)
{
    size_t i;

    for (i = 0; Name[i] != L'\0' && i < ARRAYSIZE(g_TzNameW[Index]) - 1; i++)
    {
        g_TzNameW[Index][i] = Name[i];
    }
    g_TzNameW[Index][i] = L'\0';
    if (_NtCRT_WideCharToMultiByte(_NtCRT_FileCodePage(),
                                   0,
                                   g_TzNameW[Index],
                                   -1,
                                   g_TzName[Index],
                                   sizeof(g_TzName[Index]),
                                   NULL,
                                   NULL) <= 0)
    {
        g_TzName[Index][0] = '\0';
    }
}

/* Parse the TZ environment variable like "PST8PDT" or "CET-1:00CEST" */
static
BOOL
_NtCRT_ParseTzVariable(
    _In_ PCWSTR Tz)
{
    WCHAR Name[64];
    size_t i;
    long Sign = 1, Hours = 0, Minutes = 0, Seconds = 0;

    for (i = 0; Tz[i] != L'\0' && ((Tz[i] | 0x20) >= L'a' && (Tz[i] | 0x20) <= L'z') && i < ARRAYSIZE(Name) - 1; i++)
    {
        Name[i] = Tz[i];
    }
    if (i < 3)
    {
        return FALSE;
    }
    Name[i] = L'\0';
    _NtCRT_SetTzName(0, Name);
    Tz += i;
    if (*Tz == L'-')
    {
        Sign = -1;
        Tz++;
    } else if (*Tz == L'+')
    {
        Tz++;
    }
    while (*Tz >= L'0' && *Tz <= L'9')
    {
        Hours = Hours * 10 + (*Tz++ - L'0');
    }
    if (*Tz == L':')
    {
        Tz++;
        while (*Tz >= L'0' && *Tz <= L'9')
        {
            Minutes = Minutes * 10 + (*Tz++ - L'0');
        }
        if (*Tz == L':')
        {
            Tz++;
            while (*Tz >= L'0' && *Tz <= L'9')
            {
                Seconds = Seconds * 10 + (*Tz++ - L'0');
            }
        }
    }
    _NtCRT_timezone = Sign * (Hours * 3600 + Minutes * 60 + Seconds);
    for (i = 0; Tz[i] != L'\0' && i < ARRAYSIZE(Name) - 1; i++)
    {
        Name[i] = Tz[i];
    }
    Name[i] = L'\0';
    _NtCRT_daylight = i != 0;
    _NtCRT_dstbias = -3600;
    _NtCRT_SetTzName(1, Name);

    /* United States rules: second Sunday of March to first Sunday of November, at 2:00 */
    g_Tz.HasDst = _NtCRT_daylight;
    g_Tz.UseRules = TRUE;
    memset(&g_Tz.DaylightDate, 0, sizeof(g_Tz.DaylightDate));
    memset(&g_Tz.StandardDate, 0, sizeof(g_Tz.StandardDate));
    g_Tz.DaylightDate.wMonth = 3;
    g_Tz.DaylightDate.wDay = 2;
    g_Tz.DaylightDate.wHour = 2;
    g_Tz.StandardDate.wMonth = 11;
    g_Tz.StandardDate.wDay = 1;
    g_Tz.StandardDate.wHour = 2;
    return TRUE;
}

EXTERN_C void __cdecl _NtCRT__tzset(void)
{
    WCHAR Tz[64];
    UNICODE_STRING Name, Value;
    TIME_ZONE_INFORMATION Info;
    static WCHAR TzName[] = L"TZ";

    _NtCRT_lock(_TIME_LOCK);
    _NtCRT_tzname[0] = g_TzName[0];
    _NtCRT_tzname[1] = g_TzName[1];

    _Inline_RtlInitUnicodeString(&Name, TzName);
    Value.Buffer = Tz;
    Value.Length = 0;
    Value.MaximumLength = sizeof(Tz) - sizeof(WCHAR);
    if (NT_SUCCESS(RtlQueryEnvironmentVariable_U(NULL, &Name, &Value)) && Value.Length != 0)
    {
        Tz[Value.Length / sizeof(WCHAR)] = L'\0';
        if (_NtCRT_ParseTzVariable(Tz))
        {
            goto _Exit;
        }
    }

    if (_K32_GetTimeZoneInformation(&Info) != TIME_ZONE_ID_INVALID)
    {
        _NtCRT_timezone = Info.Bias * 60;
        _NtCRT_SetTzName(0, Info.StandardName);
        _NtCRT_SetTzName(1, Info.DaylightName);
        if (Info.StandardDate.wMonth != 0)
        {
            _NtCRT_timezone += Info.StandardBias * 60;
        }
        if (Info.DaylightDate.wMonth != 0 && Info.DaylightBias != 0)
        {
            _NtCRT_daylight = 1;
            _NtCRT_dstbias = (Info.DaylightBias - Info.StandardBias) * 60;
            g_Tz.HasDst = TRUE;
            g_Tz.UseRules = TRUE;
            g_Tz.DaylightDate = Info.DaylightDate;
            g_Tz.StandardDate = Info.StandardDate;
        } else
        {
            _NtCRT_daylight = 0;
            _NtCRT_dstbias = 0;
            g_Tz.HasDst = FALSE;
            g_Tz.UseRules = FALSE;
        }
    }

_Exit:
    _InterlockedExchange(&g_Tz.Initialized, 1);
    _NtCRT_unlock(_TIME_LOCK);
}

static
void
_NtCRT_EnsureTz(void)
{
    if (!g_Tz.Initialized)
    {
        _NtCRT__tzset();
    }
}

/* Transition time of a rule in local seconds of the year y */
static
long long
_NtCRT_TransitionTime(
    _In_ long long Year,
    _In_ const SYSTEMTIME* Rule)
{
    unsigned int Day;
    long long Days;

    if (Rule->wYear != 0)
    {
        Day = Rule->wDay;
    } else
    {
        /* wDay is the occurrence of wDayOfWeek in the month, 5 is the last one */
        long long First = _NtCRT_DaysFromCivil(Year, Rule->wMonth, 1);
        unsigned int FirstDow = (unsigned int)((First % 7 + 11) % 7);
        unsigned int Max = _NtCRT_DaysInMonth(Year, Rule->wMonth);

        Day = 1 + (Rule->wDayOfWeek + 7 - FirstDow) % 7 + (Rule->wDay - 1) * 7;
        while (Day > Max)
        {
            Day -= 7;
        }
    }
    Days = _NtCRT_DaysFromCivil(Year, Rule->wMonth, Day);
    return Days * 86400 + Rule->wHour * 3600 + Rule->wMinute * 60 + Rule->wSecond;
}

/* Whether a local standard time is in daylight saving time */
static
bool
_NtCRT_IsInDst(
    _In_ long long LocalStandard)
{
    struct tm tm;
    long long Start, End;

    if (!g_Tz.HasDst || !g_Tz.UseRules)
    {
        return false;
    }
    _NtCRT_BreakDown(LocalStandard, &tm);
    /* DaylightDate is in standard time, StandardDate is in daylight time */
    Start = _NtCRT_TransitionTime(tm.tm_year + 1900LL, &g_Tz.DaylightDate);
    End = _NtCRT_TransitionTime(tm.tm_year + 1900LL, &g_Tz.StandardDate) + _NtCRT_dstbias;
    if (Start < End)
    {
        return LocalStandard >= Start && LocalStandard < End;
    }
    return LocalStandard >= Start || LocalStandard < End;
}

/* Current time */

static
long long
_NtCRT_CurrentFileTime(void)
{
    FILETIME Time;

    _Inline_GetSystemTimeAsFileTime(&Time);
    return ((long long)Time.dwHighDateTime << 32) | Time.dwLowDateTime;
}

EXTERN_C __time64_t __cdecl _NtCRT__time64(__time64_t* t)
{
    __time64_t Now = (_NtCRT_CurrentFileTime() - NTCRT_EPOCH_DIFFERENCE) / NTCRT_TICKS_PER_SECOND;

    if (t != NULL)
    {
        *t = Now;
    }
    return Now;
}

EXTERN_C __time32_t __cdecl _NtCRT__time32(__time32_t* t)
{
    __time64_t Now = _NtCRT__time64(NULL);
    __time32_t Ret = Now > NTCRT_MAX_TIME32 ? (__time32_t)-1 : (__time32_t)Now;

    if (t != NULL)
    {
        *t = Ret;
    }
    return Ret;
}

EXTERN_C void __cdecl _NtCRT__ftime64(struct __timeb64* tb)
{
    long long Now = _NtCRT_CurrentFileTime() - NTCRT_EPOCH_DIFFERENCE;

    if (tb == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return;
    }
    _NtCRT_EnsureTz();
    tb->time = Now / NTCRT_TICKS_PER_SECOND;
    tb->millitm = (unsigned short)(Now / 10000 % 1000);
    tb->timezone = (short)(_NtCRT_timezone / 60);
    tb->dstflag = (short)_NtCRT_IsInDst(tb->time - _NtCRT_timezone);
}

EXTERN_C int __cdecl _NtCRT__ftime64_s(struct __timeb64* tb)
{
    if (tb == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT__ftime64(tb);
    return 0;
}

EXTERN_C int __cdecl _NtCRT__ftime32_s(struct __timeb32* tb);

EXTERN_C void __cdecl _NtCRT__ftime32(struct __timeb32* tb)
{
    _NtCRT__ftime32_s(tb);
}

EXTERN_C int __cdecl _NtCRT__ftime32_s(struct __timeb32* tb)
{
    struct __timeb64 Tb;

    if (tb == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT__ftime64(&Tb);
    tb->time = (__time32_t)Tb.time;
    tb->millitm = Tb.millitm;
    tb->timezone = Tb.timezone;
    tb->dstflag = Tb.dstflag;
    return 0;
}

EXTERN_C clock_t __cdecl _NtCRT_clock(void)
{
    KERNEL_USER_TIMES Times;

    if (!NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &Times, sizeof(Times), NULL)))
    {
        return -1;
    }
    return (clock_t)((_NtCRT_CurrentFileTime() - Times.CreateTime.QuadPart) /
                     (NTCRT_TICKS_PER_SECOND / CLOCKS_PER_SEC));
}

EXTERN_C double __cdecl _NtCRT__difftime64(__time64_t t1, __time64_t t0)
{
    return (double)(t1 - t0);
}

EXTERN_C double __cdecl _NtCRT__difftime32(__time32_t t1, __time32_t t0)
{
    return (double)t1 - (double)t0;
}

/* Conversion */

static
struct tm*
_NtCRT_TmBuffer(void)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    if (Ptd->TmBuffer == NULL)
    {
        Ptd->TmBuffer = (struct tm*)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(struct tm));
    }
    return Ptd->TmBuffer;
}

EXTERN_C int __cdecl _NtCRT__gmtime64_s(struct tm* tm, const __time64_t* t)
{
    if (tm == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    memset(tm, 0xFF, sizeof(*tm));
    if (t == NULL || *t < NTCRT_MIN_LOCAL_TIME || *t > NTCRT_MAX_TIME64 + NTCRT_MAX_LOCAL_TIME)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_BreakDown(*t, tm);
    return 0;
}

EXTERN_C struct tm* __cdecl _NtCRT__gmtime64(const __time64_t* t)
{
    struct tm* tm = _NtCRT_TmBuffer();
    return tm != NULL && _NtCRT__gmtime64_s(tm, t) == 0 ? tm : NULL;
}

EXTERN_C int __cdecl _NtCRT__localtime64_s(struct tm* tm, const __time64_t* t)
{
    long long Local;

    if (tm == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    memset(tm, 0xFF, sizeof(*tm));
    if (t == NULL || *t < 0 || *t > NTCRT_MAX_TIME64)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_EnsureTz();
    Local = *t - _NtCRT_timezone;
    if (_NtCRT_IsInDst(Local))
    {
        _NtCRT_BreakDown(Local - _NtCRT_dstbias, tm);
        tm->tm_isdst = 1;
    } else
    {
        _NtCRT_BreakDown(Local, tm);
    }
    return 0;
}

EXTERN_C struct tm* __cdecl _NtCRT__localtime64(const __time64_t* t)
{
    struct tm* tm = _NtCRT_TmBuffer();
    return tm != NULL && _NtCRT__localtime64_s(tm, t) == 0 ? tm : NULL;
}

/* Normalize tm fields and return seconds as if tm is UTC, or LLONG_MIN on overflow */
static
long long
_NtCRT_TmToSeconds(
    _Inout_ struct tm* tm)
{
    long long Year = (long long)tm->tm_year + 1900;
    long long Month = tm->tm_mon;
    long long Days;

    Year += Month / 12;
    Month %= 12;
    if (Month < 0)
    {
        Month += 12;
        Year--;
    }
    if (Year < 1900 || Year > 3001)
    {
        return LLONG_MIN;
    }
    Days = _NtCRT_DaysFromCivil(Year, (unsigned int)Month + 1, 1) + tm->tm_mday - 1;
    return Days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
}

static
__time64_t
_NtCRT_MakeTime(
    _Inout_ struct tm* tm,
    _In_ BOOL Local,
    _In_ long long Max)
{
    long long Seconds;
    struct tm Result;

    if (tm == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Seconds = _NtCRT_TmToSeconds(tm);
    if (Seconds == LLONG_MIN)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (Local)
    {
        _NtCRT_EnsureTz();
        if (tm->tm_isdst > 0 || (tm->tm_isdst < 0 && _NtCRT_IsInDst(Seconds)))
        {
            Seconds += _NtCRT_dstbias;
        }
        Seconds += _NtCRT_timezone;
    }
    if (Seconds < 0 || Seconds > Max)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (Local)
    {
        _NtCRT__localtime64_s(&Result, &Seconds);
    } else
    {
        _NtCRT_BreakDown(Seconds, &Result);
    }
    *tm = Result;
    return Seconds;
}

EXTERN_C __time64_t __cdecl _NtCRT__mktime64(struct tm* tm)
{
    return _NtCRT_MakeTime(tm, TRUE, NTCRT_MAX_TIME64);
}

EXTERN_C __time64_t __cdecl _NtCRT__mkgmtime64(struct tm* tm)
{
    return _NtCRT_MakeTime(tm, FALSE, NTCRT_MAX_TIME64);
}

EXTERN_C __time32_t __cdecl _NtCRT__mktime32(struct tm* tm)
{
    return (__time32_t)_NtCRT_MakeTime(tm, TRUE, NTCRT_MAX_TIME32);
}

EXTERN_C __time32_t __cdecl _NtCRT__mkgmtime32(struct tm* tm)
{
    return (__time32_t)_NtCRT_MakeTime(tm, FALSE, NTCRT_MAX_TIME32);
}

EXTERN_C struct tm* __cdecl _NtCRT__gmtime32(const __time32_t* t)
{
    __time64_t Time;

    if (t == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Time = *t;
    return _NtCRT__gmtime64(&Time);
}

EXTERN_C struct tm* __cdecl _NtCRT__localtime32(const __time32_t* t)
{
    __time64_t Time;

    if (t == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Time = *t;
    return _NtCRT__localtime64(&Time);
}

EXTERN_C int __cdecl _NtCRT__gmtime32_s(struct tm* tm, const __time32_t* t)
{
    __time64_t Time;

    if (t == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    Time = *t;
    return _NtCRT__gmtime64_s(tm, &Time);
}

EXTERN_C int __cdecl _NtCRT__localtime32_s(struct tm* tm, const __time32_t* t)
{
    __time64_t Time;

    if (t == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    Time = *t;
    return _NtCRT__localtime64_s(tm, &Time);
}

/* Formatting */

static const char* const g_DayNames[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char* const g_MonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* "Wed Jan 02 02:03:55 1980\n" */
static
void
_NtCRT_FormatAsctime(
    _Out_writes_(26) char* Buffer,
    _In_ const struct tm* tm)
{
    const char* Day = (unsigned int)tm->tm_wday < 7 ? g_DayNames[tm->tm_wday] : "???";
    const char* Month = (unsigned int)tm->tm_mon < 12 ? g_MonthNames[tm->tm_mon] : "???";
    int Year = tm->tm_year + 1900;

    Buffer[0] = Day[0];
    Buffer[1] = Day[1];
    Buffer[2] = Day[2];
    Buffer[3] = ' ';
    Buffer[4] = Month[0];
    Buffer[5] = Month[1];
    Buffer[6] = Month[2];
    Buffer[7] = ' ';
    Buffer[8] = (char)('0' + tm->tm_mday / 10 % 10);
    Buffer[9] = (char)('0' + tm->tm_mday % 10);
    Buffer[10] = ' ';
    Buffer[11] = (char)('0' + tm->tm_hour / 10 % 10);
    Buffer[12] = (char)('0' + tm->tm_hour % 10);
    Buffer[13] = ':';
    Buffer[14] = (char)('0' + tm->tm_min / 10 % 10);
    Buffer[15] = (char)('0' + tm->tm_min % 10);
    Buffer[16] = ':';
    Buffer[17] = (char)('0' + tm->tm_sec / 10 % 10);
    Buffer[18] = (char)('0' + tm->tm_sec % 10);
    Buffer[19] = ' ';
    Buffer[20] = (char)('0' + Year / 1000);
    Buffer[21] = (char)('0' + Year / 100 % 10);
    Buffer[22] = (char)('0' + Year / 10 % 10);
    Buffer[23] = (char)('0' + Year % 10);
    Buffer[24] = '\n';
    Buffer[25] = '\0';
}

static
char*
_NtCRT_AsctimeBuffer(void)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    if (Ptd->AsctimeBuffer == NULL)
    {
        Ptd->AsctimeBuffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, 26 * sizeof(wchar_t));
    }
    return Ptd->AsctimeBuffer;
}

/* As msvcrt, only an invalid field of tm resets the buffer */
EXTERN_C int __cdecl _NtCRT_asctime_s(char* buffer, size_t size, const struct tm* tm)
{
    if (buffer == NULL || size < 26 || tm == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (tm->tm_year < 0 ||
        (unsigned int)tm->tm_mon > 11 ||
        (unsigned int)tm->tm_hour > 23 ||
        (unsigned int)tm->tm_min > 59 ||
        (unsigned int)tm->tm_sec > 59 ||
        tm->tm_mday < 1 ||
        (unsigned int)tm->tm_mday > _NtCRT_DaysInMonth(tm->tm_year + 1900LL, tm->tm_mon + 1) ||
        (unsigned int)tm->tm_wday > 6)
    {
        buffer[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_FormatAsctime(buffer, tm);
    return 0;
}

EXTERN_C char* __cdecl _NtCRT_asctime(const struct tm* tm)
{
    char* Buffer = _NtCRT_AsctimeBuffer();
    return Buffer != NULL && _NtCRT_asctime_s(Buffer, 26, tm) == 0 ? Buffer : NULL;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wasctime(const struct tm* tm)
{
    char Narrow[26];
    wchar_t* Buffer = (wchar_t*)_NtCRT_AsctimeBuffer();

    if (Buffer == NULL || _NtCRT_asctime_s(Narrow, sizeof(Narrow), tm) != 0)
    {
        return NULL;
    }
    for (int i = 0; i < 26; i++)
    {
        Buffer[i] = (unsigned char)Narrow[i];
    }
    return Buffer;
}

EXTERN_C errno_t __cdecl _NtCRT__wasctime_s(wchar_t* buffer, size_t size, const struct tm* tm)
{
    char Narrow[26];
    errno_t Ret;

    if (buffer == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Ret = _NtCRT_asctime_s(Narrow, size < sizeof(Narrow) ? size : sizeof(Narrow), tm);
    if (Ret != 0)
    {
        buffer[0] = L'\0';
        return Ret;
    }
    for (int i = 0; i < 26; i++)
    {
        buffer[i] = (unsigned char)Narrow[i];
    }
    return 0;
}

template<typename TChar, typename TTime>
static
errno_t
_NtCRT_CtimeS(
    _Out_writes_z_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_ const TTime* Time,
    _In_ int(__cdecl* Localtime)(struct tm*, const TTime*))
{
    struct tm tm;
    errno_t Ret;

    if (Buffer == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Buffer[0] = 0;
    if (Size < 26 || Time == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Ret = Localtime(&tm, Time);
    if (Ret != 0)
    {
        return Ret;
    }
    if constexpr (sizeof(TChar) == sizeof(char))
    {
        return _NtCRT_asctime_s((char*)Buffer, Size, &tm);
    } else
    {
        return _NtCRT__wasctime_s((wchar_t*)Buffer, Size, &tm);
    }
}

EXTERN_C errno_t __cdecl _NtCRT__ctime64_s(char* buffer, size_t size, const __time64_t* t)
{
    return _NtCRT_CtimeS(buffer, size, t, _NtCRT__localtime64_s);
}

EXTERN_C errno_t __cdecl _NtCRT__ctime32_s(char* buffer, size_t size, const __time32_t* t)
{
    return _NtCRT_CtimeS(buffer, size, t, _NtCRT__localtime32_s);
}

EXTERN_C errno_t __cdecl _NtCRT__wctime64_s(wchar_t* buffer, size_t size, const __time64_t* t)
{
    return _NtCRT_CtimeS(buffer, size, t, _NtCRT__localtime64_s);
}

EXTERN_C errno_t __cdecl _NtCRT__wctime32_s(wchar_t* buffer, size_t size, const __time32_t* t)
{
    return _NtCRT_CtimeS(buffer, size, t, _NtCRT__localtime32_s);
}

EXTERN_C char* __cdecl _NtCRT__ctime64(const __time64_t* t)
{
    struct tm* tm = _NtCRT__localtime64(t);
    return tm != NULL ? _NtCRT_asctime(tm) : NULL;
}

EXTERN_C char* __cdecl _NtCRT__ctime32(const __time32_t* t)
{
    struct tm* tm = _NtCRT__localtime32(t);
    return tm != NULL ? _NtCRT_asctime(tm) : NULL;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wctime64(const __time64_t* t)
{
    struct tm* tm = _NtCRT__localtime64(t);
    return tm != NULL ? _NtCRT__wasctime(tm) : NULL;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wctime32(const __time32_t* t)
{
    struct tm* tm = _NtCRT__localtime32(t);
    return tm != NULL ? _NtCRT__wasctime(tm) : NULL;
}

/* ISO 8601 week-based year and week number */
static
int
_NtCRT_IsoWeek(
    _In_ const struct tm* tm,
    _Out_ int* IsoYear)
{
    int Year = tm->tm_year + 1900;
    int Wday = (tm->tm_wday + 6) % 7;
    int Week = (tm->tm_yday - Wday + 10) / 7;

    if (Week < 1)
    {
        int PrevYday;

        Year--;
        PrevYday = tm->tm_yday + (_NtCRT_IsLeapYear(Year) ? 366 : 365);
        Week = (PrevYday - Wday + 10) / 7;
    } else if (Week == 53)
    {
        int DaysInYear = _NtCRT_IsLeapYear(Year) ? 366 : 365;
        if (tm->tm_yday - Wday + 3 >= DaysInYear)
        {
            Week = 1;
            Year++;
        }
    }
    *IsoYear = Year;
    return Week;
}

template<typename TChar>
struct NTCRT_FORMATTER
{
    TChar* Buffer;
    size_t Size;
    size_t Used;
    bool Overflow;

    void Put(TChar c)
    {
        if (Used + 1 < Size)
        {
            Buffer[Used++] = c;
        } else
        {
            Overflow = true;
        }
    }

    void PutAscii(const char* s)
    {
        while (*s != '\0')
        {
            Put((TChar)(unsigned char)*s++);
        }
    }

    void PutNumber(long long Value, int Width, bool NoPad)
    {
        char Digits[24];
        int Count = 0;
        bool Negative = Value < 0;
        unsigned long long Magnitude = Negative ? 0 - (unsigned long long)Value : (unsigned long long)Value;

        do
        {
            Digits[Count++] = (char)('0' + Magnitude % 10);
            Magnitude /= 10;
        } while (Magnitude != 0);
        if (Negative)
        {
            Put('-');
        }
        if (!NoPad)
        {
            for (int i = Count; i < Width; i++)
            {
                Put('0');
            }
        }
        while (Count > 0)
        {
            Put((TChar)Digits[--Count]);
        }
    }
};

static
const char*
_NtCRT_TzNameOf(
    _In_ int Dst)
{
    return g_TzName[Dst > 0 ? 1 : 0];
}

static
const wchar_t*
_NtCRT_TzNameOfW(
    _In_ int Dst)
{
    return g_TzNameW[Dst > 0 ? 1 : 0];
}

template<typename TChar>
static
void
_NtCRT_FormatTime(
    _Inout_ NTCRT_FORMATTER<TChar>& Out,
    _In_ const TChar* Format,
    _In_ const struct tm* tm)
{
    for (; *Format != 0 && !Out.Overflow; Format++)
    {
        bool NoPad = false;
        int Hour12;

        if (*Format != '%')
        {
            Out.Put(*Format);
            continue;
        }
        Format++;
        if (*Format == '#')
        {
            NoPad = true;
            Format++;
        }
        /* E and O modifiers of C99 are ignored */
        if (*Format == 'E' || *Format == 'O')
        {
            Format++;
        }
        Hour12 = tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12;
        switch (*Format)
        {
            case 'a':
                if ((unsigned int)tm->tm_wday < 7)
                {
                    char Name[4] = {
                        g_DayNames[tm->tm_wday][0], g_DayNames[tm->tm_wday][1], g_DayNames[tm->tm_wday][2], '\0'
                    };
                    Out.PutAscii(Name);
                }
                break;
            case 'A':
                if ((unsigned int)tm->tm_wday < 7)
                {
                    Out.PutAscii(g_DayNames[tm->tm_wday]);
                }
                break;
            case 'b':
            case 'h':
                if ((unsigned int)tm->tm_mon < 12)
                {
                    char Name[4] = {
                        g_MonthNames[tm->tm_mon][0], g_MonthNames[tm->tm_mon][1], g_MonthNames[tm->tm_mon][2], '\0'
                    };
                    Out.PutAscii(Name);
                }
                break;
            case 'B':
                if ((unsigned int)tm->tm_mon < 12)
                {
                    Out.PutAscii(g_MonthNames[tm->tm_mon]);
                }
                break;
            case 'c':
                if (NoPad)
                {
                    /* Long date and time of the "C" locale */
                    static const TChar LongFormat[] = { '%', 'A', ',', ' ', '%', 'B', ' ', '%', '#', 'd', ',', ' ',
                                                        '%', 'Y', ' ', '%', 'H', ':', '%', 'M', ':', '%', 'S', 0 };
                    _NtCRT_FormatTime(Out, LongFormat, tm);
                } else
                {
                    static const TChar ShortFormat[] = { '%', 'm', '/', '%', 'd', '/', '%', 'y', ' ',
                                                         '%', 'H', ':', '%', 'M', ':', '%', 'S', 0 };
                    _NtCRT_FormatTime(Out, ShortFormat, tm);
                }
                break;
            case 'C':
                Out.PutNumber((tm->tm_year + 1900) / 100, 2, NoPad);
                break;
            case 'd':
                Out.PutNumber(tm->tm_mday, 2, NoPad);
                break;
            case 'D':
            case 'x':
                if (*Format == 'x' && NoPad)
                {
                    static const TChar LongDate[] = { '%', 'A', ',', ' ', '%', 'B', ' ', '%',
                                                      '#', 'd', ',', ' ', '%', 'Y', 0 };
                    _NtCRT_FormatTime(Out, LongDate, tm);
                } else
                {
                    static const TChar ShortDate[] = { '%', 'm', '/', '%', 'd', '/', '%', 'y', 0 };
                    _NtCRT_FormatTime(Out, ShortDate, tm);
                }
                break;
            case 'e':
                if (tm->tm_mday < 10 && !NoPad)
                {
                    Out.Put(' ');
                }
                Out.PutNumber(tm->tm_mday, 1, true);
                break;
            case 'F':
            {
                static const TChar IsoDate[] = { '%', 'Y', '-', '%', 'm', '-', '%', 'd', 0 };
                _NtCRT_FormatTime(Out, IsoDate, tm);
                break;
            }
            case 'g':
            case 'G':
            case 'V':
            {
                int IsoYear;
                int Week = _NtCRT_IsoWeek(tm, &IsoYear);
                if (*Format == 'V')
                {
                    Out.PutNumber(Week, 2, NoPad);
                } else if (*Format == 'G')
                {
                    Out.PutNumber(IsoYear, 4, NoPad);
                } else
                {
                    Out.PutNumber(IsoYear % 100, 2, NoPad);
                }
                break;
            }
            case 'H':
                Out.PutNumber(tm->tm_hour, 2, NoPad);
                break;
            case 'I':
                Out.PutNumber(Hour12, 2, NoPad);
                break;
            case 'j':
                Out.PutNumber(tm->tm_yday + 1, 3, NoPad);
                break;
            case 'm':
                Out.PutNumber(tm->tm_mon + 1, 2, NoPad);
                break;
            case 'M':
                Out.PutNumber(tm->tm_min, 2, NoPad);
                break;
            case 'n':
                Out.Put('\n');
                break;
            case 'p':
                Out.PutAscii(tm->tm_hour < 12 ? "AM" : "PM");
                break;
            case 'r':
            {
                static const TChar Time12[] = { '%', 'I', ':', '%', 'M', ':', '%', 'S', ' ', '%', 'p', 0 };
                _NtCRT_FormatTime(Out, Time12, tm);
                break;
            }
            case 'R':
            {
                static const TChar Time24[] = { '%', 'H', ':', '%', 'M', 0 };
                _NtCRT_FormatTime(Out, Time24, tm);
                break;
            }
            case 'S':
                Out.PutNumber(tm->tm_sec, 2, NoPad);
                break;
            case 't':
                Out.Put('\t');
                break;
            case 'T':
            case 'X':
            {
                static const TChar Time[] = { '%', 'H', ':', '%', 'M', ':', '%', 'S', 0 };
                _NtCRT_FormatTime(Out, Time, tm);
                break;
            }
            case 'u':
                Out.PutNumber(tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, NoPad);
                break;
            case 'U':
                Out.PutNumber((tm->tm_yday + 7 - tm->tm_wday) / 7, 2, NoPad);
                break;
            case 'w':
                Out.PutNumber(tm->tm_wday, 1, NoPad);
                break;
            case 'W':
                Out.PutNumber((tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, NoPad);
                break;
            case 'y':
                Out.PutNumber((tm->tm_year + 1900) % 100, 2, NoPad);
                break;
            case 'Y':
                Out.PutNumber(tm->tm_year + 1900, 4, NoPad);
                break;
            case 'z':
            {
                long Offset;
                _NtCRT_EnsureTz();
                Offset = -(_NtCRT_timezone + (tm->tm_isdst > 0 ? _NtCRT_dstbias : 0));
                Out.Put(Offset < 0 ? '-' : '+');
                if (Offset < 0)
                {
                    Offset = -Offset;
                }
                Out.PutNumber(Offset / 3600 * 100 + Offset / 60 % 60, 4, false);
                break;
            }
            case 'Z':
                _NtCRT_EnsureTz();
                if constexpr (sizeof(TChar) == sizeof(char))
                {
                    Out.PutAscii(_NtCRT_TzNameOf(tm->tm_isdst));
                } else
                {
                    for (const wchar_t* p = _NtCRT_TzNameOfW(tm->tm_isdst); *p != L'\0'; p++)
                    {
                        Out.Put(*p);
                    }
                }
                break;
            case '%':
                Out.Put('%');
                break;
            case 0:
                return;
            default:
                /* msvcrt fails on an invalid conversion */
                NTCRT_ERRNO = EINVAL;
                Out.Overflow = true;
                return;
        }
    }
}

EXTERN_C size_t __cdecl _NtCRT_strftime(char* buffer, size_t size, const char* format, const struct tm* tm)
{
    NTCRT_FORMATTER<char> Out = { buffer, size, 0, false };

    if (buffer == NULL || size == 0 || format == NULL || tm == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    _NtCRT_FormatTime(Out, format, tm);
    if (Out.Overflow)
    {
        buffer[0] = '\0';
        return 0;
    }
    buffer[Out.Used] = '\0';
    return Out.Used;
}

EXTERN_C size_t __cdecl _NtCRT_wcsftime(wchar_t* buffer, size_t size, const wchar_t* format, const struct tm* tm)
{
    NTCRT_FORMATTER<wchar_t> Out = { buffer, size, 0, false };

    if (buffer == NULL || size == 0 || format == NULL || tm == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    _NtCRT_FormatTime(Out, format, tm);
    if (Out.Overflow)
    {
        buffer[0] = L'\0';
        return 0;
    }
    buffer[Out.Used] = L'\0';
    return Out.Used;
}

EXTERN_C char* __cdecl _NtCRT__strdate(char* buffer)
{
    __time64_t Now = _NtCRT__time64(NULL);
    struct tm tm;

    _NtCRT__localtime64_s(&tm, &Now);
    _NtCRT_strftime(buffer, 9, "%m/%d/%y", &tm);
    return buffer;
}

EXTERN_C char* __cdecl _NtCRT__strtime(char* buffer)
{
    __time64_t Now = _NtCRT__time64(NULL);
    struct tm tm;

    _NtCRT__localtime64_s(&tm, &Now);
    _NtCRT_strftime(buffer, 9, "%H:%M:%S", &tm);
    return buffer;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wstrdate(wchar_t* buffer)
{
    __time64_t Now = _NtCRT__time64(NULL);
    struct tm tm;

    _NtCRT__localtime64_s(&tm, &Now);
    _NtCRT_wcsftime(buffer, 9, L"%m/%d/%y", &tm);
    return buffer;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wstrtime(wchar_t* buffer)
{
    __time64_t Now = _NtCRT__time64(NULL);
    struct tm tm;

    _NtCRT__localtime64_s(&tm, &Now);
    _NtCRT_wcsftime(buffer, 9, L"%H:%M:%S", &tm);
    return buffer;
}

/* "MM/DD/YY" and "HH:MM:SS" need 9 characters */
template<typename TChar>
static
errno_t
_NtCRT_StrTimeS(
    _Out_writes_z_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_ TChar*(__cdecl* Format)(TChar*))
{
    if (Buffer == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Buffer[0] = 0;
    if (Size < 9)
    {
        return _NtCRT_InvalidParameter(ERANGE);
    }
    Format(Buffer);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__strdate_s(char* buffer, size_t size)
{
    return _NtCRT_StrTimeS(buffer, size, _NtCRT__strdate);
}

EXTERN_C errno_t __cdecl _NtCRT__strtime_s(char* buffer, size_t size)
{
    return _NtCRT_StrTimeS(buffer, size, _NtCRT__strtime);
}

EXTERN_C errno_t __cdecl _NtCRT__wstrdate_s(wchar_t* buffer, size_t size)
{
    return _NtCRT_StrTimeS(buffer, size, _NtCRT__wstrdate);
}

EXTERN_C errno_t __cdecl _NtCRT__wstrtime_s(wchar_t* buffer, size_t size)
{
    return _NtCRT_StrTimeS(buffer, size, _NtCRT__wstrtime);
}

/* Time zone accessors */

EXTERN_C long* __cdecl _NtCRT___p__timezone(void)
{
    _NtCRT_EnsureTz();
    return &_NtCRT_timezone;
}

EXTERN_C int* __cdecl _NtCRT___p__daylight(void)
{
    _NtCRT_EnsureTz();
    return &_NtCRT_daylight;
}

EXTERN_C long* __cdecl _NtCRT___p__dstbias(void)
{
    _NtCRT_EnsureTz();
    return &_NtCRT_dstbias;
}

EXTERN_C char** __cdecl _NtCRT___p__tzname(void)
{
    _NtCRT_EnsureTz();
    return _NtCRT_tzname;
}

EXTERN_C int __cdecl _NtCRT__get_timezone(long* value)
{
    if (value == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_EnsureTz();
    *value = _NtCRT_timezone;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__get_daylight(int* value)
{
    if (value == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_EnsureTz();
    *value = _NtCRT_daylight;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__get_dstbias(long* value)
{
    if (value == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_EnsureTz();
    *value = _NtCRT_dstbias;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__get_tzname(size_t* length, char* buffer, size_t size, int index)
{
    const char* Name;
    size_t Length;

    if (length == NULL || (unsigned int)index > 1 || (buffer == NULL && size != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    _NtCRT_EnsureTz();
    Name = g_TzName[index];
    Length = strlen(Name) + 1;
    *length = Length;
    if (buffer == NULL)
    {
        return 0;
    }
    if (Length > size)
    {
        return ERANGE;
    }
    memcpy(buffer, Name, Length);
    return 0;
}

NTCRT_IMPORT(_tzset, _NtCRT__tzset);
NTCRT_IMPORT(tzset, _NtCRT__tzset);
NTCRT_IMPORT_DATA(_timezone, _NtCRT_timezone);
NTCRT_IMPORT_DATA(_daylight, _NtCRT_daylight);
NTCRT_IMPORT_DATA(_dstbias, _NtCRT_dstbias);
NTCRT_IMPORT_DATA(_tzname, _NtCRT_tzname);
NTCRT_IMPORT_DATA(timezone, _NtCRT_timezone);
NTCRT_IMPORT_DATA(daylight, _NtCRT_daylight);
NTCRT_IMPORT_DATA(tzname, _NtCRT_tzname);
NTCRT_IMPORT(__p__timezone, _NtCRT___p__timezone);
NTCRT_IMPORT(__p__daylight, _NtCRT___p__daylight);
NTCRT_IMPORT(__p__dstbias, _NtCRT___p__dstbias);
NTCRT_IMPORT(__p__tzname, _NtCRT___p__tzname);
NTCRT_IMPORT(_get_timezone, _NtCRT__get_timezone);
NTCRT_IMPORT(_get_daylight, _NtCRT__get_daylight);
NTCRT_IMPORT(_get_dstbias, _NtCRT__get_dstbias);
NTCRT_IMPORT(_get_tzname, _NtCRT__get_tzname);
NTCRT_IMPORT(_time64, _NtCRT__time64);
NTCRT_IMPORT(time, _NtCRT__time64);
NTCRT_IMPORT(_time32, _NtCRT__time32);
NTCRT_IMPORT(_ftime64, _NtCRT__ftime64);
NTCRT_IMPORT(_ftime, _NtCRT__ftime64);
NTCRT_IMPORT(_ftime64_s, _NtCRT__ftime64_s);
NTCRT_IMPORT(_ftime_s, _NtCRT__ftime64_s);
NTCRT_IMPORT(_ftime32, _NtCRT__ftime32);
NTCRT_IMPORT(_ftime32_s, _NtCRT__ftime32_s);
NTCRT_IMPORT(clock, _NtCRT_clock);
NTCRT_IMPORT(_difftime64, _NtCRT__difftime64);
NTCRT_IMPORT(difftime, _NtCRT__difftime64);
NTCRT_IMPORT(_difftime32, _NtCRT__difftime32);
NTCRT_IMPORT(_gmtime64, _NtCRT__gmtime64);
NTCRT_IMPORT(gmtime, _NtCRT__gmtime64);
NTCRT_IMPORT(_gmtime64_s, _NtCRT__gmtime64_s);
NTCRT_IMPORT(_localtime64, _NtCRT__localtime64);
NTCRT_IMPORT(localtime, _NtCRT__localtime64);
NTCRT_IMPORT(_localtime64_s, _NtCRT__localtime64_s);
NTCRT_IMPORT(_mktime64, _NtCRT__mktime64);
NTCRT_IMPORT(mktime, _NtCRT__mktime64);
NTCRT_IMPORT(_mkgmtime64, _NtCRT__mkgmtime64);
NTCRT_IMPORT(_mkgmtime, _NtCRT__mkgmtime64);
NTCRT_IMPORT(_mktime32, _NtCRT__mktime32);
NTCRT_IMPORT(_mkgmtime32, _NtCRT__mkgmtime32);
NTCRT_IMPORT(_gmtime32, _NtCRT__gmtime32);
NTCRT_IMPORT(_localtime32, _NtCRT__localtime32);
NTCRT_IMPORT(_gmtime32_s, _NtCRT__gmtime32_s);
NTCRT_IMPORT(_localtime32_s, _NtCRT__localtime32_s);
NTCRT_IMPORT(asctime, _NtCRT_asctime);
NTCRT_IMPORT(asctime_s, _NtCRT_asctime_s);
NTCRT_IMPORT(_wasctime, _NtCRT__wasctime);
NTCRT_IMPORT(_ctime64, _NtCRT__ctime64);
NTCRT_IMPORT(ctime, _NtCRT__ctime64);
NTCRT_IMPORT(_ctime32, _NtCRT__ctime32);
NTCRT_IMPORT(_wctime64, _NtCRT__wctime64);
NTCRT_IMPORT(_wctime, _NtCRT__wctime64);
NTCRT_IMPORT(_wctime32, _NtCRT__wctime32);
NTCRT_IMPORT(strftime, _NtCRT_strftime);
NTCRT_IMPORT(wcsftime, _NtCRT_wcsftime);
NTCRT_IMPORT(_strdate, _NtCRT__strdate);
NTCRT_IMPORT(_strdate_s, _NtCRT__strdate_s);
NTCRT_IMPORT(_strtime_s, _NtCRT__strtime_s);
NTCRT_IMPORT(_wstrdate_s, _NtCRT__wstrdate_s);
NTCRT_IMPORT(_wstrtime_s, _NtCRT__wstrtime_s);
NTCRT_IMPORT(_wasctime_s, _NtCRT__wasctime_s);
NTCRT_IMPORT(_ctime64_s, _NtCRT__ctime64_s);
NTCRT_IMPORT(_ctime32_s, _NtCRT__ctime32_s);
NTCRT_IMPORT(_wctime64_s, _NtCRT__wctime64_s);
NTCRT_IMPORT(_wctime32_s, _NtCRT__wctime32_s);
NTCRT_IMPORT(_strtime, _NtCRT__strtime);
NTCRT_IMPORT(_wstrdate, _NtCRT__wstrdate);
NTCRT_IMPORT(_wstrtime, _NtCRT__wstrtime);
