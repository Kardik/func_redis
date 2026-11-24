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
endif()
