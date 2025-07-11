
if(NOT MySQL_CONFIG_EXECUTABLE)
    set(MySQL_CONFIG_EXECUTABLE "mysql_config")
endif ()
macro(mysql_get_conf _VARNAME _OPTNAME)
  execute_process(
    COMMAND ${MySQL_CONFIG_EXECUTABLE} ${_OPTNAME}
    OUTPUT_VARIABLE ${_VARNAME}
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endmacro ()

#
# Get the header and library paths:
#
mysql_get_conf(MySQL_INCLUDE_DIR "--variable=pkgincludedir")
mysql_get_conf(MySQL_LIB_DIR "--variable=pkglibdir")

#
# Get the list of libraries to link against:
#
mysql_get_conf(MySQL_LIBRARIES_RAW "--libs")
string(REPLACE " " ";" MySQL_LIBRARIES_RAW_LIST "${MySQL_LIBRARIES_RAW}")
set(MySQL_LIBRARIES "")
foreach(_lib IN LISTS MySQL_LIBRARIES_RAW_LIST)
    string(FIND "${_lib}" "-L" DASH_L_IDX)
    if ( NOT ${DASH_L_IDX} EQUAL 0 )
        string(REGEX MATCH "^-l.*(mysql|mariadb).*$" _lib_capture "${_lib}")
        if ( _lib_capture )
            string(SUBSTRING "${_lib_capture}" 2 -1 _lib_capture)
            list(APPEND MySQL_LIBRARIES "${_lib_capture}")
        endif ()
    endif ()
endforeach ()
list(LENGTH MySQL_LIBRARIES MySQL_LIBRARIES_LEN)
if ( "${MySQL_LIBRARIES_LEN}" EQUAL 0 )
    message(FATAL_ERROR "MySQL libraries not found.")
endif ()

set(MySQL_INCLUDE_DIRS ${MySQL_INCLUDE_DIR})
set(MySQL_LIB_DIRS ${MySQL_LIB_DIR})

function(__mysql_import_library _target _var _config)
  if(_config)
    set(_config_suffix "_${_config}")
  else()
    set(_config_suffix "")
  endif()

  set(_lib "${${_var}${_config_suffix}}")
  if(EXISTS "${_lib}")
    if(_config)
      set_property(TARGET ${_target} APPEND PROPERTY
        IMPORTED_CONFIGURATIONS ${_config})
    endif()
    set_target_properties(${_target} PROPERTIES
      IMPORTED_LOCATION${_config_suffix} "${_lib}")
  endif()
endfunction()

if (NOT TARGET MySQL::MySQL)
    add_library(MySQL::MySQL INTERFACE IMPORTED)
    set_target_properties(MySQL::MySQL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MySQL_INCLUDE_DIRS}"
        INTERFACE_LINK_DIRECTORIES "${MySQL_LIB_DIRS}")
    __mysql_import_library(MySQL::MySQL MySQL_LIBRARY "")
    __mysql_import_library(MySQL::MySQL MySQL_LIBRARY "RELEASE")
    __mysql_import_library(MySQL::MySQL MySQL_LIBRARY "DEBUG")
    set_property(TARGET MySQL::MySQL APPEND PROPERTY IMPORTED_CONFIGURATIONS "RELEASE" "DEBUG")
    set_target_properties(MySQL::MySQL PROPERTIES
        IMPORTED_LIBNAME "${MySQL_LIBRARIES}"
        IMPORTED_LIBNAME "${MySQL_LIBRARIES}"
        IMPORTED_LIBNAME "${MySQL_LIBRARIES}")
    message(NOTICE "-- Found MySQL header path: ${MySQL_INCLUDE_DIRS}")
    message(NOTICE "-- Found MySQL library path: ${MySQL_LIB_DIRS}")
    message(NOTICE "-- Found MySQL libraries: ${MySQL_LIBRARIES}")
endif ()

