#pragma once
/* Native ARM64 SACX ICU contract. No POSIX, dynamic library or file service is
 * exposed to this library. ICU must use registered in-memory Unicode data.
 * UTF-8 is the native byte encoding; target wchar_t holds UTF-32 code units. */
#if !defined(__DIHOS__) || !defined(__aarch64__) || !defined(__ELF__)
#error This ICU profile is for the native DihOS ARM64 target
#endif
#if __SIZEOF_WCHAR_T__ != 4 || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Revalidate the ICU data and wchar_t ABI for this target
#endif
#define U_PLATFORM 0
#define U_PLATFORM_IMPLEMENTS_POSIX 0
#define U_POSIX_LOCALE 0
#define U_HAVE_TZSET 0
#define U_HAVE_TIMEZONE 0
#define U_HAVE_TZNAME 0
#define U_HAVE_NL_LANGINFO_CODESET 0
#define U_HAVE_NL_LANGINFO 0
#define U_WCHAR_IS_UTF32 1
#define U_CHARSET_IS_UTF8 1
#define UCONFIG_NO_FILE_IO 1
#define U_ENABLE_DYLOAD 0
#define U_STATIC_IMPLEMENTATION 1
