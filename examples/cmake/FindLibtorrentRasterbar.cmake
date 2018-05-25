# - Try to find libtorrent-rasterbar
#
# This module tries to locate libtorrent-rasterbar Config.cmake files and uses pkg-config if available
# and the config file could not be found.
# If that does not work, you can pre-set LibtorrentRasterbar_CUSTOM_DEFINITIONS
# for definitions unrelated to Boost's separate compilation (which are already
# decided by the LibtorrentRasterbar_USE_STATIC_LIBS variable).
#
# Once done this will define
#  LibtorrentRasterbar_FOUND - System has libtorrent-rasterbar
#  LibtorrentRasterbar_INCLUDE_DIRS - The libtorrent-rasterbar include directories
#  LibtorrentRasterbar_LIBRARIES - The libraries needed to use libtorrent-rasterbar
#  LibtorrentRasterbar_DEFINITIONS - Compiler switches required for using libtorrent-rasterbar
#  LibtorrentRasterbar_OPENSSL_ENABLED - libtorrent-rasterbar uses and links against OpenSSL
#  LibtorrentRasterbar::torrent-rasterbar imported target will be created

# Let's begin with the config mode

set(_exactKeyword "")
if (${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_EXACT})
	set(_exactKeyword "EXACT")
endif()

find_package(LibtorrentRasterbar ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} ${_exactKeyword} CONFIG)

