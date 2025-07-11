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
        VERSION 0.1.0
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
# Configure the library of common components:
#
add_subdirectory(libiptracking)

#
# Configure the PAM daemon:
#
add_subdirectory(pam-daemon)

#
# Configure the firewall daemon:
#
add_subdirectory(firewall-daemon)
