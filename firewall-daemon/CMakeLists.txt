#
# Generate the systemd service file:
#
#configure_file(iptracking-firewalld.service.in iptracking-firewalld.service)

#
# Target:       iptracking-firewalld
# Namespaces:   Threads, PostgreSQL
# Others:       LIBYAML_*
#
# The daemon that receives firewall block list changes.
#
add_executable(iptracking-firewalld
        iptracking-firewalld.c)
target_link_libraries(iptracking-firewalld
    PRIVATE
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
