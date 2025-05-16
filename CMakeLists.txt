cmake_minimum_required(VERSION 3.20)

project(iptracking-daemon
        VERSION 1.2.3
        DESCRIPTION "A multithreaded message-processing queue for SSH connections"
        LANGUAGES C)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/" CACHE PATH "Installation prefix path" FORCE)
endif ()

include(GNUInstallDirs)

set(CONFIGURATION_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_SYSCONFDIR}/iptracking.yml" CACHE PATH "Default configuration file path")

set(LOG_POOL_RECORDS_MIN "32" CACHE STRING "Minimum number of logging records available to queue")
set(LOG_POOL_RECORDS_MAX "0" CACHE STRING "Maximum number of logging records available to queue")
set(LOG_POOL_RECORDS_DELTA "32" CACHE STRING "Number of logging records in each queue capacity expansion")

set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN "5" CACHE STRING "Minimum seconds wait between failed logging record alloc ")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX "600" CACHE STRING "Maximum seconds wait between failed logging record alloc")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH "4" CACHE STRING "Number of failed allocs before increasing wait time")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT "5" CACHE STRING "Seconds to increase wait time after threshold")

set(PIPE_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/iptracking.fifo" CACHE PATH "Default named pipe path")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_path(LIBYAML_INCLUDE_DIR yaml.h REQUIRED)
find_library(LIBYAML_LIBRARY yaml REQUIRED)

find_package(PostgreSQL REQUIRED)
if (PostgreSQL_VERSION_STRING VERSION_LESS "15")
    message(FATAL_ERROR "PostgreSQL 15 of newer is required, found ${PostgreSQL_VERSION_STRING}")
endif ()

configure_file(iptracking-daemon.h.in iptracking-daemon.h)

add_executable(iptracking-daemon
        logging.c
        log_queue.c
        iptracking-daemon.c)
target_include_directories(iptracking-daemon
    PUBLIC
        "${PROJECT_BINARY_DIR}"
        Threads::Threads
        PostgreSQL::PostgreSQL
        "${LIBYAML_INCLUDE_DIR}")
target_link_libraries(iptracking-daemon
    PRIVATE
        Threads::Threads
        PostgreSQL::PostgreSQL
        "${LIBYAML_LIBRARY}")
