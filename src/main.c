#include <stdio.h>
#include <stdlib.h>
#include "frontend/parser.h"
#include "middleend/type_checker.h"
#include "ir/ir_gen.h"
#include "ir/ir_printer.h"
#include "backend/pe_linker.h"
#include "backend/asm_x86_64.h"
#include "utils/logger.h"

// 声明前端 API (假设它们在 parser.h 中)
AstNode* parse_program(Parser* parser);
void parser_free(Parser* parser);

// 读取整个文件内容到内存
static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Erratum: Non possum aperire fasciculum '%s'.\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Erratum: Memoria non sufficit.\n");
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

int main(int argc, char** argv) {
    logger_init();
    LOG_INFO("Roma Invicta! Cor compilatoris Scoriae spirat.");

    if (argc < 2) {
        LOG_ERROR("Nullus fasciculus datus est.");
        printf("Usus: scoria <fasciculus.sco>\n");
        return 1;
    }

    const char* source_path = argv[1];
    char* source = read_file(source_path);
    if (!source) {
        return 1;
    }

    LOG_INFO("--- [1] Fons (Source): %s ---", source_path);

    // 1. 前端：语法分析 (Syntax Analysis)
    Parser parser;
    parser_init(&parser, source);
    AstNode* program = parse_program(&parser);
    
    if (parser.had_error) {
        LOG_ERROR("Erratum in syntaxi. Compilatio terminata.");
        parser_free(&parser);
        return 1;
    }
    LOG_INFO("--- [2] Syntaxis (AST) OK ---");

    // 2. 中端：类型检查与语义分析 (Semantic Analysis)
    types_init(); // 初始化类型系统单例
    TypeChecker checker;
    type_checker_init(&checker);
    
    if (!type_checker_run(&checker, program)) {
        LOG_ERROR("Erratum in typis. Compilatio terminata.");
        type_checker_free(&checker);
        parser_free(&parser);
        return 1;
    }
    LOG_INFO("--- [3] Semantica (Type Check) OK ---");

    // 3. 后端：IR 生成 (IR Generation)
    IrBuilder builder;
    ir_builder_init(&builder, "TestModule");
    ir_gen_generate(&builder, program);
    
    LOG_INFO("--- [4] SIR (Scoria IR) ---");
    sir_print_module(stdout, builder.module);
    printf("\n");

    // 4. 后端：生成汇编代码 (Assembly Generation)
    LOG_INFO("--- [5] Assembly (x86_64) ---");
    asm_x86_64_generate(stdout, builder.module);
    printf("\n");

    // 5. 生成 Windows 原生可执行文件 (.exe)
    PeLinker pe_linker;
    pe_linker_init(&pe_linker);
    if (pe_linker_generate_executable(&pe_linker, builder.module, "scoria_out.exe")) {
        LOG_INFO("--- [6] Executable Generated: scoria_out.exe ---");
        LOG_INFO("Potes nunc currere: scoria_out.exe");
    } else {
        LOG_ERROR("Erratum in generatione PE.");
    }
    pe_linker_free(&pe_linker);

    // 6. 清理战场 (Cleanup)
    ir_builder_free(&builder);
    type_checker_free(&checker);
    parser_free(&parser);
    free(source);

    return 0;
}
