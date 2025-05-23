#
# CMake project definition for the iptracking project.
#
# On each official release the version in the project() directive
# should be updated.  The generated header file will then be
# affected and the two executables will compile with the version
# present.
#
cmake_minimum_required(VERSION 3.20)
project(iptracking-daemon
        VERSION 0.0.1
        DESCRIPTION "A multithreaded message-processing queue for PAM SSH events"
        LANGUAGES C)

#
# If no install path was explicitly provided, override the default /usr/local
# with the root directory (so that /etc, /var/run, /sbin are used):
#
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/" CACHE PATH "Installation prefix path" FORCE)
endif ()

#
# This is Linux-only software, so use the GNU directory layout:
#
include(GNUInstallDirs)

#
# Default location of the daemon configuration file:
#
set(CONFIGURATION_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_SYSCONFDIR}/iptracking.yml" CACHE PATH "Default configuration file path")

#
# Enable if the generated daemon configuration file should be installed
# during `make install`:
#
option(SHOULD_INSTALL_CONFIG_TEMPLATE Off "Install a template configuration file")

#
# Enable if the generated systemd service file should be installed during
# `make install`:
#
option(SHOULD_INSTALL_SYSTEMD_SERVICE Off "Install a systemd service for the daemon")

#
# These options control the initial size and growth of the pool of logging
# records available to the daemon.  The record is designed to be 128 bytes in
# size, and Linux likes 4 KiB pages, so 32 records fit in a page.
#
set(LOG_POOL_RECORDS_MIN "32" CACHE STRING "Minimum number of logging records available to queue")
set(LOG_POOL_RECORDS_MAX "0" CACHE STRING "Maximum number of logging records available to queue")
set(LOG_POOL_RECORDS_DELTA "32" CACHE STRING "Number of logging records in each queue capacity expansion")

#
# These options control the timing of wait periods that the daemon may experience
# if the record pool is saturated and cannot grow any larger.  The sleep time starts
# at LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN and after a number of retries fail the
# sleep time is increased by LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT; repeat.  Once the
# time has reached or exceeded LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX it no longer
# extends any more.
#
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MIN "5" CACHE STRING "Minimum seconds wait between failed logging record alloc ")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_MAX "600" CACHE STRING "Maximum seconds wait between failed logging record alloc")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT_THRESH "4" CACHE STRING "Number of failed allocs before increasing wait time")
set(LOG_POOL_DEFAULT_PUSH_WAIT_SECONDS_DT "5" CACHE STRING "Seconds to increase wait time after threshold")

#
# Path to the socket file that the daemon will monitor and to which the callback program
# will write events:
#
set(SOCKET_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_RUNSTATEDIR}/iptracking.s" CACHE PATH "Default socket path")

#
# Find a threading package -- we demand pthreads!
#
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#
# Find a libyaml package, using libyaml_ROOT as a hint if it exists:
#
if (DEFINED libyaml_ROOT)
    find_path(LIBYAML_INCLUDE_DIR yaml.h HINTS "${libyaml_ROOT}/include" REQUIRED)
    find_library(LIBYAML_LIBRARY yaml HINTS "${libyaml_ROOT}/lib" "${libyaml_ROOT}/lib64" REQUIRED)
else ()
    find_path(LIBYAML_INCLUDE_DIR yaml.h REQUIRED)
    find_library(LIBYAML_LIBRARY yaml REQUIRED)
endif ()

#
# Find a PostgreSQL package at least 15.x or newer:
#
option(ENABLE_POSTGRESQL_DRIVER Off "Include the PostgreSQL database driver")
if (ENABLE_POSTGRESQL_DRIVER)
    set(HAVE_POSTGRESQL On)
    find_package(PostgreSQL REQUIRED)
    if (PostgreSQL_VERSION_STRING VERSION_LESS "15")
        message(FATAL_ERROR "PostgreSQL 15 of newer is required, found ${PostgreSQL_VERSION_STRING}")
    endif ()
endif ()

#
# Find an SQLite3 package:
#
option(ENABLE_SQLITE3_DRIVER Off "Include the SQLite3 database driver")
if (ENABLE_SQLITE3_DRIVER)
    set(HAVE_SQLITE3 On)
    find_package(SQLite3 REQUIRED)
endif ()

#
# Generate the shared header file with version info, other compile-time option
# values, and all of the headers we'll include in general:
#
configure_file(iptracking-daemon.h.in iptracking-daemon.h)

#
# Generate the example daemon configuration file:
#
configure_file(iptracking.yml.in iptracking.yml)

#
# Generate the systemd service file:
#
configure_file(iptracking-daemon.service.in iptracking-daemon.service)


#
# Target:       iptracking-daemon
# Namespaces:   Threads, PostgreSQL
# Others:       LIBYAML_*
#
# The daemon that receives PAM events and injects the into the
# database.
#
add_executable(iptracking-daemon
        chartest.c
        logging.c
        log_queue.c
        yaml_helpers.c
        db_interface.c
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
        "${LIBYAML_LIBRARY}")
if (HAVE_POSTGRESQL)
    target_include_directories(iptracking-daemon PUBLIC PostgreSQL::PostgreSQL)
    target_link_libraries(iptracking-daemon PRIVATE PostgreSQL::PostgreSQL)
endif ()
if (HAVE_SQLITE3)
    target_include_directories(iptracking-daemon PUBLIC SQLite::SQLite3)
    target_link_libraries(iptracking-daemon PRIVATE SQLite::SQLite3)
endif ()
# Install the executable in the sbin/ directory with owner and
# group privileges only:
install(TARGETS iptracking-daemon
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_SBINDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)


#
# Target:       iptracking-pam-callback
# Namespaces:   
# Others:       
#
# The helper program executed by PAM.
#
add_executable(iptracking-pam-callback
        iptracking-pam-callback.c)
target_include_directories(iptracking-pam-callback
    PUBLIC
        "${PROJECT_BINARY_DIR}")
# Install the executable in the libexec/ directory with owner and
# group privileges only:
install(TARGETS iptracking-pam-callback
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)


#
# Were we asked to install the generated daemon configuration file?
#
if (${SHOULD_INSTALL_CONFIG_TEMPLATE} AND NOT EXISTS "${CONFIGURATION_FILEPATH_DEFAULT}")
    # Get the directory and filename at which we want to install the daemon
    # configuration file:
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT PARENT_PATH CONFIGURATION_FILEPATH)
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT FILENAME CONFIGURATION_FILENAME)
    # Install the generated daemon configuration file:
    install(FILES "${PROJECT_BINARY_DIR}/iptracking.yml"
            DESTINATION "${CONFIGURATION_FILEPATH}"
            RENAME "${CONFIGURATION_FILENAME}"
            PERMISSIONS OWNER_READ OWNER_WRITE
                        GROUP_READ GROUP_WRITE)
endif ()


#
# Were we asked to install the generated systemd service file?
#
if (${SHOULD_INSTALL_SYSTEMD_SERVICE} AND NOT EXISTS "/etc/systemd/system/iptracking-daemon.service")
    # Install the generated systemd service file in /etc/systemd/system:
    install(FILES "${PROJECT_BINARY_DIR}/iptracking-daemon.service"
            DESTINATION "/etc/systemd/system")
endif ()
