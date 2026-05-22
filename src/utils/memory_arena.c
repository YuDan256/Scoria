#include "memory_arena.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

void arena_init(Arena* arena, size_t size) {
    arena->base = (uint8_t*)malloc(size);
    if (!arena->base) {
        fprintf(stderr, "Clades Memoriae: Failed to allocate %zu bytes for Arena.\n", size);
        exit(1);
    }
    arena->size = size;
    arena->offset = 0;
}

void* arena_alloc(Arena* arena, size_t size) {
    size_t aligned_size = ALIGN_UP(size, 8);
    if (arena->offset + aligned_size > arena->size) {
        fprintf(stderr, "Clades Memoriae: Arena out of memory! (Capacity: %zu)\n", arena->size);
        exit(1);
    }
    void* ptr = arena->base + arena->offset;
    arena->offset += aligned_size;
    memset(ptr, 0, aligned_size); // 物理清零，确保安全
    return ptr;
}

void* arena_realloc(Arena* arena, void* old_ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) return NULL;
    if (!old_ptr) return arena_alloc(arena, new_size);

    size_t aligned_old = ALIGN_UP(old_size, 8);
    size_t aligned_new = ALIGN_UP(new_size, 8);

    // 如果是最后一次分配，直接在原地址向后推进游标
    if ((uint8_t*)old_ptr + aligned_old == arena->base + arena->offset) {
        if (arena->offset - aligned_old + aligned_new > arena->size) {
            fprintf(stderr, "Clades Memoriae: Arena out of memory during realloc!\n");
            exit(1);
        }
        arena->offset = arena->offset - aligned_old + aligned_new;
        memset((uint8_t*)old_ptr + aligned_old, 0, aligned_new - aligned_old);
        return old_ptr;
    }

    // 否则，开辟新领地并转移辎重
    void* new_ptr = arena_alloc(arena, new_size);
    memcpy(new_ptr, old_ptr, old_size);
    return new_ptr;
}

void arena_free(Arena* arena) {
    if (arena->base) {
        free(arena->base);
        arena->base = NULL;
    }
    arena->size = 0;
    arena->offset = 0;
}
