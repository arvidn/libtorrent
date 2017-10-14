cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project(libtorrent)
set (SOVERSION "10")
set (VERSION "1.2.0")

list(APPEND CMAKE_MODULE_PATH ${libtorrent_SOURCE_DIR}/cmake/Modules)

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
	path
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
	ip_notifier
	ip_voter
	listen_socket_handle
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
	read_resume_data
	write_resume_data
	request_blocks
	resolve_links
	resolver
	session
	session_call
	session_handle
	session_impl
	session_settings
	session_udp_sockets
	proxy_settings
	session_stats
	settings_pack
	sha1_hash
	socket_io
	socket_type
	socks5_stream
	stat
	stat_cache
	storage
	storage_piece_set
	storage_utils
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
	disk_job_fence
	disk_job_pool
	disk_buffer_pool
	disk_io_thread
	disk_io_thread_pool
	enum_net
	broadcast_socket
	magnet_uri
	parse_url
	ConvertUTF
	xml_parse
	version
	ffs
	add_torrent_params
	peer_info
	stack_allocator

# -- extensions --
	ut_pex
	ut_metadata
	smart_ban
)

# -- kademlia --
set(kademlia_sources
	dht_state
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
	ed25519
	sample_infohashes
	dht_settings
)

# -- ed25519 --
set(ed25519_sources
	add_scalar
	fe
	ge
	key_exchange
	keypair
	sc
	sign
	verify
)

set(includes include ed25519/src)

option(shared "build libtorrent as a shared library" ON)
option(static_runtime "build libtorrent with static runtime" OFF)
option(encryption "link against openssl and enable encryption" ON)
option(dht "enable support for Mainline DHT" ON)
option(deprecated-functions "enable deprecated functions for backwards compatibility" ON)
option(exceptions "build with exception support" ON)
option(libiconv "enable linking against system libiconv" OFF)
option(logging "build with logging" ON)
option(build_tests "build tests" OFF)
option(build_examples "build examples" OFF)

find_package(Threads REQUIRED)

include_directories(${includes})

if (encryption)
	list(APPEND sources pe_crypto)
else()
	if (NOT WIN32 AND NOT APPLE)
		list(APPEND sources sha1)
	endif()
endif (encryption)

foreach(s ${sources})
	list(APPEND sources2 src/${s})
endforeach(s)

if (dht)
	foreach(s ${kademlia_sources})
		list(APPEND sources2 src/kademlia/${s})
	endforeach(s)
	foreach(s ${ed25519_sources})
		list(APPEND sources2 ed25519/src/${s})
	endforeach(s)
	list(APPEND sources2 src/hasher512)
	if (NOT encryption AND NOT WIN32 AND NOT APPLE)
		list(APPEND sources2 src/sha512)
	endif()
endif()

if (shared)
	add_library(torrent-rasterbar SHARED ${sources2})
	target_compile_definitions(torrent-rasterbar PRIVATE TORRENT_BUILDING_SHARED)
	if(NOT MSVC)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
	endif()
else()
	if(static_runtime)
		# fix /MT flag:
		set(CompilerFlags
				CMAKE_CXX_FLAGS
				CMAKE_CXX_FLAGS_DEBUG
				CMAKE_CXX_FLAGS_RELWITHDEBINFO
				CMAKE_CXX_FLAGS_RELEASE
				CMAKE_C_FLAGS
				CMAKE_C_FLAGS_DEBUG
				CMAKE_CXX_FLAGS_RELWITHDEBINFO
				CMAKE_C_FLAGS_RELEASE
				)
		foreach(CompilerFlag ${CompilerFlags})
			string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
		endforeach()
		set(Boost_USE_MULTITHREADED ON)

		set(Boost_USE_STATIC_RUNTIME ON)
	endif()
	set(Boost_USE_STATIC_LIBS ON)
	add_library(torrent-rasterbar STATIC ${sources2})
endif()

target_compile_definitions(torrent-rasterbar PUBLIC $<$<CONFIG:Debug>:TORRENT_DEBUG>)

target_compile_definitions(torrent-rasterbar PRIVATE TORRENT_BUILDING_LIBRARY)

if (build_tests)
	# this will make some internal functions available in the
	# DLL interface, for the tests to access
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_EXPORT_EXTRA)
endif (build_tests)

if (libiconv)
	find_package(Iconv REQUIRED)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_ICONV)
	target_include_directories(torrent-rasterbar SYSTEM PUBLIC ${ICONV_INCLUDE_DIR})
endif (libiconv)

if (encryption)
	if(NOT DEFINED OPENSSL_INCLUDE_DIR OR NOT DEFINED OPENSSL_LIBRARIES)
		FIND_PACKAGE(OpenSSL REQUIRED)
	endif()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_OPENSSL TORRENT_USE_LIBCRYPTO)
	target_include_directories(torrent-rasterbar PUBLIC ${OPENSSL_INCLUDE_DIR})
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_ENCRYPTION)
endif()

if (NOT logging)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_LOGGING)
endif()

if (NOT dht)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_DHT)
endif()

# Boost
if(NOT DEFINED Boost_INCLUDE_DIR OR NOT DEFINED Boost_LIBRARIES)
	FIND_PACKAGE(Boost REQUIRED COMPONENTS system)
