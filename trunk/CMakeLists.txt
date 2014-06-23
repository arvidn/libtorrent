cmake_minimum_required(VERSION 2.6)
project(libtorrent)

set(sources
	web_connection_base
	alert
	alert_manager
	allocator
	asio
	assert
	bandwidth_limit
	bandwidth_manager
	bandwidth_queue_entry
	bloom_filter
	chained_buffer
	connection_queue
	create_torrent
	disk_buffer_holder
	entry
	error_code
	file_storage
	lazy_bdecode
	escape_string
	string_util
	file
	gzip
	hasher
	http_connection
	http_stream
	http_parser
	i2p_stream
	identify_client
	ip_filter
	ip_voter
	peer_connection
	bt_peer_connection
	web_peer_connection
	http_seed_connection
	instantiate_connection
	natpmp
	packet_buffer
	piece_picker
	policy
	puff
	random
	rss
	session
	session_impl
	settings
	socket_io
	socket_type  
	socks5_stream
	stat
	storage
	time
	timestamp_history
	torrent
	torrent_handle
	torrent_info
	tracker_manager
	http_tracker_connection
	utf8
	udp_tracker_connection
	udp_socket
	upnp
	utp_socket_manager
	utp_stream
	logger
	file_pool
	lsd
	disk_buffer_pool
	disk_io_thread
	enum_net
	broadcast_socket
	magnet_uri
	parse_url
	ConvertUTF
	thread
	xml_parse

# -- extensions --
	metadata_transfer
	ut_pex
	ut_metadata
	smart_ban
	lt_trackers
)

# -- kademlia --
set(kademlia_sources
	dht_tracker
	node
	refresh
	rpc_manager
	find_data
	node_id
	routing_table
	traversal_algorithm
	logging
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

option(shared "build libtorrent as a shared library" ON)
option(static_runtime "build libtorrent with static runtime" OFF)
option(tcmalloc "link against google performance tools tcmalloc" OFF)
option(pool-allocators "Uses a pool allocator for disk and piece buffers" ON)
option(encryption "link against openssl and enable encryption" ON)
option(geoip "link against LGPL GeoIP code from Maxmind, to enable geoip database support" OFF)
option(dht "enable support for Mainline DHT" ON)
option(resolve-countries "enable support for resolving countries from peer IPs" ON)
option(unicode "enable unicode support" ON)
option(deprecated-functions "enable deprecated functions for backwards compatibility" ON)
option(exceptions "build with exception support" ON)
option(logging "build with logging" OFF)
option(verbose-logging "build with verbose logging" OFF)
option(build_tests "build tests" OFF)
option(build_examples "build examples" ON)

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

if (encryption)
	list(APPEND sources pe_crypto)
endif (encryption)

if (logging)
	add_definitions(-DTORRENT_LOGGING)
endif()
if (verbose-logging)
	add_definitions(-DTORRENT_VERBOSE_LOGGING)
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
else()
	add_definitions(-DTORRENT_DISABLE_DHT)
endif()

if(NOT MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fvisibility-inlines-hidden")
endif()

if (shared)
	add_definitions(-DTORRENT_BUILDING_SHARED)
	add_library(torrent-rasterbar SHARED ${sources2})
else (shared)
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
	endif()
	add_library(torrent-rasterbar STATIC ${sources2})
endif()

# Boost
if(NOT DEFINED Boost_INCLUDE_DIR OR NOT DEFINED Boost_LIBRARIES)
	FIND_PACKAGE( Boost COMPONENTS system thread date_time chrono)
endif()
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(torrent-rasterbar ${Boost_LIBRARIES})

# this works around a bug in asio in boost-1.39
#add_definitions(-DBOOST_ASIO_HASH_MAP_BUCKETS=1021  -D__USE_W32_SOCKETS -DWIN32_LEAN_AND_MEAN )

if(NOT static_runtime)
	add_definitions(-DBOOST_ASIO_DYN_LINK -DBOOST_DATE_TIME_DYN_LINK -DBOOST_THREAD_DYN_LINK -DBOOST_SYSTEM_DYN_LINK -DBOOST_CHRONO_DYN_LINK)
else()
	add_definitions(-DBOOST_ASIO_SEPARATE_COMPILATION)
endif()

if (WIN32)
	target_link_libraries(torrent-rasterbar wsock32 ws2_32)
	add_definitions(-D_WIN32_WINNT=0x0500)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP") # multicore compilation
endif()

if (encryption)
	if(NOT DEFINED OPENSSL_INCLUDE_DIR OR NOT DEFINED OPENSSL_LIBRARIES)
		FIND_PACKAGE(OpenSSL REQUIRED)
	endif()
	add_definitions(-DTORRENT_USE_OPENSSL)
	list(APPEND sources asio_ssl)
	include_directories(${OPENSSL_INCLUDE_DIR})
	target_link_libraries(torrent-rasterbar ${OPENSSL_LIBRARIES})
else()
	add_definitions(-DTORRENT_DISABLE_ENCRYPTION)
	list(APPEND sources sha1)
endif()

if (NOT pool-allocators)
	add_definitions(-DTORRENT_DISABLE_POOL_ALLOCATOR)
endif()

if (NOT geoip)
	add_definitions(-DTORRENT_DISABLE_GEO_IP)
endif()

if (NOT resolve-countries)
	add_definitions(-DTORRENT_DISABLE_RESOLVE_COUNTRIES)
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
else()
	add_definitions(-Wno-c++11-extensions)
endif()

add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-DBOOST_EXCEPTION_DISABLE)
add_definitions(-DBOOST_ASIO_ENABLE_CANCELIO)

if (tcmalloc)
	target_link_libraries(torrent-rasterbar tcmalloc)
endif()

include_directories(${includes})

set_target_properties(torrent-rasterbar PROPERTIES
	SOVERSION 1
	VERSION 1)

set (VERSION "1.0.0")

get_property (COMPILETIME_OPTIONS_LIST
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIRECTORY}
	PROPERTY COMPILE_DEFINITIONS
	)
