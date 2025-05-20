/*
 * iptracking
 * logging.h
 *
 * Informational messaging API.
 *
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

/*!
 * @enum logging_level
 *
 * Values that indicate the verbosity level associated with a logging
 * message.  The quietest execution should be associated with
 * logging_level_fatal and the most verbose with logging_level_debug.
 */
enum logging_level {
    logging_level_fatal = 0,
    logging_level_error,
    logging_level_warn,
    logging_level_info,
    logging_level_debug
};

/*!
 * @function logging_get_level
 *
 * Returns the current runtime logging level for this API.
 *
 * This function is thread-safe.
 */
int logging_get_level();

/*!
 * @function logging_set_level
 *
 * Sets the current runtime logging level for this API to
 * <level>.  If <level> is not a member of the logging_level
 * enumeration, the logging level is unchanged.
 *
 * This function is thread-safe.
 */
void logging_set_level(int level);

/*!
 * @function logging_printf
 *
 * If the <level> is less than or equal to the current logging
 * level for this API, use the <__format> string and additional
 * arguments to write a message to stdout.
 *
 * The message will be formatted as:
 *
 *     [YYYY-MM-DD HH:MM:SS] <level-str> (<pid>)  <message>
 *
 * This function is thread-safe.
 */
void logging_printf(int level, const char  *__restrict __format, ...);

/*!
 * @defined FATAL
 *
 * Wrapper to logging_printf() with a level of logging_level_fatal.
 */
#define FATAL(FMT,...) logging_printf(logging_level_fatal, FMT, ##__VA_ARGS__)
/*!
 * @defined ERROR
 *
 * Wrapper to logging_printf() with a level of logging_level_error.
 */
#define ERROR(FMT,...) logging_printf(logging_level_error, FMT, ##__VA_ARGS__)
/*!
 * @defined WARN
 *
 * Wrapper to logging_printf() with a level of logging_level_warn.
 */
#define WARN(FMT,...) logging_printf(logging_level_warn, FMT, ##__VA_ARGS__)
/*!
 * @defined INFO
 *
 * Wrapper to logging_printf() with a level of logging_level_info.
 */
#define INFO(FMT,...) logging_printf(logging_level_info, FMT, ##__VA_ARGS__)
/*!
 * @defined DEBUG
 *
 * Wrapper to logging_printf() with a level of logging_level_debug.
 */
#define DEBUG(FMT,...) logging_printf(logging_level_debug, FMT, ##__VA_ARGS__)

#endif /* __LOGGING_H__ */
