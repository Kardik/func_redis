#
# CMakeLists.txt
#
# Original Author : Sergio Medina Toledo <lumasepa at gmail>
# https://github.com/tic-ull/func_redis
#
# Asterisk 20+ port : https://github.com/Kardik/func_redis (asterisk-20 branch)
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.

cmake_minimum_required(VERSION 3.10)

project(func_redis VERSION 2.0.0 LANGUAGES C)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(asterisk REQUIRED)

if(NOT ASTERISK_FOUND)
	message(FATAL_ERROR "Asterisk headers/modules directory not found. Install asterisk-dev (Debian/Ubuntu) or asterisk-devel (RHEL/SUSE), or pass -DASTERISK_INCLUDE_DIR=... -DASTERISK_LIBRARY_DIR=....")
endif()

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY NAMES hiredis)
if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIBRARY)
	message(FATAL_ERROR "hiredis not found. Install libhiredis-dev (Debian/Ubuntu) or hiredis-devel (RHEL/SUSE).")
endif()

add_library(func_redis SHARED src/func_redis.c)

set_target_properties(func_redis PROPERTIES
	PREFIX ""
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS ON)

target_include_directories(func_redis PRIVATE "${ASTERISK_INCLUDE_DIR}" "${HIREDIS_INCLUDE_DIR}")

target_link_libraries(func_redis PRIVATE "${HIREDIS_LIBRARY}")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(func_redis PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS func_redis DESTINATION "${ASTERISK_LIBRARY_DIR}")

# ---------------------------------------------------------------------------
# Asterisk XML documentation for out-of-tree modules.
#
# Asterisk loads function/application documentation for external modules from
# ${AST_DATA_DIR}/documentation/thirdparty/*-<lang>.xml (see xmldoc.c). Without
# it every dialplan function registration logs
# "Couldn't find function X in XML documentation" and 'core show function'
# prints no help. Generate the file from the DOCUMENTATION block and install
# it; a running Asterisk then needs 'xmldoc reload' (or a restart).
#
# The data directory differs across installs (/usr/share/asterisk on
# Debian/Ubuntu, /var/lib/asterisk on vanilla builds), so detect it by looking
# for the installed core documentation.
find_path(ASTERISK_DATA_DIR
	NAMES documentation/core-en_US.xml
	PATHS
		/usr/share/asterisk
		/usr/local/share/asterisk
		/var/lib/asterisk
		/usr/local/var/lib/asterisk
	DOC "Asterisk data directory (the one containing documentation/)")
if(NOT ASTERISK_DATA_DIR)
	set(ASTERISK_DATA_DIR "/var/lib/asterisk" CACHE PATH "Asterisk data directory (documentation/thirdparty)")
	message(WARNING "Could not locate the Asterisk data directory; installing documentation to ${ASTERISK_DATA_DIR}/documentation/thirdparty (override with -DASTERISK_DATA_DIR=...)")
endif()

find_program(AWK_EXECUTABLE NAMES awk gawk mawk)

set(ASTDOC_XML "${CMAKE_CURRENT_BINARY_DIR}/func_redis-en_US.xml")
if(AWK_EXECUTABLE)
	add_custom_command(
		OUTPUT "${ASTDOC_XML}"
		COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/build_tools/gen_xmldoc.sh"
			"${CMAKE_CURRENT_SOURCE_DIR}/src/func_redis.c" func_redis "${ASTDOC_XML}"
		DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/src/func_redis.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/build_tools/gen_xmldoc.sh"
			"${CMAKE_CURRENT_SOURCE_DIR}/build_tools/get_documentation"
		COMMENT "Generating Asterisk XML documentation (func_redis-en_US.xml)")
	add_custom_target(xmldoc ALL DEPENDS "${ASTDOC_XML}")
	install(FILES "${ASTDOC_XML}" DESTINATION "${ASTERISK_DATA_DIR}/documentation/thirdparty")
else()
	message(WARNING "awk not found: Asterisk XML documentation will not be generated")
endif()

add_custom_target(samples
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/samples/func_redis.conf.sample" /etc/asterisk/func_redis.conf)

add_custom_target(uninstall
	COMMAND rm -f "${ASTERISK_LIBRARY_DIR}/func_redis.so" /etc/asterisk/func_redis.conf "${ASTERISK_DATA_DIR}/documentation/thirdparty/func_redis-en_US.xml")

add_custom_target(security_checks COMMAND "${CMAKE_SOURCE_DIR}/build_tools/check_security.sh" "${CMAKE_SOURCE_DIR}")

add_custom_target(doc COMMAND "${CMAKE_SOURCE_DIR}/build_tools/gen_doc.sh" "${CMAKE_SOURCE_DIR}")

add_custom_target(dialplan_test COMMAND "${CMAKE_SOURCE_DIR}/build_tools/asterisk_dialplan_test.sh" "${CMAKE_SOURCE_DIR}")
