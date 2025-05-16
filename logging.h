
#ifndef __LOGGING_H__
#define __LOGGING_H__

enum logging_level_e {
    logging_level_fatal = 0,
    logging_level_error,
    logging_level_warn,
    logging_level_info,
    logging_level_debug
};

int logging_get_level();
void logging_set_level(int level);
void logging_printf(int level, const char  *__restrict __format, ...);

#define FATAL(FMT,...) logging_printf(logging_level_fatal, FMT, ##__VA_ARGS__)
#define ERROR(FMT,...) logging_printf(logging_level_error, FMT, ##__VA_ARGS__)
#define WARN(FMT,...) logging_printf(logging_level_warn, FMT, ##__VA_ARGS__)
#define INFO(FMT,...) logging_printf(logging_level_info, FMT, ##__VA_ARGS__)
#define DEBUG(FMT,...) logging_printf(logging_level_debug, FMT, ##__VA_ARGS__)

#endif
