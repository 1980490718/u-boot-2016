#ifndef __LWIP_STDLIB_H__
#define __LWIP_STDLIB_H__

#include <malloc.h>
#include <vsprintf.h>

static inline int atoi(const char *str)
{
	return (int)simple_strtol(str, NULL, 10);
}

#endif