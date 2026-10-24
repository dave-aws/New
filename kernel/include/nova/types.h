/* NOVA OS — base types and compiler helpers. */
#ifndef NOVA_TYPES_H
#define NOVA_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef intptr_t  iptr;
typedef uintptr_t uptr;

#define NULL ((void *)0)
#define noreturn __attribute__((noreturn))
#define packed   __attribute__((packed))
#define aligned(n) __attribute__((aligned(n)))
#define unused   __attribute__((unused))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX(v, lo), hi)

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((u64)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((u64)(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define HUGE_PAGE_SIZE (2UL * 1024 * 1024)

#define NOVA_MAGIC 0x4E4F5641u /* 'NOVA' */

static inline u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

#endif /* NOVA_TYPES_H */
