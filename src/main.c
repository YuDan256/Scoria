#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    const char* source_path = NULL;
    const char* output_path = "scoria_out.exe";
    bool emit_ir = false;
    bool emit_asm = false;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--emitte-ir") == 0) {
            emit_ir = true;
        } else if (strcmp(argv[i], "--emitte-asm") == 0) {
            emit_asm = true;
        } else if (strcmp(argv[i], "--verbosus") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (argv[i][0] != '-') {
            source_path = argv[i];
        } else {
            LOG_ERROR("Argumentum ignotum: %s", argv[i]);
            printf("Usus: scoria <fasciculus.sco> [-o <opus.exe>] [--emitte-ir] [--emitte-asm] [--verbosus]\n");
            return 1;
        }
    }

    logger_set_level(verbose ? LOG_INFO : LOG_WARN);
    LOG_INFO("Incipit compilatio Scoriae. Roma Invicta.");

    if (!source_path) {
        LOG_ERROR("Nullus fasciculus datus est.");
        printf("Usus: scoria <fasciculus.sco> [-o <opus.exe>] [--emitte-ir] [--emitte-asm] [--verbosus]\n");
        return 1;
    }

    char* source = read_file(source_path);
    if (!source) {
        return 1;
    }

    LOG_INFO("[I] Lectio fontis: %s", source_path);

    // 1. 前端：语法分析 (Syntax Analysis)
    Parser parser;
    parser_init(&parser, source);
    AstNode* program = parse_program(&parser);
    
    if (parser.had_error) {
        LOG_ERROR("Vitia syntactica inventa sunt. Processus abortus est.");
        parser_free(&parser);
        return 1;
    }
    LOG_INFO("[II] Analysis syntactica perfecta est.");

    // 2. 中端：类型检查与语义分析 (Semantic Analysis)
    types_init(); // 初始化类型系统单例
    TypeChecker checker;
    type_checker_init(&checker);
    
    if (!type_checker_run(&checker, program)) {
        LOG_ERROR("Vitia semantica inventa sunt. Processus abortus est.");
        type_checker_free(&checker);
        parser_free(&parser);
        return 1;
    }
    LOG_INFO("[III] Probatio typorum et semantica perfecta est.");

    // 3. 后端：IR 生成 (IR Generation)
    IrBuilder builder;
    ir_builder_init(&builder, "TestModule");
    ir_gen_generate(&builder, program);
    
    if (emit_ir) {
        LOG_INFO("[IV] Generatio Repraesentationis Intermediae (SIR):");
        sir_print_module(stdout, builder.module);
        printf("\n");
    }

    // 4. 后端：生成汇编代码 (Assembly Generation)
    if (emit_asm) {
        LOG_INFO("[V] Generatio codicis machinalis (x86_64):");
        asm_x86_64_generate(stdout, builder.module);
        printf("\n");
    }

    // 5. 生成 Windows 原生可执行文件 (.exe)
    PeLinker pe_linker;
    pe_linker_init(&pe_linker);
    if (pe_linker_generate_executable(&pe_linker, builder.module, output_path)) {
        LOG_INFO("[VI] Opus perfectum est: %s", output_path);
        LOG_INFO("Feliciter. Imperium exspectat: %s", output_path);
    } else {
        LOG_ERROR("Defectus in generatione formae PE.");
    }
    pe_linker_free(&pe_linker);

    // 6. 清理战场 (Cleanup)
    ir_builder_free(&builder);
    type_checker_free(&checker);
    parser_free(&parser);
    free(source);

    return 0;
}
