cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)

project(libtorrent
	DESCRIPTION "Bittorrent library"
	VERSION 1.1.10
)

set (SOVERSION "9")

list(APPEND CMAKE_MODULE_PATH ${libtorrent_SOURCE_DIR}/cmake/Modules)
include(FeatureSummary)
include(GNUInstallDirs)
include(GeneratePkgConfig)

set(sources
	web_connection_base
	alert
	alert_manager
	allocator
	announce_entry
	assert
	bandwidth_limit
	bandwidth_manager
	bandwidth_queue_entry
	bdecode
	bitfield
	block_cache
	bloom_filter
	chained_buffer
	choker
	close_reason
	cpuid
	crc32c
	create_torrent
	disk_buffer_holder
	entry
	error_code
	file_storage
	file_progress
	lazy_bdecode
	escape_string
	string_util
	file
	fingerprint
	gzip
	hasher
	hex
	http_connection
	http_stream
	http_parser
	i2p_stream
	identify_client
	ip_filter
	ip_voter
	performance_counters
	peer_class
	peer_class_set
	peer_connection
	bt_peer_connection
	web_peer_connection
	http_seed_connection
	peer_connection_handle
	instantiate_connection
	merkle
	natpmp
	part_file
	packet_buffer
	piece_picker
	platform_util
	proxy_base
	peer_list
	puff
	random
	receive_buffer
	request_blocks
	resolve_links
	resolver
	rss
	session
	session_call
	session_handle
	session_impl
	session_settings
	proxy_settings
	session_stats
	settings_pack
	socket_io
	socket_type
	socks5_stream
	stat
	stat_cache
	storage
	time
	timestamp_history
	torrent
	torrent_handle
	torrent_info
	torrent_peer
	torrent_peer_allocator
	torrent_status
	tracker_manager
	http_tracker_connection
	utf8
	udp_tracker_connection
	udp_socket
	upnp
	utp_socket_manager
	utp_stream
	file_pool
	lsd
	disk_io_job
	disk_job_pool
	disk_buffer_pool
	disk_io_thread
	enum_net
	broadcast_socket
	magnet_uri
	parse_url
	ConvertUTF
	thread
	xml_parse
	version

# -- extensions --
	metadata_transfer
	ut_pex
	ut_metadata
	smart_ban
	lt_trackers
)

# -- kademlia --
set(kademlia_sources
	dht_storage
	dos_blocker
	dht_tracker
	msg
	node
	node_entry
	refresh
	rpc_manager
	find_data
	put_data
	node_id
	routing_table
	traversal_algorithm
	item
	get_peers
	get_item
)

# -- ed25519 --
set(ed25519_sources
	add_scalar
	fe
	ge
	key_exchange
	keypair
	sc
	seed
	sha512
	sign
	verify
)

set(includes include ed25519/src)

function(list_prepend _listName _prefix)
  string(REGEX REPLACE "([^;]+)" "${_prefix}\\1" _tmp_list "${${_listName}}")
  set (${_listName} "${_tmp_list}" PARENT_SCOPE)
endfunction()

list_prepend(sources "src/")
list_prepend(kademlia_sources "src/kademlia/")
list_prepend(ed25519_sources "ed25519/src/")

