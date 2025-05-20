cmake_minimum_required(VERSION 3.20)

project(iptracking-daemon
        VERSION 0.0.1
        DESCRIPTION "A multithreaded message-processing queue for SSH connections"
        LANGUAGES C)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/" CACHE PATH "Installation prefix path" FORCE)
endif ()

include(GNUInstallDirs)

set(CONFIGURATION_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_SYSCONFDIR}/iptracking.yml" CACHE PATH "Default configuration file path")

option(SHOULD_INSTALL_CONFIG_TEMPLATE Off "Install a template configuration file")

option(SHOULD_INSTALL_SYSTEMD_SERVICE Off "Install a systemd service for the daemon")

set(LOG_POOL_RECORDS_MIN "32" CACHE STRING "Minimum number of logging records available to queue")
set(LOG_POOL_RECORDS_MAX "0" CACHE STRING "Maximum number of logging records available to queue")
set(LOG_POOL_RECORDS_DELTA "32" CACHE STRING "Number of logging records in each queue capacity expansion")

set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN "5" CACHE STRING "Minimum seconds wait between failed logging record alloc ")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX "600" CACHE STRING "Maximum seconds wait between failed logging record alloc")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH "4" CACHE STRING "Number of failed allocs before increasing wait time")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT "5" CACHE STRING "Seconds to increase wait time after threshold")

set(DB_SCHEMA_DEFAULT "" CACHE STRING "Default database schema name for the iptracking tables/views/functions")

set(FIFO_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/iptracking.fifo" CACHE PATH "Default named pipe path")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (DEFINED libyaml_ROOT)
    find_path(LIBYAML_INCLUDE_DIR yaml.h HINTS "${libyaml_ROOT}/include" REQUIRED)
    find_library(LIBYAML_LIBRARY yaml HINTS "${libyaml_ROOT}/lib" "${libyaml_ROOT}/lib64" REQUIRED)
else ()
    find_path(LIBYAML_INCLUDE_DIR yaml.h REQUIRED)
    find_library(LIBYAML_LIBRARY yaml REQUIRED)
endif ()

find_package(PostgreSQL REQUIRED)
if (PostgreSQL_VERSION_STRING VERSION_LESS "15")
    message(FATAL_ERROR "PostgreSQL 15 of newer is required, found ${PostgreSQL_VERSION_STRING}")
endif ()

configure_file(iptracking-daemon.h.in iptracking-daemon.h)
configure_file(iptracking.yml.in iptracking.yml)

add_executable(iptracking-daemon
        logging.c
        log_queue.c
        yaml-helpers.c
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
if (${SHOULD_INSTALL_CONFIG_TEMPLATE} AND NOT EXISTS "${CONFIGURATION_FILEPATH_DEFAULT}")
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT PARENT_PATH CONFIGURATION_FILEPATH)
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT FILENAME CONFIGURATION_FILENAME)
    install(FILES "${PROJECT_BINARY_DIR}/iptracking.yml"
            DESTINATION "${CONFIGURATION_FILEPATH}"
            RENAME "${CONFIGURATION_FILENAME}"
            PERMISSIONS OWNER_READ OWNER_WRITE
                        GROUP_READ GROUP_WRITE)
endif ()
install(TARGETS iptracking-daemon
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_SBINDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)

if (${SHOULD_INSTALL_SYSTEMD_SERVICE} AND NOT EXISTS "/etc/systemd/system/iptracking-daemon.service")
    configure_file(iptracking-daemon.service.in iptracking-daemon.service)
    install(FILES "${PROJECT_BINARY_DIR}/iptracking-daemon.service"
            DESTINATION "/etc/systemd/system")
endif ()

add_executable(iptracking-pam-callback
        iptracking-pam-callback.c)
target_include_directories(iptracking-pam-callback
    PUBLIC
        "${PROJECT_BINARY_DIR}")
install(TARGETS iptracking-pam-callback
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)
