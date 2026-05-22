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
file(GLOB_RECURSE SCORIA_SRCS "${CMAKE_SOURCE_DIR}/src/*.c") 
 
add_executable(scoria ${SCORIA_SRCS}) 
set_target_properties(scoria PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin") 
