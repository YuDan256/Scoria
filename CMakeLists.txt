cmake_minimum_required(VERSION 3.15) 
 
project(Scoria VERSION 1.0.0 LANGUAGES C) 
 
set(CMAKE_C_STANDARD 11) 
set(CMAKE_C_STANDARD_REQUIRED ON) 
set(CMAKE_C_EXTENSIONS OFF) 
 
if(CMAKE_C_COMPILER_ID MATCHES "MSVC") 
    add_compile_options(/W4 /WX /utf-8) 
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) 
else() 
    add_compile_options(-Wall -Wextra -Werror) 
endif() 
 
include_directories(${CMAKE_SOURCE_DIR}/src) 

# 显式列出源文件，避免使用 GLOB_RECURSE 带来的依赖追踪问题
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
    src/backend/asm_x86_64.c
    src/backend/pe_linker.c
    src/backend/pe_idata.c
    src/backend/builtins.c
    src/utils/memory_arena.c
    src/utils/logger.c
)
 
add_executable(scoria ${SCORIA_SRCS}) 
set_target_properties(scoria PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin") 