function(target_optional_compile_definitions _target _scope)
	set(options FEATURE)
	set(oneValueArgs NAME DESCRIPTION DEFAULT)
	set(multiValueArgs ENABLED DISABLED)
	cmake_parse_arguments(TOCD ${options} "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	option(${TOCD_NAME} "${TOCD_DESCRIPTION}" ${TOCD_DEFAULT})
	if (${${TOCD_NAME}})
		target_compile_definitions(${_target} ${_scope} ${TOCD_ENABLED})
	else()
		target_compile_definitions(${_target} ${_scope} ${TOCD_DISABLED})
	endif()
	if(${TOCD_FEATURE})
		add_feature_info(${TOCD_NAME} ${TOCD_NAME} "${TOCD_DESCRIPTION}")
	endif()
endfunction()

macro(feature_option _name _description _default)
	option(${_name} "${_description}" ${_default})
	add_feature_info(${_name} ${_name} "${_description}")
endmacro()

# these options control target creation and thus have to be declared before the add_library() call
feature_option(BUILD_SHARED_LIBS "build libtorrent as a shared library" ON)
feature_option(static_runtime "build libtorrent with static runtime" OFF)

macro(find_public_dependency _name)
	find_package(${_name} ${ARGN})
	string(TOUPPER "${_name}" _name_uppercased)
	if (${_name}_FOUND OR ${_name_uppercased}_FOUND)
		# Dependencies to be used below for generating Config.cmake file
		# We don't need the 'REQUIRED' argument there
		set(_args "${_name}")
		list(APPEND _args "${ARGN}")
		list(REMOVE_ITEM _args "REQUIRED")
		list(REMOVE_ITEM _args "") # just in case
		string(REPLACE ";" " " _args "${_args}")
		list(APPEND _package_dependencies "${_args}")
	endif()
endmacro()

find_public_dependency(Threads REQUIRED)

if(static_runtime)
	include(ucm_flags)
	ucm_set_runtime(STATIC)
	set(Boost_USE_MULTITHREADED ON)
	set(Boost_USE_STATIC_RUNTIME ON)
	set(OPENSSL_USE_STATIC_LIBS TRUE)
	set(OPENSSL_MSVC_STATIC_RT TRUE)
endif()

if (NOT BUILD_SHARED_LIBS)
	set(Boost_USE_STATIC_LIBS ON)
endif()

add_library(torrent-rasterbar ${sources})

if (BUILD_SHARED_LIBS)
	target_compile_definitions(torrent-rasterbar
		PRIVATE TORRENT_BUILDING_SHARED
		INTERFACE TORRENT_LINKING_SHARED
	)
endif()

set_target_properties(torrent-rasterbar
	PROPERTIES
		CXX_VISIBILITY_PRESET "hidden"
		VISIBILITY_INLINES_HIDDEN "true"
		VERSION ${PROJECT_VERSION}
		SOVERSION ${SOVERSION}
)

target_compile_definitions(torrent-rasterbar
	PUBLIC
		$<$<CONFIG:Debug>:TORRENT_DEBUG>
	PRIVATE
		TORRENT_BUILDING_LIBRARY
		_FILE_OFFSET_BITS=64
		BOOST_EXCEPTION_DISABLE
		BOOST_ASIO_ENABLE_CANCELIO
)

target_include_directories(torrent-rasterbar
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(torrent-rasterbar
	PUBLIC
		Threads::Threads
)

feature_option(build_tests "build tests" OFF)
if(NOT build_tests) # tests require deprecated symbols
	target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME deprecated-functions DEFAULT ON
		DESCRIPTION "enable deprecated functions for backwards compatibility" DISABLED TORRENT_NO_DEPRECATE)
endif()
feature_option(build_examples "build examples" OFF)
feature_option(build_tools "build tools" OFF)
feature_option(python-bindings "build python bindings" OFF)

target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME logging DEFAULT ON
	DESCRIPTION "build with logging" DISABLED TORRENT_DISABLE_LOGGING)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME pool-allocators DEFAULT ON
	DESCRIPTION "Uses a pool allocator for disk and piece buffers" DISABLED TORRENT_DISABLE_POOL_ALLOCATOR)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME resolve-countries DEFAULT ON
	DESCRIPTION "enable support for resolving countries from peer IPs" DISABLED TORRENT_DISABLE_RESOLVE_COUNTRIES)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME unicode DEFAULT ON
	DESCRIPTION "enable unicode support" ENABLED UNICODE _UNICODE)

feature_option(dht "enable support for Mainline DHT" ON)
feature_option(encryption "link against openssl and enable encryption" ON)
feature_option(exceptions "build with exception support" ON)

if (dht)
	target_sources(torrent-rasterbar PRIVATE ${kademlia_sources} ${ed25519_sources})
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_DHT)
endif()

if (encryption)
	find_public_dependency(OpenSSL REQUIRED)
	set_package_properties(OpenSSL
	PROPERTIES
		URL "https://www.openssl.org/"
		DESCRIPTION "Full-strength general purpose cryptography library"
		TYPE RECOMMENDED
		PURPOSE "Provides encryption support to libtorrent"
	)
	target_sources(torrent-rasterbar PRIVATE src/mpi src/pe_crypto)
	target_link_libraries(torrent-rasterbar PUBLIC OpenSSL::SSL)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_OPENSSL)
else()
	target_sources(torrent-rasterbar PRIVATE  src/sha1)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_ENCRYPTION)
endif()


option(tcmalloc "link against google performance tools tcmalloc" OFF)

# C++ standard and Boost requirements are connected:
# 1. With C++11 onward, we require Boost system component, with C++03 we need chrono and random components too
# 2. When building against boost 1.66 and newer, C++11 is required.

