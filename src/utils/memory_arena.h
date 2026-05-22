#ifndef SCORIA_MEMORY_ARENA_H
#define SCORIA_MEMORY_ARENA_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 幽灵后勤大营：巨型连续内存池 (Bump Allocator)
 */
typedef struct {
    uint8_t* base;
    size_t size;
    size_t offset;
} Arena;

/**
 * @brief 建立营地，一次性拨付巨量物理内存
 */
void arena_init(Arena* arena, size_t size);

/**
 * @brief 单向推进分配内存，绝不回头
 */
void* arena_alloc(Arena* arena, size_t size);

/**
 * @brief 重新分配内存 (如果是最后一次分配则直接推进，否则新开辟并拷贝)
 */
void* arena_realloc(Arena* arena, void* old_ptr, size_t old_size, size_t new_size);

/**
 * @brief 掀桌子：拔营并归还所有内存给操作系统
 */
void arena_free(Arena* arena);

#endif // SCORIA_MEMORY_ARENA_H