foreach (s ${COMPILETIME_OPTIONS_LIST})
	set (COMPILETIME_OPTIONS "${COMPILETIME_OPTIONS} -D${s}")
endforeach (s)

if (MSVC)
	configure_file(libtorrent-rasterbar-cmake.pc.in libtorrent-rasterbar.pc)
endif()

string (COMPARE EQUAL "${CMAKE_SIZEOF_VOID_P}" "8" IS64BITS)

if (IS64BITS AND RESPECTLIB64)
	set (LIBDIR "lib64")
else()
	set (LIBDIR "lib")
endif()

install(TARGETS torrent-rasterbar DESTINATION ${LIBDIR} CONFIGURATIONS release)
install(DIRECTORY include/libtorrent
	DESTINATION include
	PATTERN ".svn" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar.pc DESTINATION ${LIBDIR}/pkgconfig)

# === build examples ===
if(build_examples)
	FILE(GLOB examples RELATIVE "${PROJECT_SOURCE_DIR}" "examples/*.cpp")
	foreach(s ${examples})
		get_filename_component (sn ${s} NAME_WE)
		add_executable(${sn} ${s})
		target_link_libraries(${sn} torrent-rasterbar)
	endforeach(s)

	include_directories(${Boost_INCLUDE_DIR})
endif()

# === build tests ===
if(build_tests)
	FILE(GLOB tests RELATIVE "${PROJECT_SOURCE_DIR}" "tests/*.cpp")
	add_library(test_common STATIC test/main.cpp test/setup_transfer.cpp)
	enable_testing()

	foreach(s ${tests})
		get_filename_component (sn ${s} NAME_WE)
		add_executable(${sn} ${s})
		target_link_libraries(${sn} torrent-rasterbar test_common)
		add_test(${sn} ${s})
	endforeach(s)

	add_executable(test_upnp test/test_upnp.cpp)
	target_link_libraries(test_upnp torrent-rasterbar)

	add_executable(test_natpmp test/test_natpmp.cpp)
	target_link_libraries(test_natpmp torrent-rasterbar)
endif()
