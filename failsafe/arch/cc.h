#ifndef __CC_H__
#define __CC_H__

#include <malloc.h>

extern int printf(const char *fmt, ...);
extern void hang(void) __attribute__((noreturn));
extern unsigned long get_timer(unsigned long base);

#ifdef ERR_OK
#undef ERR_OK
#endif

#define LWIP_NO_STDINT_H                1
#define LWIP_NO_STDDEF_H                1
#define LWIP_NO_INTTYPES_H              1

typedef unsigned char u8_t;
typedef signed char s8_t;
typedef unsigned short u16_t;
typedef signed short s16_t;
typedef unsigned int u32_t;
typedef signed int s32_t;
typedef unsigned long long u64_t;
typedef signed long long s64_t;
typedef unsigned long mem_ptr_t;

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "lu"

extern long simple_strtol(const char *cp, char **endp, unsigned int base);
static inline int lwip_atoi(const char *str)
{
	return (int)simple_strtol(str, ((void *)0), 10);
}
#define atoi lwip_atoi

#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(message) do { printf("lwIP assert: %s\n", message); hang(); } while(0)

#define LWIP_RAND() ((u32_t)get_timer(0))

#ifndef NULL
#define NULL ((void *)0)
#endif

#define LWIP_NO_CTYPE_H 1

#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\v' || (c) == '\f')
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isupper(c) ((c) >= 'A' && (c) <= 'Z')
#define islower(c) ((c) >= 'a' && (c) <= 'z')
#define tolower(c) (isupper(c) ? (c) - 'A' + 'a' : (c))
#define toupper(c) (islower(c) ? (c) - 'a' + 'A' : (c))

#define LWIP_PROVIDE_ERRNO 1

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#ifndef EALREADY
#define EALREADY 114
#endif
#ifndef EISCONN
#define EISCONN 106
#endif
#ifndef ENOTCONN
#define ENOTCONN 107
#endif

#ifndef htons
#define htons(x) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#endif
#ifndef ntohs
#define ntohs(x) htons(x)
#endif
#ifndef htonl
#define htonl(x) ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | (((x) >> 8) & 0xff00) | (((x) >> 24) & 0xff))
#endif
#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

#endif