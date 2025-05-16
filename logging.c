
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

//

#include "logging.h"

//

static const char* logging_level_strs[] = {
        "FATAL",
        "ERROR",
        "WARN ",
        "INFO ",
        "DEBUG",
        NULL
    };

static int logging_level = logging_level_error;

static pthread_mutex_t logging_lock = PTHREAD_MUTEX_INITIALIZER;

//

int
logging_get_level()
{
    int     lvl;
    
    pthread_mutex_lock(&logging_lock);
    lvl = logging_level;
    pthread_mutex_unlock(&logging_lock);
    return lvl;
}

//
    
void
logging_set_level(
    int     level
)
{
    pthread_mutex_lock(&logging_lock);
    if ( level < logging_level_fatal ) level = logging_level_fatal;
    else if ( level > logging_level_debug ) level = logging_level_debug;
    logging_level = level;
    pthread_mutex_unlock(&logging_lock);
}

//

void
logging_printf(
    int         level,
    const char  *__restrict __format,
    ...
)
{
    if ( level <= logging_level ) {
        va_list     argv;
        time_t      now_t;
        struct tm   now_tm;
        char        now_s[24];
        
        pthread_mutex_lock(&logging_lock);
        
        now_t = time(NULL);
        localtime_r(&now_t, &now_tm);
        strftime(now_s, sizeof(now_s), "%Y-%m-%d %H:%M:%S", &now_tm);
        
        va_start(argv, __format);
        fprintf(stderr, "[%s] %s (%d)  ", now_s, logging_level_strs[level], getpid());
        vfprintf(stderr, __format, argv);
        fprintf(stderr, "\n");
        va_end(argv);
        
        pthread_mutex_unlock(&logging_lock);
    }
    if ( level == logging_level_fatal ) exit(errno);
}
