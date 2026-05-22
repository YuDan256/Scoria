#include "memory_arena.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

static ArenaChunk* arena_chunk_create(size_t size) {
    ArenaChunk* chunk = (ArenaChunk*)malloc(sizeof(ArenaChunk));
    if (!chunk) {
        fprintf(stderr, "Clades Memoriae: Allocatio ArenaChunk defecit.\n");
        exit(1);
    }
    chunk->base = (uint8_t*)malloc(size);
    if (!chunk->base) {
        fprintf(stderr, "Clades Memoriae: Allocatio %zu octetorum pro Arena defecit.\n", size);
        exit(1);
    }
    chunk->size = size;
    chunk->offset = 0;
    chunk->next = NULL;
    return chunk;
}

void arena_init(Arena* arena, size_t size) {
    arena->default_chunk_size = size;
    arena->head = arena_chunk_create(size);
    arena->current = arena->head;
}

void* arena_alloc(Arena* arena, size_t size) {
    size_t aligned_size = ALIGN_UP(size, 8);
    ArenaChunk* chunk = arena->current;

    if (chunk->offset + aligned_size > chunk->size) {
        // 当前营地已满，开辟新营地并链接
        size_t new_size = arena->default_chunk_size;
        if (aligned_size > new_size) new_size = aligned_size;
        
        ArenaChunk* new_chunk = arena_chunk_create(new_size);
        chunk->next = new_chunk;
        arena->current = new_chunk;
        chunk = new_chunk;
    }

    void* ptr = chunk->base + chunk->offset;
    chunk->offset += aligned_size;
    memset(ptr, 0, aligned_size); // 物理清零，确保安全
    return ptr;
}

void* arena_realloc(Arena* arena, void* old_ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) return NULL;
    if (!old_ptr) return arena_alloc(arena, new_size);

    size_t aligned_old = ALIGN_UP(old_size, 8);
    size_t aligned_new = ALIGN_UP(new_size, 8);
    ArenaChunk* chunk = arena->current;

    // 如果是最后一次分配，且当前营地容量足够，直接在原地址向后推进游标
    if ((uint8_t*)old_ptr + aligned_old == chunk->base + chunk->offset) {
        if (chunk->offset - aligned_old + aligned_new <= chunk->size) {
            chunk->offset = chunk->offset - aligned_old + aligned_new;
            // 只有在扩容时才需要清零新增的物理内存，避免缩容时 size_t 下溢
            if (aligned_new > aligned_old) {
                memset((uint8_t*)old_ptr + aligned_old, 0, aligned_new - aligned_old);
            }
            return old_ptr;
        }
    }

    // 否则，开辟新领地并转移辎重
    void* new_ptr = arena_alloc(arena, new_size);
    memcpy(new_ptr, old_ptr, old_size);
    return new_ptr;
}

void arena_free(Arena* arena) {
    ArenaChunk* chunk = arena->head;
    while (chunk) {
        ArenaChunk* next = chunk->next;
        free(chunk->base);
        free(chunk);
        chunk = next;
    }
    arena->head = NULL;
    arena->current = NULL;
}
