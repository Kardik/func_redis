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

add_custom_target(samples
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/samples/func_redis.conf.sample" /etc/asterisk/func_redis.conf)

add_custom_target(uninstall
	COMMAND rm -f "${ASTERISK_LIBRARY_DIR}/func_redis.so" /etc/asterisk/func_redis.conf)

add_custom_target(security_checks COMMAND "${CMAKE_SOURCE_DIR}/build_tools/check_security.sh" "${CMAKE_SOURCE_DIR}")

add_custom_target(doc COMMAND "${CMAKE_SOURCE_DIR}/build_tools/gen_doc.sh" "${CMAKE_SOURCE_DIR}")

add_custom_target(dialplan_test COMMAND "${CMAKE_SOURCE_DIR}/build_tools/asterisk_dialplan_test.sh" "${CMAKE_SOURCE_DIR}")
