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
option(pool-allocators "Uses a pool allocator for disk and piece buffers" ON)
option(encryption "link against openssl and enable encryption" ON)
option(dht "enable support for Mainline DHT" ON)
option(unicode "enable unicode support" ON)
option(deprecated-functions "enable deprecated functions for backwards compatibility" ON)
option(disk-stats "enable disk activity logging feature" OFF)
option(exceptions "build with exception support" ON)
option(libiconv "enable linking against system libiconv" OFF)
option(logging "build with logging" ON)
option(build_tests "build tests" OFF)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release FORCE)
endif()

# add_definitions() doesn't seem to let you say wich build type to apply it to
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DTORRENT_DEBUG")
if(UNIX)
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

if (build_tests)
	# this will make some internal functions available in the
	# DLL interface, for the tests to access
	add_definitions(-DTORRENT_EXPORT_EXTRA)
endif (build_tests)

find_package(Threads REQUIRED)

include_directories(${includes})

add_definitions(-DTORRENT_BUILDING_LIBRARY)
if (disk-stats)
	add_definitions(-DTORRENT_DISK_STATS)
endif (disk-stats)

if (libiconv)
	find_package(Iconv REQUIRED)
	add_definitions(-DTORRENT_USE_ICONV)
	include_directories(SYSTEM ${ICONV_INCLUDE_DIR})
endif (libiconv)

if (encryption)
	list(APPEND sources pe_crypto)
	if(NOT DEFINED OPENSSL_INCLUDE_DIR OR NOT DEFINED OPENSSL_LIBRARIES)
		FIND_PACKAGE(OpenSSL REQUIRED)
	endif()
	add_definitions(-DTORRENT_USE_OPENSSL)
	add_definitions(-DTORRENT_USE_LIBCRYPTO)
	include_directories(${OPENSSL_INCLUDE_DIR})
else()
	add_definitions(-DTORRENT_DISABLE_ENCRYPTION)
	if (NOT WIN32 AND NOT APPLE)
		list(APPEND sources sha1)
	endif()
endif (encryption)

if (NOT logging)
	add_definitions(-DTORRENT_DISABLE_LOGGING)
endif()

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
else()
	add_definitions(-DTORRENT_DISABLE_DHT)
endif()

if (shared)
	add_definitions(-DTORRENT_BUILDING_SHARED)
	add_library(torrent-rasterbar SHARED ${sources2})
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

# Boost
if(NOT DEFINED Boost_INCLUDE_DIR OR NOT DEFINED Boost_LIBRARIES)
	FIND_PACKAGE(Boost REQUIRED COMPONENTS system)
endif()
include_directories(${Boost_INCLUDE_DIRS})
target_link_libraries(torrent-rasterbar ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_definitions(-DBOOST_ASIO_HAS_STD_CHRONO=1 )

if (WIN32)
	target_link_libraries(torrent-rasterbar wsock32 ws2_32 Iphlpapi)
	add_definitions(-D_WIN32_WINNT=0x0600)
	# prevent winsock1 to be included
	add_definitions(-DWIN32_LEAN_AND_MEAN)
	if (MSVC)
		add_definitions(-DBOOST_ALL_NO_LIB)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP") # multicore compilation
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

if (NOT pool-allocators)
	add_definitions(-DTORRENT_DISABLE_POOL_ALLOCATOR)
endif()

if (unicode)
	add_definitions(-DUNICODE -D_UNICODE)
endif()

if (NOT deprecated-functions)
	add_definitions(-DTORRENT_NO_DEPRECATE)
endif()

if (exceptions)
	if (MSVC)
		add_definitions(/EHsc)
	else (MSVC)
		add_definitions(-fexceptions)
	endif (MSVC)
else()
	if (MSVC)
		add_definitions(-D_HAS_EXCEPTIONS=0)
	else (MSVC)
		add_definitions(-fno-exceptions)
	endif (MSVC)
endif()

if (MSVC)
# disable bogus deprecation warnings on msvc8
	add_definitions(-D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE)
# these compiler settings just makes the compiler standard conforming
	add_definitions(/Zc:wchar_t /Zc:forScope)
# for multi-core compilation
	add_definitions(/MP)

#$(SolutionDir)<toolset>msvc,<variant>release:<linkflags>/OPT:ICF=5
#$(SolutionDir)<toolset>msvc,<variant>release:<linkflags>/OPT:REF
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_definitions(-fcolor-diagnostics)
endif()

add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-DBOOST_EXCEPTION_DISABLE)
add_definitions(-DBOOST_ASIO_ENABLE_CANCELIO)
add_definitions(-DBOOST_ASIO_HAS_STD_CHRONO)

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

install(TARGETS torrent-rasterbar DESTINATION ${LIBDIR})
install(DIRECTORY include/libtorrent
	DESTINATION include
	PATTERN ".svn" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar.pc DESTINATION ${LIBDIR}/pkgconfig)

# === set up examples directory as an independent project ===
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/examples)
configure_file(examples/run_cmake.sh.in examples/run_cmake.sh)
# to build the examples, run examples/run_cmake.sh after building libtorrent

# === build tests ===
if(build_tests)
	file(GLOB tests RELATIVE "${PROJECT_SOURCE_DIR}" "test/test_*.cpp")
	list(REMOVE_ITEM tests "test/test_natpmp.cpp") # doesn't build at time of writing
	list(REMOVE_ITEM tests "test/test_utils.cpp") # helper file, not a test

	add_library(test_common OBJECT test/main.cpp test/test.cpp
		test/setup_transfer.cpp test/dht_server.cpp test/udp_tracker.cpp
		test/peer_server.cpp test/web_seed_suite.cpp test/swarm_suite.cpp
		test/test_utils.cpp test/make_torrent.hpp test/make_torrent.cpp
		test/settings.hpp test/settings.cpp)
		set_target_properties(test_common PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
	enable_testing()

	foreach(s ${tests})
		get_filename_component (sn ${s} NAME_WE)
		add_executable(${sn} ${s} $<TARGET_OBJECTS:test_common>)
		target_link_libraries(${sn} torrent-rasterbar)
		set_target_properties(${sn} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
		add_test(${sn} ${s})
	endforeach(s)

	add_executable(bdecode_benchmark test/bdecode_benchmark.cpp)
	target_link_libraries(bdecode_benchmark torrent-rasterbar)
	set_target_properties(bdecode_benchmark PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
endif()
