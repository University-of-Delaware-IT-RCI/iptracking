#
# Generate the systemd service file:
#
configure_file(iptracking-pamd.service.in iptracking-pamd.service)

#
# Target:       iptracking-pamd
# Namespaces:   Threads, PostgreSQL
# Others:       LIBYAML_*
#
# The daemon that receives PAM events and injects the into the
# database.
#
add_executable(iptracking-pamd
        chartest.c
        log_queue.c
        iptracking-pamd.c)
target_link_libraries(iptracking-pamd
    PRIVATE
        libiptracking)
# Install the executable in the sbin/ directory with owner and
# group privileges only:
install(TARGETS iptracking-pamd
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
get_target_property(LIBIPTRACKING_INCLUDE_DIRS libiptracking INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(iptracking-pam-callback
    PRIVATE
        ${LIBIPTRACKING_INCLUDE_DIRS})
# Install the executable in the libexec/ directory with owner and
# group privileges only:
install(TARGETS iptracking-pam-callback
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ             GROUP_EXECUTE)


#
# Were we asked to install the generated systemd service file?
#
if (${SHOULD_INSTALL_SYSTEMD_SERVICE} AND NOT EXISTS "/etc/systemd/system/iptracking-pamd.service")
    # Install the generated systemd service file in /etc/systemd/system:
    install(FILES "${PROJECT_BINARY_DIR}/iptracking-pamd.service"
            DESTINATION "/etc/systemd/system")
endif ()
