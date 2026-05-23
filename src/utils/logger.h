#ifndef SCORIA_LOGGER_H
#define SCORIA_LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

// 初始化日志系统
void logger_init(void);

// 设置日志级别
void logger_set_level(LogLevel level);

// 打印日志
void log_msg(LogLevel level, const char* format, ...);

#define LOG_INFO(...)  log_msg(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_ERROR, __VA_ARGS__)
#define LOG_DEBUG(...) log_msg(LOG_DEBUG, __VA_ARGS__)

#endif // SCORIA_LOGGER_H