if (LibtorrentRasterbar_FOUND)
	if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
		message(STATUS "LibtorrentRasterbar package found in ${LibtorrentRasterbar_DIR}")
		message(STATUS "LibtorrentRasterbar version: ${LibtorrentRasterbar_VERSION}")
	endif()
	# Extract target properties into this module variables
	get_target_property(LibtorrentRasterbar_INCLUDE_DIRS LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
	get_target_property(LibtorrentRasterbar_LIBRARIES LibtorrentRasterbar::torrent-rasterbar IMPORTED_LOCATION)
	get_target_property(_iface_link_libs LibtorrentRasterbar::torrent-rasterbar INTERFACE_LINK_LIBRARIES)
	list(APPEND LibtorrentRasterbar_LIBRARIES ${_iface_link_libs})
	get_target_property(LibtorrentRasterbar_DEFINITIONS LibtorrentRasterbar::torrent-rasterbar INTERFACE_COMPILE_DEFINITIONS)
	get_target_property(_iface_compile_options LibtorrentRasterbar::torrent-rasterbar INTERFACE_COMPILE_OPTIONS)
	list(APPEND LibtorrentRasterbar_DEFINITIONS ${_iface_compile_options})
	list(FIND _iface_link_libs "OpenSSL::SSL" _openssl_lib_index)
	if (_openssl_lib_index GREATER -1)
		set(LibtorrentRasterbar_OPENSSL_ENABLED TRUE)
	else()
		set(LibtorrentRasterbar_OPENSSL_ENABLED FALSE)
	endif()
else()
	find_package(Threads QUIET REQUIRED)
	find_package(PkgConfig QUIET)

	if(PKG_CONFIG_FOUND)
		pkg_check_modules(PC_LIBTORRENT_RASTERBAR QUIET libtorrent-rasterbar)
	endif()

	if(LibtorrentRasterbar_USE_STATIC_LIBS)
		set(LibtorrentRasterbar_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
		set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
	endif()

	if(PC_LIBTORRENT_RASTERBAR_FOUND)
		set(LibtorrentRasterbar_DEFINITIONS ${PC_LIBTORRENT_RASTERBAR_CFLAGS_OTHER})
	else()
		if(LibtorrentRasterbar_CUSTOM_DEFINITIONS)
			set(LibtorrentRasterbar_DEFINITIONS ${LibtorrentRasterbar_CUSTOM_DEFINITIONS})
		else()
			# Without pkg-config, we can't possibly figure out the correct build flags.
			# libtorrent is very picky about those. Let's take a set of defaults and
			# hope that they apply. If not, you the user are on your own.
			set(LibtorrentRasterbar_DEFINITIONS
				-DTORRENT_USE_OPENSSL
				-DTORRENT_DISABLE_GEO_IP
				-DBOOST_ASIO_ENABLE_CANCELIO
				-D_FILE_OFFSET_BITS=64)
		endif()

		if(NOT LibtorrentRasterbar_USE_STATIC_LIBS)
			list(APPEND LibtorrentRasterbar_DEFINITIONS
				-DTORRENT_LINKING_SHARED
				-DBOOST_SYSTEM_DYN_LINK)
		endif()
	endif()

	if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
		message(STATUS "libtorrent definitions: ${LibtorrentRasterbar_DEFINITIONS}")
	endif()

	find_path(LibtorrentRasterbar_INCLUDE_DIR libtorrent
			HINTS ${PC_LIBTORRENT_RASTERBAR_INCLUDEDIR} ${PC_LIBTORRENT_RASTERBAR_INCLUDE_DIRS}
			PATH_SUFFIXES libtorrent-rasterbar)

	find_library(LibtorrentRasterbar_LIBRARY NAMES torrent-rasterbar
				HINTS ${PC_LIBTORRENT_RASTERBAR_LIBDIR} ${PC_LIBTORRENT_RASTERBAR_LIBRARY_DIRS})

	if(LibtorrentRasterbar_USE_STATIC_LIBS)
		set(CMAKE_FIND_LIBRARY_SUFFIXES ${LibtorrentRasterbar_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
	endif()

	set(LibtorrentRasterbar_LIBRARIES ${LibtorrentRasterbar_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
	set(LibtorrentRasterbar_INCLUDE_DIRS ${LibtorrentRasterbar_INCLUDE_DIR})

	if(NOT Boost_SYSTEM_FOUND)
		find_package(Boost QUIET REQUIRED COMPONENTS system)
		set(LibtorrentRasterbar_LIBRARIES
			${LibtorrentRasterbar_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
		set(LibtorrentRasterbar_INCLUDE_DIRS
			${LibtorrentRasterbar_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
	endif()

	list(FIND LibtorrentRasterbar_DEFINITIONS -DTORRENT_USE_OPENSSL LibtorrentRasterbar_ENCRYPTION_INDEX)
	if(LibtorrentRasterbar_ENCRYPTION_INDEX GREATER -1)
		find_package(OpenSSL QUIET REQUIRED)
		set(LibtorrentRasterbar_LIBRARIES ${LibtorrentRasterbar_LIBRARIES} ${OPENSSL_LIBRARIES})
		set(LibtorrentRasterbar_INCLUDE_DIRS ${LibtorrentRasterbar_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})
		set(LibtorrentRasterbar_OPENSSL_ENABLED ON)
	endif()

	include(FindPackageHandleStandardArgs)
	# handle the QUIETLY and REQUIRED arguments and set LibtorrentRasterbar_FOUND to TRUE
	# if all listed variables are TRUE
	find_package_handle_standard_args(LibtorrentRasterbar DEFAULT_MSG
									LibtorrentRasterbar_LIBRARY
									LibtorrentRasterbar_INCLUDE_DIR
									Boost_SYSTEM_FOUND
	)

	mark_as_advanced(LibtorrentRasterbar_INCLUDE_DIR LibtorrentRasterbar_LIBRARY
		LibtorrentRasterbar_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES
		LibtorrentRasterbar_ENCRYPTION_INDEX)

	if (LibtorrentRasterbar_FOUND AND NOT TARGET LibtorrentRasterbar::torrent-rasterbar)
		add_library(LibtorrentRasterbar::torrent-rasterbar SHARED IMPORTED)

		# LibtorrentRasterbar_DEFINITIONS var contains a mix of -D, -f, and possible -std options
		# let's split them into definitions and options (that are not definitions)
		set(LibtorrentRasterbar_defines "${LibtorrentRasterbar_DEFINITIONS}")
		set(LibtorrentRasterbar_options "${LibtorrentRasterbar_DEFINITIONS}")
		list(FILTER LibtorrentRasterbar_defines INCLUDE REGEX "(^|;)-D.+")
		list(FILTER LibtorrentRasterbar_options EXCLUDE REGEX "(^|;)-D.+")
		# remove '-D' from LibtorrentRasterbar_defines
		string(REGEX REPLACE "(^|;)(-D)" "\\1" LibtorrentRasterbar_defines "${LibtorrentRasterbar_defines}")

		set_target_properties(LibtorrentRasterbar::torrent-rasterbar PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
			IMPORTED_LOCATION "${LibtorrentRasterbar_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${LibtorrentRasterbar_INCLUDE_DIRS}"
			INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${LibtorrentRasterbar_INCLUDE_DIRS}"
			INTERFACE_LINK_LIBRARIES "${LibtorrentRasterbar_LIBRARIES}"
			INTERFACE_COMPILE_DEFINITIONS "${LibtorrentRasterbar_defines}"
			INTERFACE_COMPILE_OPTIONS "${LibtorrentRasterbar_options}"
		)
	endif()
endif()
