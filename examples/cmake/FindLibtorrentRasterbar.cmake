# - Try to find libtorrent-rasterbar
#
# This module tries to locate libtorrent-rasterbar Config.cmake files and fallbacks to pkg-config.
# If that does not work, you can pre-set LibtorrentRasterbar_CUSTOM_DEFINITIONS
# for definitions unrelated to Boost's separate compilation (which are already
# decided by the LibtorrentRasterbar_USE_STATIC_LIBS variable).
#
# Once done this will define
#  LibtorrentRasterbar_FOUND - System has libtorrent-rasterbar
#  LibtorrentRasterbar_OPENSSL_ENABLED - libtorrent-rasterbar uses and links against OpenSSL
#  LibtorrentRasterbar::torrent-rasterbar imported target will be created

function(_try_config_mode)
	set(_exactKeyword "")
	if (${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_EXACT})
		set(_exactKeyword "EXACT")
	endif()

	find_package(LibtorrentRasterbar ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} ${_exactKeyword} CONFIG)

	if (LibtorrentRasterbar_FOUND)
		if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
			message(STATUS "${CMAKE_FIND_PACKAGE_NAME} package found in ${LibtorrentRasterbar_DIR}")
			message(STATUS "${CMAKE_FIND_PACKAGE_NAME} version: ${LibtorrentRasterbar_VERSION}")
		endif()
		# Extract target properties into this module variables
		get_target_property(_iface_link_libs LibtorrentRasterbar::torrent-rasterbar INTERFACE_LINK_LIBRARIES)
		list(FIND _iface_link_libs "OpenSSL::SSL" _openssl_lib_index)
		if (_openssl_lib_index GREATER -1)
			set(LibtorrentRasterbar_OPENSSL_ENABLED TRUE PARENT_SCOPE)
		else()
			set(LibtorrentRasterbar_OPENSSL_ENABLED FALSE PARENT_SCOPE)
		endif()
	endif()
endfunction()

function(_try_pkgconfig_mode)
	if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
		set(_quietKeyword "QUIET")
	endif()
	find_package(Threads ${_quietKeyword} REQUIRED)
	find_package(PkgConfig ${_quietKeyword})
	if(PKG_CONFIG_FOUND)
		set(_moduleSpec "libtorrent-rasterbar")
		if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION)
			if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_EXACT)
				set(_moduleSpec "${_moduleSpec}=${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION}")
			else()
				set(_moduleSpec "${_moduleSpec}>=${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION}")
			endif()
		endif()

		pkg_check_modules(PC_LIBTORRENT_RASTERBAR ${_quietKeyword} IMPORTED_TARGET GLOBAL ${_moduleSpec})
		if (PC_LIBTORRENT_RASTERBAR_FOUND)
			add_library(LibtorrentRasterbar::torrent-rasterbar ALIAS PkgConfig::PC_LIBTORRENT_RASTERBAR)
			list(FIND PC_LIBTORRENT_RASTERBAR_LIBRARIES "ssl" _openssl_lib_index)
			if (_openssl_lib_index GREATER -1)
				set(LibtorrentRasterbar_OPENSSL_ENABLED TRUE PARENT_SCOPE)
			else()
				set(LibtorrentRasterbar_OPENSSL_ENABLED FALSE PARENT_SCOPE)
			endif()
			set(LibtorrentRasterbar_FOUND TRUE PARENT_SCOPE)
		else()
			set(LibtorrentRasterbar_FOUND FALSE PARENT_SCOPE)
		endif()
	endif()
endfunction()

function(_try_generic_mode)
	if(LibtorrentRasterbar_USE_STATIC_LIBS)
		set(LibtorrentRasterbar_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
		set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
	endif()
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

	find_path(LibtorrentRasterbar_INCLUDE_DIR libtorrent
			HINTS ${PC_LIBTORRENT_RASTERBAR_INCLUDEDIR} ${PC_LIBTORRENT_RASTERBAR_INCLUDE_DIRS}
			PATH_SUFFIXES libtorrent-rasterbar)

	find_library(LibtorrentRasterbar_LIBRARY NAMES torrent-rasterbar
				HINTS ${PC_LIBTORRENT_RASTERBAR_LIBDIR} ${PC_LIBTORRENT_RASTERBAR_LIBRARY_DIRS})

	if(LibtorrentRasterbar_USE_STATIC_LIBS)
		set(CMAKE_FIND_LIBRARY_SUFFIXES ${LibtorrentRasterbar_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
	endif()

	if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
		message(STATUS "${CMAKE_FIND_PACKAGE_NAME} definitions: ${LibtorrentRasterbar_DEFINITIONS}")
		if (LibtorrentRasterbar_INCLUDE_DIR)
			message(STATUS "${CMAKE_FIND_PACKAGE_NAME} include dir: ${LibtorrentRasterbar_INCLUDE_DIR}")
		endif()
		if (LibtorrentRasterbar_LIBRARY)
			message(STATUS "${CMAKE_FIND_PACKAGE_NAME} library: ${LibtorrentRasterbar_LIBRARY}")
		endif()
	endif()

	mark_as_advanced(LibtorrentRasterbar_LIBRARY LibtorrentRasterbar_INCLUDE_DIR)

	if(NOT LibtorrentRasterbar_LIBRARY OR NOT LibtorrentRasterbar_INCLUDE_DIR)
		set(LibtorrentRasterbar_FOUND FALSE PARENT_SCOPE)
		return()
	endif()

	set(LibtorrentRasterbar_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

	find_package(Boost QUIET REQUIRED)
	if (Boost_MAJOR_VERSION LESS_EQUAL 1 AND Boost_MINOR_VERSION LESS 69)
		if (NOT Boost_SYSTEM_FOUND)
			find_package(Boost QUIET REQUIRED COMPONENTS system)
		endif()
		list(APPEND LibtorrentRasterbar_LIBRARIES Boost::system)
	endif()

	list(FIND LibtorrentRasterbar_DEFINITIONS -DTORRENT_USE_OPENSSL _ENCRYPTION_INDEX)
	if(_ENCRYPTION_INDEX GREATER -1)
		find_package(OpenSSL QUIET REQUIRED)
		list(APPEND LibtorrentRasterbar_LIBRARIES OpenSSL::SSL)
		if (LibtorrentRasterbar_USE_STATIC_LIBS)
			list(APPEND LibtorrentRasterbar_LIBRARIES OpenSSL::Crypto)
		endif()
		set(LibtorrentRasterbar_OPENSSL_ENABLED ON PARENT_SCOPE)
	else()
		set(LibtorrentRasterbar_OPENSSL_ENABLED OFF PARENT_SCOPE)
	endif()

	set(LibtorrentRasterbar_FOUND TRUE PARENT_SCOPE)

	if (NOT TARGET LibtorrentRasterbar::torrent-rasterbar)
		if (LibtorrentRasterbar_USE_STATIC_LIBS)
			set(_libType "STATIC")
		else()
			set(_libType "SHARED")
		endif()

		add_library(LibtorrentRasterbar::torrent-rasterbar ${_libType} IMPORTED)

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
endfunction()

if (NOT LibtorrentRasterbar_FOUND)
	_try_config_mode()
endif()

if (NOT LibtorrentRasterbar_FOUND)
	_try_pkgconfig_mode()
endif()

if (NOT LibtorrentRasterbar_FOUND)
	_try_generic_mode()
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LibtorrentRasterbar_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibtorrentRasterbar DEFAULT_MSG LibtorrentRasterbar_FOUND)
