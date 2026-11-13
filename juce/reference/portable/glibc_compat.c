/*  Portable Linux release builds (cmake -DALBION_PORTABLE_LINUX=ON).

    Building on a brand-new distro binds a few libc/libm functions to brand-new
    symbol versions (e.g. atan2f@GLIBC_2.43, the C23 __isoc23_strtol family),
    which stops the plugin loading on older distributions. With the linker's
    --wrap, every reference to those functions comes here instead, and we call
    the long-standing versions that every x86-64 glibc provides.

    Result: binaries need glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 and newer).
*/
#include <stdarg.h>
#include <stdint.h>
#include <sys/random.h>

#define ALBION_OLD(sym, alias, version) __asm__ (".symver " #alias "," #sym "@" version)

float  albion_atan2f (float, float);              ALBION_OLD (atan2f, albion_atan2f, "GLIBC_2.2.5");
float  albion_log10f (float);                     ALBION_OLD (log10f, albion_log10f, "GLIBC_2.2.5");
float  albion_sqrtf (float);                      ALBION_OLD (sqrtf,  albion_sqrtf,  "GLIBC_2.2.5");
double albion_fmod (double, double);              ALBION_OLD (fmod,   albion_fmod,   "GLIBC_2.2.5");
long      albion_strtol (const char*, char**, int);   ALBION_OLD (strtol,  albion_strtol,  "GLIBC_2.2.5");
long long albion_strtoll (const char*, char**, int);  ALBION_OLD (strtoll, albion_strtoll, "GLIBC_2.2.5");
unsigned long      albion_strtoul (const char*, char**, int);   ALBION_OLD (strtoul,  albion_strtoul,  "GLIBC_2.2.5");
unsigned long long albion_strtoull (const char*, char**, int);  ALBION_OLD (strtoull, albion_strtoull, "GLIBC_2.2.5");
int albion_vsscanf (const char*, const char*, va_list);         ALBION_OLD (__isoc99_vsscanf, albion_vsscanf, "GLIBC_2.7");

float  __wrap_atan2f (float y, float x)  { return albion_atan2f (y, x); }
float  __wrap_log10f (float x)           { return albion_log10f (x); }
float  __wrap_sqrtf (float x)            { return albion_sqrtf (x); }
double __wrap_fmod (double x, double y)  { return albion_fmod (x, y); }

long      __wrap___isoc23_strtol (const char* s, char** e, int b)   { return albion_strtol (s, e, b); }
long long __wrap___isoc23_strtoll (const char* s, char** e, int b)  { return albion_strtoll (s, e, b); }
unsigned long      __wrap___isoc23_strtoul (const char* s, char** e, int b)   { return albion_strtoul (s, e, b); }
unsigned long long __wrap___isoc23_strtoull (const char* s, char** e, int b)  { return albion_strtoull (s, e, b); }

int __wrap___isoc23_sscanf (const char* s, const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    const int r = albion_vsscanf (s, fmt, args);
    va_end (args);
    return r;
}

int __wrap___isoc23_vsscanf (const char* s, const char* fmt, va_list args)
{
    return albion_vsscanf (s, fmt, args);
}

/* libstdc++'s std::random_device uses arc4random (glibc 2.36); getrandom has
   existed since glibc 2.25. */
uint32_t __wrap_arc4random (void)
{
    uint32_t v = 0;
    while (getrandom (&v, sizeof v, 0) != (ssize_t) sizeof v) {}
    return v;
}
