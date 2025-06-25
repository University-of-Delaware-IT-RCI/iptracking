#
# Generate the systemd service file:
#
#configure_file(iptracking-firewalld.service.in iptracking-firewalld.service)

#
# Does the system have ipset dev headers present?
#
include(CheckIncludeFile)
check_include_file(libipset/session.h HAVE_IPSET_SESSION_H)
if (HAVE_IPSET_SESSION_H)
    #
    # Determine the version of the library:
    #
    find_program(IPSET_CMD ipset HINTS /sbin /usr/sbin ${IPSET_ROOT}/sbin REQUIRED)
    message(STATUS "found ipset command: ${IPSET_CMD}")
    execute_process(COMMAND ${IPSET_CMD} --version
            OUTPUT_VARIABLE ipset_version_out
            RESULT_VARIABLE ipset_version_rc)
    if ( ipset_version_rc EQUAL 0 )
        # Attempt to parse the version from the output:
        string(REGEX MATCH "v([0-9]+)\\.([0-9]+)" IPSET_VERSION_MATCH "${ipset_version_out}")
        if (IPSET_VERSION_MATCH)
            math(EXPR IPSET_VERSION "${CMAKE_MATCH_1} * 100 + ${CMAKE_MATCH_2}")
            message(STATUS "libipset version ${IPSET_VERSION} found")
            check_include_file(libipset/ipset.h HAVE_IPSET_IPSET_H)
            if (HAVE_IPSET_IPSET_H)
                message(STATUS "libipset ipset API is available.")
                #
                # Allow the user to choose to use the higher-level ipset API:
                #
                option(USE_IPSET_IPSET_API "For ipset 7.x or newer, use the ipset and not session API" OFF)
            else ()
                message(STATUS "libipset ipset API is not available.")
                #
                # The higher-level ipset API is not available:
                #
                set(USE_IPSET_IPSET_API OFF)
            endif()
        else ()
            message(FATAL_ERROR "unable to parse libipset version: ${ipset_version_out}")
        endif ()
    else ()
        message(FATAL_ERROR "unable to determine libipset version")
    endif ()
else ()
    message(FATAL_ERROR "libipset development headers (libipset/session.h) not available.")
endif ()

#
# Target:       iptracking-firewalld
# Namespaces:   Threads, PostgreSQL
# Others:       LIBYAML_*
#
# The daemon that receives firewall block list changes.
#
add_executable(iptracking-firewalld
        ipset_helper.c
        iptracking-firewalld.c)
target_compile_definitions(iptracking-firewalld PRIVATE IPSET_VERSION=${IPSET_VERSION})
if (USE_IPSET_IPSET_API)
    target_compile_definitions(iptracking-firewalld PRIVATE USE_IPSET_IPSET_API)
endif ()
target_link_libraries(iptracking-firewalld
    PRIVATE
        -lipset
        libiptracking)
# Install the executable in the sbin/ directory with owner and
# group privileges only:
install(TARGETS iptracking-firewalld
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_SBINDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)

#
# Were we asked to install the generated systemd service file?
#
#if (${SHOULD_INSTALL_SYSTEMD_SERVICE} AND NOT EXISTS "/etc/systemd/system/iptracking-pamd.service")
#    # Install the generated systemd service file in /etc/systemd/system:
#    install(FILES "${PROJECT_BINARY_DIR}/iptracking-pamd.service"
#            DESTINATION "/etc/systemd/system")
#endif ()
