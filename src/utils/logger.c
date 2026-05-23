#include "logger.h"
#include <stdarg.h>

static LogLevel g_log_level = LOG_WARN; // 默认只显示 WARN 和 ERROR

void logger_init(void) {
    // 预留给未来的日志系统初始化逻辑
}

void logger_set_level(LogLevel level) {
    g_log_level = level;
}

void log_msg(LogLevel level, const char* format, ...) {
    if (level < g_log_level) return;

    switch (level) {
        case LOG_INFO:  printf("[INFO] "); break;
        case LOG_WARN:  printf("[WARN] "); break;
        case LOG_ERROR: printf("[ERROR] "); break;
        case LOG_DEBUG: printf("[DEBUG] "); break;
    }

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}
