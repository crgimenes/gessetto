/* Declarations for the freestanding wasm32 build: the memory functions the
   engine calls, which the embedding host defines. */
#ifndef GESSO_FREESTANDING_STRING_H
#define GESSO_FREESTANDING_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#endif