endif()
include_directories(${Boost_INCLUDE_DIRS})
target_link_libraries(torrent-rasterbar ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

if (WIN32)
	target_link_libraries(torrent-rasterbar wsock32 ws2_32 Iphlpapi)
	# target Windows Vista or later
	target_compile_definitions(torrent-rasterbar PUBLIC _WIN32_WINNT=0x0600)
	target_link_libraries(torrent-rasterbar debug dbghelp)
	# prevent winsock1 to be included
	target_compile_definitions(torrent-rasterbar PUBLIC WIN32_LEAN_AND_MEAN)
	if (MSVC)
		target_compile_definitions(torrent-rasterbar PUBLIC BOOST_ALL_NO_LIB)
		# for multicore compilation
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
		# increase the number of sections for obj files
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
	endif()
endif()

if(APPLE)
	# for ip_notifier
	target_link_libraries(torrent-rasterbar "-framework CoreFoundation" "-framework SystemConfiguration")
endif (APPLE)

if (encryption)
	target_link_libraries(torrent-rasterbar ${OPENSSL_LIBRARIES})
endif()

if (libiconv)
	target_link_libraries(torrent-rasterbar ${ICONV_LIBRARIES})
endif (libiconv)

if (NOT deprecated-functions)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_NO_DEPRECATE)
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

if (MSVC)
	# disable bogus deprecation warnings on msvc8
	target_compile_definitions(torrent-rasterbar PUBLIC _SCL_SECURE_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE)
	# these compiler settings just make the compiler standard conforming
	target_compile_options(torrent-rasterbar PUBLIC /Zc:wchar_t /Zc:forScope)
	# for multi-core compilation
	target_compile_options(torrent-rasterbar PUBLIC /MP)
	# increase the number of sections for obj files
	target_compile_options(torrent-rasterbar PUBLIC /bigobj)
endif()

target_compile_definitions(torrent-rasterbar PUBLIC _FILE_OFFSET_BITS=64)
target_compile_definitions(torrent-rasterbar PUBLIC BOOST_EXCEPTION_DISABLE)
target_compile_definitions(torrent-rasterbar PUBLIC BOOST_ASIO_ENABLE_CANCELIO)
target_compile_definitions(torrent-rasterbar PUBLIC BOOST_ASIO_HAS_STD_CHRONO)

set_target_properties(torrent-rasterbar PROPERTIES
	SOVERSION ${SOVERSION})

# libtorrent requires at least C++11
set_property(TARGET torrent-rasterbar PROPERTY CXX_STANDARD 11)
set_property(TARGET torrent-rasterbar PROPERTY CXX_STANDARD_REQUIRED ON)

get_property (COMPILETIME_OPTIONS_LIST
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIRECTORY}
	PROPERTY COMPILE_DEFINITIONS
	)
foreach (s ${COMPILETIME_OPTIONS_LIST})
	set (COMPILETIME_OPTIONS "${COMPILETIME_OPTIONS} -D${s}")
endforeach (s)

configure_file(libtorrent-rasterbar-cmake.pc.in libtorrent-rasterbar.pc)

string (COMPARE EQUAL "${CMAKE_SIZEOF_VOID_P}" "8" IS64BITS)

if (IS64BITS AND RESPECTLIB64)
	set (LIBDIR "lib64")
else()
	set (LIBDIR "lib")
endif()

install(TARGETS torrent-rasterbar EXPORT torrent-rasterbarTargets
	DESTINATION ${LIBDIR} INCLUDES DESTINATION include)
install(DIRECTORY include/libtorrent DESTINATION include)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar.pc DESTINATION ${LIBDIR}/pkgconfig)

# === generate a CMake Config File ===
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfigVersion.cmake"
	VERSION ${VERSION}
	COMPATIBILITY ExactVersion
)

export(EXPORT torrent-rasterbarTargets
	FILE "${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarTargets.cmake"
	NAMESPACE Libtorrent::
)

configure_file(libtorrent-rasterbarConfig.cmake
	"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfig.cmake"
	COPYONLY
)

set(ConfigPackageLocation lib/cmake/libtorrent-rasterbar)
install(EXPORT torrent-rasterbarTargets
	FILE
		libtorrent-rasterbarTargets.cmake
	NAMESPACE
		Libtorrent::
	DESTINATION
		${ConfigPackageLocation}
)
install(
	FILES
		libtorrent-rasterbarConfig.cmake
		"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfigVersion.cmake"
	DESTINATION
		${ConfigPackageLocation}
)

# === build examples ===
if (build_examples)
	add_subdirectory(examples)
endif()

# === build tests ===
if(build_tests)
	if (NOT logging)
		message(FATAL_ERROR "tests require logging to be enabled")
	endif()

	file(GLOB tests RELATIVE "${PROJECT_SOURCE_DIR}" "test/test_*.cpp")
	list(REMOVE_ITEM tests "test/test_natpmp.cpp") # doesn't build at time of writing
	list(REMOVE_ITEM tests "test/test_utils.cpp") # helper file, not a test

	add_library(test_common STATIC test/main.cpp test/test.cpp
		test/setup_transfer.cpp test/dht_server.cpp test/udp_tracker.cpp
		test/peer_server.cpp test/web_seed_suite.cpp test/swarm_suite.cpp
		test/test_utils.cpp test/make_torrent.hpp test/make_torrent.cpp
		test/settings.hpp test/settings.cpp)
	set_target_properties(test_common PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
	target_link_libraries(test_common torrent-rasterbar)
	enable_testing()

	foreach(s ${tests})
		get_filename_component (sn ${s} NAME_WE)
		add_executable(${sn} ${s})
		target_link_libraries(${sn} test_common)
		set_target_properties(${sn} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test")
		set_target_properties(${sn} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
		add_test(NAME ${sn} COMMAND ${sn} WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test")
	endforeach(s)
endif()
