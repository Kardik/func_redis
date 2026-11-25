#
# Findasterisk.cmake
#
# Original Author : Sergio Medina Toledo <lumasepa at gmail>
# https://github.com/tic-ull/func_redis
#
# Asterisk 20+ port : https://github.com/Kardik/func_redis (asterisk-20 branch)
#
# This program is free software, distributed under the terms of
# the GNU General Public License version 2. See the LICENSE file
# at the top of the source tree.

# Locate Asterisk headers and the modules directory.
# This module defines:
#  ASTERISK_INCLUDE_DIR - where to find the Asterisk includes
#  ASTERISK_LIBRARY_DIR - the Asterisk modules install directory
#
# Note: Asterisk modules are not linked against a libasterisk; only the
# header location and the module install directory are needed.

set(ASTERISK_FOUND "NO")

find_path(ASTERISK_INCLUDE_DIR asterisk/version.h
	HINTS
	PATH_SUFFIXES include
	PATHS
	/usr/local/include
	/usr/include
	/opt/include
)

find_path(ASTERISK_LIBRARY_DIR app_dial.so
	HINTS
	PATH_SUFFIXES modules
	PATHS
	/usr/lib/asterisk
	/usr/lib64/asterisk
	/usr/lib/x86_64-linux-gnu/asterisk
	/usr/lib/aarch64-linux-gnu/asterisk
	/usr/local/lib/asterisk
	/usr/local/lib64/asterisk
)

if(ASTERISK_INCLUDE_DIR AND ASTERISK_LIBRARY_DIR)
	set(ASTERISK_FOUND "YES")
elseif(ASTERISK_INCLUDE_DIR)
	# Only the development headers are installed (e.g. a build container with
	# asterisk-dev but no asterisk package), so no module directory with
	# app_dial.so exists to detect. Fall back to the standard module
	# directory layout of the platform.
	set(ASTERISK_FOUND "YES")
	if(EXISTS "/usr/lib/x86_64-linux-gnu")
		set(ASTERISK_LIBRARY_DIR "/usr/lib/x86_64-linux-gnu/asterisk/modules")
	elseif(EXISTS "/usr/lib64")
		set(ASTERISK_LIBRARY_DIR "/usr/lib64/asterisk/modules")
	else()
		set(ASTERISK_LIBRARY_DIR "/usr/lib/asterisk/modules")
	endif()
	message(STATUS "Asterisk modules directory not found; defaulting to ${ASTERISK_LIBRARY_DIR} (override with -DASTERISK_LIBRARY_DIR=...)")
endif()
