cmake_minimum_required(VERSION 3.15) 

project(Scoria VERSION 1.0.0 LANGUAGES C) 
 
set(CMAKE_C_STANDARD 11) 
set(CMAKE_C_STANDARD_REQUIRED ON) 
set(CMAKE_C_EXTENSIONS OFF) 

# =========================================================================
# 显式列出源文件，避免使用 GLOB_RECURSE 带来的依赖追踪问题
# =========================================================================
set(SCORIA_SRCS
    src/main.c
    src/frontend/lexer.c
    src/frontend/parser.c
    src/middleend/types.c
    src/middleend/symtab.c
    src/middleend/type_checker.c
    src/ir/ir_builder.c
    src/ir/ir_gen.c
    src/ir/ir_printer.c
    src/backend/reg_alloc.c
    src/backend/x86_mir.c
    src/backend/asm_x86_64.c
    src/backend/pe_linker.c
    src/backend/pe_idata.c
    src/backend/builtins.c
    src/utils/memory_arena.c
    src/utils/logger.c
)
 
add_executable(scoria ${SCORIA_SRCS}) 
target_include_directories(scoria PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(scoria PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# =========================================================================
# 编译器火力配置与“反潜雷达”（强制 Debug 符号生成）
# =========================================================================
if(CMAKE_C_COMPILER_ID MATCHES "MSVC") 
    # 全局严格警告
    target_compile_options(scoria PRIVATE /W4 /WX /utf-8) 
    target_compile_definitions(scoria PRIVATE _CRT_SECURE_NO_WARNINGS) 
    
    # 【核心防御】：如果是 Debug 模式，强行注入以下魔法！
    # /Zi: 生成完整 PDB 调试信息数据库
    # /FS: 强迫同步写入 PDB（防止并行编译时 PDB 损坏丢失文件映射）
    # /Od: 强制关闭所有 C 编译器优化，防止内联导致下断点位置乱飘
    target_compile_options(scoria PRIVATE 
        $<$<CONFIG:Debug>:/Zi> 
        $<$<CONFIG:Debug>:/FS> 
        $<$<CONFIG:Debug>:/Od>
    )
    
    # 强令链接器必须输出 DEBUG 完整映射表
    target_link_options(scoria PRIVATE 
        $<$<CONFIG:Debug>:/DEBUG:FULL>
    )

else() 
    # 针对 GCC/Clang 环境的同等级雷达
    target_compile_options(scoria PRIVATE -Wall -Wextra -Werror) 
    target_compile_options(scoria PRIVATE 
        $<$<CONFIG:Debug>:-g> 
        $<$<CONFIG:Debug>:-O0>
    )
endif()
