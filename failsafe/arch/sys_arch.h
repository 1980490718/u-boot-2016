#ifndef __SYS_ARCH_H__
#define __SYS_ARCH_H__

#define sys_arch_protect(lev) (lev = 0)
#define sys_arch_unprotect(lev) ((void)(lev))
typedef int sys_prot_t;

#endif