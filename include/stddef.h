#ifndef STDDEF_H
#define STDDEF_H

typedef long ptrdiff_t;
#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ unsigned long
#endif
typedef __SIZE_TYPE__ size_t;
/* There is a GCC macro for a size_t type, but not
 * for a ssize_t type. Below construct tricks GCC
 * into making __SIZE_TYPE__ signed.
#define unsigned signed
typedef __SIZE_TYPE__ ssize_t;
#undef unsigned
 */

#define NULL ((unsigned long)0)

/* Provide a pointer to address 0 that thwarts any "accessing this is
 * undefined behaviour and do whatever" trickery in compilers.
 * Use when you _really_ need to read32(zeroptr) (ie. read address 0).
 */
extern char zeroptr[];

#endif /* STDDEF_H */
