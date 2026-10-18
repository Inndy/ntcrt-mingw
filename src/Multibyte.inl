/* The state of the multibyte characters and the UTF-8 codec, see Multibyte.cpp and MultibyteChar.cpp */

#pragma once

/*
 * mbstate_t is an int in MinGW-w64 headers:
 *   bits 0 - 20  : the value decoded so far, or a pending surrogate
 *   bits 21 - 22 : continuation bytes still needed
 *   bits 23 - 24 : length of the sequence - 1
 *   bit 25       : the high surrogate of the sequence was returned
 *   bit 26       : a low surrogate is pending for mbrtoc16
 *   bit 27       : a high surrogate is pending for wcrtomb and c16rtomb
 */
#define NTCRT_MBS_VALUE(s)    ((s) & 0x1FFFFF)
#define NTCRT_MBS_NEED(s)     (((s) >> 21) & 3)
#define NTCRT_MBS_LENGTH(s)   ((((s) >> 23) & 3) + 1)
#define NTCRT_MBS_HIGH_DONE   (1u << 25)
#define NTCRT_MBS_LOW_PENDING (1u << 26)
#define NTCRT_MBS_HIGH_SAVED  (1u << 27)
#define NTCRT_MBS(Value, Need, Length, Flags) \
    ((Value) | ((unsigned int)(Need) << 21) | ((unsigned int)(Length) - 1) << 23 | (Flags))

#define NTCRT_ERROR      ((size_t)-1)
#define NTCRT_INCOMPLETE ((size_t)-2)
#define NTCRT_PENDING    ((size_t)-3)

/* The codec of MultibyteChar.cpp */
extern "C" size_t _NtCRT_EncodingError(_Inout_ mbstate_t* State);
extern "C" size_t _NtCRT_Utf8Decode(_Out_ char32_t* Char, _In_reads_(Count) const unsigned char* Bytes,
                                    _In_ size_t Count, _Inout_ mbstate_t* State, _In_ BOOL Split);
extern "C" size_t _NtCRT_Utf8Encode(_Out_writes_(4) char* Bytes, _In_ char32_t Char);
extern "C" size_t _NtCRT_MbrToWc(_In_ UINT CodePage, _Out_opt_ wchar_t* pwc, _In_reads_bytes_opt_(n) const char* s,
                                 _In_ size_t n, _Inout_ mbstate_t* ps);
extern "C" size_t _NtCRT_WcrToMb(_In_ UINT CodePage, _Out_writes_opt_(MB_LEN_MAX) char* s, _In_ wchar_t wc,
                                 _Inout_ mbstate_t* ps);
