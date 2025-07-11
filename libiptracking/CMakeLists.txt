# Look for local modules in this path:
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Default location of the daemon configuration file:
#
set(CONFIGURATION_FILEPATH_DEFAULT "${CMAKE_INSTALL_FULL_SYSCONFDIR}/iptracking.yml" CACHE PATH "Default configuration file path")

#
# By default we don't install the library, since it's primarily only of
# use internally to this project.  But we're willing if the user wants to...
#
option(SHOULD_INSTALL_LIBTRACKING On "Install the libtracking library and headers")

#
# Enable if the generated daemon configuration file should be installed
# during `make install`:
#
option(SHOULD_INSTALL_CONFIG_TEMPLATE Off "Install a template configuration file")

#
# Enable if the generated systemd service files should be installed during
# `make install`:
#
option(SHOULD_INSTALL_SYSTEMD_SERVICES Off "Install systemd services for the daemons")

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
# Socket API tunables:
#
set(SOCKET_DEFAULT_BACKLOG "5" CACHE STRING "Socket listen connection backlog (see 'man 3 listen')")
set(SOCKET_DEFAULT_POLL_INTERVAL "90" CACHE STRING "Socket connection-polling timeout in seconds (see 'man 3 poll')")

#
# Firewall update interval:
#
set(FIREWALLD_CHECK_INTERVAL_DEFAULT "300" CACHE STRING "Maximum interval between ipset updates")
set(FIREWALLD_IPSET_NAME_PRODUCTION_DEFAULT "iptracking_block" CACHE STRING "Name of ipset referenced by filtering rules")
set(FIREWALLD_IPSET_NAME_REBUILD_DEFAULT "iptracking_block_update" CACHE STRING "Name of ipset for building updates")

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
# Find a Mysql client package:
#
option(ENABLE_MYSQL_DRIVER Off "Include the Mysql database driver")
if (ENABLE_MYSQL_DRIVER)
    set(HAVE_MYSQL On)
    include(FindMySQL)
endif ()

#
# We want to use asprintf()
#
include(CheckSymbolExists)

# Check for asprintf without any specific definitions
check_symbol_exists(asprintf "stdio.h;stdlib.h" HAVE_ASPRINTF)

# If asprintf is not found, try with _GNU_SOURCE
if(NOT HAVE_ASPRINTF)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(asprintf "stdio.h;stdlib.h" HAVE_ASPRINTF_GNU_SOURCE)
    unset(CMAKE_REQUIRED_DEFINITIONS) # Unset for subsequent checks
endif()

# Report the result
if (HAVE_ASPRINTF)
    message(STATUS "asprintf found.")
elseif (HAVE_ASPRINTF_GNU_SOURCE)
    message(STATUS "asprintf found with _GNU_SOURCE.")
else ()
    message(STATUS "asprintf not found.")
endif ()

#
# Generate the shared header file with version info, other compile-time option
# values, and all of the headers we'll include in general:
#
configure_file(iptracking.h.in iptracking.h)

#
# Generate the example daemon configuration file:
#
configure_file(iptracking.yml.in iptracking.yml)

#
# Target:       libiptracking
# Namespaces:   Threads, PostgreSQL, MySQL
# Others:       LIBYAML_*
#
# Functionality held in common between various components of this project.
#
set(LIBIPTRACKING_HEADERS
        db_interface.h
        logging.h
        yaml_helpers.h
        log_data.h
        chartest.h
        "${CMAKE_CURRENT_BINARY_DIR}/iptracking.h")
set(LIBIPTRACKING_SOURCES
        logging.c
        chartest.c
        log_data.c
        yaml_helpers.c
        db_interface.c)
if (NOT HAVE_ASPRINTF AND NOT HAVE_ASPRINTF_GNU_SOURCE)
    list(LIBIPTRACKING_SOURCES APPEND asprintf.c)
endif ()
add_library(libiptracking STATIC ${LIBIPTRACKING_SOURCES})
set_target_properties(libiptracking
        PROPERTIES ARCHIVE_OUTPUT_NAME "iptracking")
set_target_properties(libiptracking
        PROPERTIES PUBLIC_HEADER "${LIBIPTRACKING_HEADERS}")
target_include_directories(libiptracking
    PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${LIBYAML_INCLUDE_DIR}")
target_link_libraries(libiptracking
    PUBLIC
        Threads::Threads
        "${LIBYAML_LIBRARY}")
if (HAVE_POSTGRESQL)
    target_link_libraries(libiptracking PUBLIC PostgreSQL::PostgreSQL)
endif ()
if (HAVE_SQLITE3)
    target_link_libraries(libiptracking PUBLIC SQLite::SQLite3)
endif ()
if (HAVE_MYSQL)
    target_link_libraries(libiptracking PUBLIC MySQL::MySQL)
endif ()
# Install the executable in the sbin/ directory with owner and
# group privileges only:
if ( ${SHOULD_INSTALL_LIBTRACKING} )
    install(TARGETS libiptracking
            PUBLIC_HEADER
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            ARCHIVE
                DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                            GROUP_READ             GROUP_EXECUTE)
endif ()


#
# Were we asked to install the generated daemon configuration file?
#
if (${SHOULD_INSTALL_CONFIG_TEMPLATE} AND NOT EXISTS "${CONFIGURATION_FILEPATH_DEFAULT}")
    # Get the directory and filename at which we want to install the daemon
    # configuration file:
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT PARENT_PATH CONFIGURATION_FILEPATH)
    cmake_path(GET CONFIGURATION_FILEPATH_DEFAULT FILENAME CONFIGURATION_FILENAME)
    # Install the generated daemon configuration file:
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iptracking.yml"
            DESTINATION "${CONFIGURATION_FILEPATH}"
            RENAME "${CONFIGURATION_FILENAME}"
            PERMISSIONS OWNER_READ OWNER_WRITE
                        GROUP_READ GROUP_WRITE)
endif ()