# For the first requirement we do:
set(required_boost_components system)
if (NOT cxx_std_11 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	list(APPEND required_boost_components chrono random)
endif()
# Boost
find_public_dependency(Boost REQUIRED COMPONENTS ${required_boost_components})
target_include_directories(torrent-rasterbar PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(torrent-rasterbar PUBLIC ${Boost_SYSTEM_LIBRARY})

# now test the second requirement:
if (Boost_VERSION VERSION_GREATER_EQUAL 106600)
	if (NOT cxx_std_11 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
		message(FATAL_ERROR "When building against boost 1.66 and newer, C++11 is required,\n"
		                    "and your compiler does not support that")
	endif()
endif()

# selecting C++14/11 mode if available
if (cxx_std_14 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	target_compile_features(torrent-rasterbar PUBLIC cxx_std_14)
	message(STATUS "Building in C++14 mode")
elseif(cxx_std_11 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	target_compile_features(torrent-rasterbar PUBLIC cxx_std_11)
	message(STATUS "Building in C++11 mode")
endif()

target_link_libraries(torrent-rasterbar PUBLIC ${Boost_LIBRARIES})

if (WIN32)
	target_link_libraries(torrent-rasterbar
		PRIVATE
			wsock32 ws2_32 Iphlpapi
			debug dbghelp
	)
	add_definitions(-D_WIN32_WINNT=0x0600)
	# prevent winsock1 to be included
	add_definitions(-DWIN32_LEAN_AND_MEAN)
	if (MSVC)
		target_compile_definitions(torrent-rasterbar
			PUBLIC
				BOOST_ALL_NO_LIB
				_SCL_SECURE_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE # disable bogus deprecation warnings on msvc8
		)
		target_compile_options(torrent-rasterbar
			PRIVATE
				/Zc:wchar_t /Zc:forScope # these compiler settings just make the compiler standard conforming
				/MP # for multi-core compilation
				/bigobj # increase the number of sections for obj files
		)
	endif()
endif()

if (exceptions)
	if (MSVC)
		target_compile_options(torrent-rasterbar PUBLIC /EHsc)
	else (MSVC)
		target_compile_options(torrent-rasterbar PUBLIC -fexceptions)
	endif (MSVC)
else()
	if (MSVC)
		target_compile_definitions(torrent-rasterbar PUBLIC _HAS_EXCEPTIONS=0)
	else (MSVC)
		target_compile_options(torrent-rasterbar PUBLIC -fno-exceptions)
	endif (MSVC)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_definitions(-Wno-c++11-extensions)
	add_definitions(-fcolor-diagnostics)
endif()

if (tcmalloc)
	target_link_libraries(torrent-rasterbar PRIVATE tcmalloc)
endif()

set_target_properties(torrent-rasterbar PROPERTIES
	SOVERSION ${SOVERSION})

get_property (COMPILETIME_OPTIONS_LIST
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIRECTORY}
	PROPERTY COMPILE_DEFINITIONS
	)

set(COMPILETIME_OPTIONS ${CXX_MODE_COMPILE_OPTION})
foreach (s ${COMPILETIME_OPTIONS_LIST})
	set (COMPILETIME_OPTIONS "${COMPILETIME_OPTIONS} -D${s}")
endforeach (s)

generate_and_install_pkg_config_file(torrent-rasterbar libtorrent-rasterbar)

install(TARGETS torrent-rasterbar EXPORT LibtorrentRasterbarTargets
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/libtorrent DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h*")

# === generate a CMake Config File ===
include(CMakePackageConfigHelpers)
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/LibtorrentRasterbar)
string(REGEX REPLACE "([^;]+)" "find_dependency(\\1)" _find_dependency_calls "${_package_dependencies}")
string(REPLACE ";" "\n" _find_dependency_calls "${_find_dependency_calls}")

if(CMAKE_VERSION VERSION_LESS "3.11.0")
	set(_compatibility ExactVersion)
else()
	set(_compatibility SameMinorVersion)
endif()

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfigVersion.cmake"
	VERSION ${libtorrent_VERSION}
	COMPATIBILITY ${_compatibility}
)

export(EXPORT LibtorrentRasterbarTargets
	FILE "${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarTargets.cmake"
	NAMESPACE LibtorrentRasterbar::
)

configure_package_config_file(LibtorrentRasterbarConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake"
	INSTALL_DESTINATION "${ConfigPackageLocation}"
	NO_SET_AND_CHECK_MACRO
	NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

install(EXPORT LibtorrentRasterbarTargets
	NAMESPACE
		LibtorrentRasterbar::
	DESTINATION
		${ConfigPackageLocation}
)
install(
	FILES
		"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfigVersion.cmake"
	DESTINATION
		${ConfigPackageLocation}
)

install(
	FILES
		${CMAKE_CURRENT_SOURCE_DIR}/examples/cmake/FindLibtorrentRasterbar.cmake
	DESTINATION
		${CMAKE_INSTALL_DATADIR}/cmake/Modules
)

include(CheckCXXCompilerFlag)

add_subdirectory(bindings)

# === build tools ===
if (build_tools)
	add_subdirectory(tools)
endif()

# === build examples ===
if (build_examples)
	add_subdirectory(examples)
endif()

# === build tests ===
if(build_tests)
	enable_testing()
	# this will make some internal functions available in the DLL interface
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_EXPORT_EXTRA)
	add_subdirectory(test)
endif()

feature_summary(DEFAULT_DESCRIPTION WHAT ALL)
