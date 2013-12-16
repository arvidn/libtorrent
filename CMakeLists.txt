cmake_minimum_required(VERSION 2.6)
project(libtorrent)

set(sources
	web_connection_base
	alert
	allocator
	asio
	asio_ssl
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
	http_connection
	http_stream
	http_parser
	i2p_stream
	identify_client
	ip_filter
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
	rsa
	rss
	session
	session_impl
	settings
	socket_io
	socket_type  
	socks5_stream
	stat
	storage
	thread
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
)

set(includes include)

option(shared "build libtorrent as a shared library" ON)
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
endif (NOT CMAKE_BUILD_TYPE)

# add_definitions() doesn't seem to let you say wich build type to apply it to
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DTORRENT_DEBUG")

set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

if (encryption)
	list(APPEND sources pe_crypto)
endif (encryption)

if (logging)
	add_definitions(-DTORRENT_LOGGING)
endif (logging)
if (verbose-logging)
	add_definitions(-DTORRENT_VERBOSE_LOGGING)
endif (verbose-logging)

foreach(s ${sources})
	list(APPEND sources2 src/${s})
endforeach(s)

if (dht)
	foreach(s ${kademlia_sources})
		list(APPEND sources2 src/kademlia/${s})
	endforeach(s)
else (dht)
	add_definitions(-DTORRENT_DISABLE_DHT)
endif (dht)

if(NOT MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fvisibility-inlines-hidden")
endif()

if (shared)
	add_definitions(-DTORRENT_BUILDING_SHARED)
	add_library(torrent-rasterbar SHARED ${sources2})
else (shared)
	add_library(torrent-rasterbar STATIC ${sources2})
endif (shared)

if (NOT Boost_VERSION LESS 103500)
	if(NOT MSVC)
		FIND_PACKAGE( Boost 1.35 COMPONENTS system)
	else(NOT MSVC)
		FIND_PACKAGE( Boost 1.35 COMPONENTS system thread date_time)
	endif(NOT MSVC)
endif (NOT Boost_VERSION LESS 103500)
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(torrent-rasterbar ${Boost_LIBRARIES})

# this works around a bug in asio in boost-1.39
add_definitions(-DBOOST_ASIO_HASH_MAP_BUCKETS=1021 -DBOOST_ASIO_DYN_LINK -D__USE_W32_SOCKETS -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0500)

add_definitions(-DBOOST_DATE_TIME_DYN_LINK -DBOOST_THREAD_DYN_LINK -DBOOST_SYSTEM_DYN_LINK -DBOOST_CHRONO_DYN_LINK)

if (WIN32)
	target_link_libraries(torrent-rasterbar wsock32 ws2_32)
endif (WIN32)

if (encryption)
	FIND_PACKAGE(OpenSSL REQUIRED)
	add_definitions(-DTORRENT_USE_OPENSSL)
	include_directories(${OPENSSL_INCLUDE_DIR})
	target_link_libraries(torrent-rasterbar ${OPENSSL_LIBRARIES})
else (encryption)
	add_definitions(-DTORRENT_DISABLE_ENCRYPTION)
	list(APPEND sources sha1)
endif (encryption)

if (NOT pool-allocators)
	add_definitions(-DTORRENT_DISABLE_POOL_ALLOCATOR)
endif (NOT pool-allocators)

if (NOT geoip)
	add_definitions(-DTORRENT_DISABLE_GEO_IP)
endif (NOT geoip)

if (NOT resolve-countries)
	add_definitions(-DTORRENT_DISABLE_RESOLVE_COUNTRIES)
endif (NOT resolve-countries)

if (unicode)
	add_definitions(-DUNICODE -D_UNICODE)
endif (unicode)

if (NOT deprecated-functions)
	add_definitions(-DTORRENT_NO_DEPRECATE)
endif (NOT deprecated-functions)

if (exceptions)
	if (MSVC)
		add_definitions(/EHsc)
	else (MSVC)
		add_definitions(-fexceptions)
	endif (MSVC)
else (exceptions)
	add_definitions(-fno-exceptions)
endif (exceptions)

if (MSVC)
# disable bogus deprecation warnings on msvc8
	add_definitions(-D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE)
# these compiler settings just makes the compiler standard conforming
	add_definitions(/Zc:wchar_t /Zc:forScope)
# for multi-core compilation
	add_definitions(/MP)

#	<toolset>msvc,<variant>release:<linkflags>/OPT:ICF=5
#	<toolset>msvc,<variant>release:<linkflags>/OPT:REF
endif(MSVC)

add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-DBOOST_DISABLE_EXCEPTION)
add_definitions(-DBOOST_ASIO_ENABLE_CANCELIO)

if (tcmalloc)
	target_link_libraries(torrent-rasterbar tcmalloc)
endif (tcmalloc)

include_directories(${includes})

set_target_properties(torrent-rasterbar PROPERTIES
	SOVERSION 1
	VERSION 1)

set (VERSION "0.16.13")

get_property (COMPILETIME_OPTIONS_LIST
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIRECTORY}
	PROPERTY COMPILE_DEFINITIONS
	)
foreach (s ${COMPILETIME_OPTIONS_LIST})
	set (COMPILETIME_OPTIONS "${COMPILETIME_OPTIONS} -D${s}")
endforeach (s)

if (MSVC)
	configure_file(libtorrent-rasterbar-cmake.pc.in libtorrent-rasterbar.pc)
endif (MSVC)

string (COMPARE EQUAL "${CMAKE_SIZEOF_VOID_P}" "8" IS64BITS)

if (IS64BITS AND RESPECTLIB64)
	set (LIBDIR "lib64")
else (IS64BITS AND RESPECTLIB64)
	set (LIBDIR "lib")
endif (IS64BITS AND RESPECTLIB64)

install(TARGETS torrent-rasterbar DESTINATION ${LIBDIR} CONFIGURATIONS release)
install(DIRECTORY include/libtorrent
	DESTINATION include
	PATTERN ".svn" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar.pc DESTINATION ${LIBDIR}/pkgconfig)

# === build examples ===
if(build_examples)
	set(examples client_test dump_torrent simple_client enum_if make_torrent)

	foreach(s ${examples})
		add_executable(${s} examples/${s}.cpp)
		target_link_libraries(${s} torrent-rasterbar)
	endforeach(s)

	include_directories(${Boost_INCLUDE_DIR})
endif(build_examples)
# === build tests ===
if(build_tests)
	set(tests
		test_auto_unchoke
		test_http_connection
		test_buffer
		test_utp
		test_storage
		test_torrent
		test_dht
		test_transfer
		test_piece_picker
		test_fast_extension
		test_pe_crypto
		test_bencoding
		test_bdecode_performance
		test_primitives
		test_ip_filter
		test_hasher
		test_metadata_extension
		test_trackers_extension
		test_swarm
		test_lsd
		test_pex
		test_web_seed
		test_bandwidth_limiter
		test_session
		test_torrent_parse
		test_threads
		)

	add_library(test_common STATIC test/main.cpp test/setup_transfer.cpp)
	enable_testing()

	foreach(s ${tests})
		add_executable(${s} test/${s}.cpp)
		target_link_libraries(${s} torrent-rasterbar test_common)
		add_test(${s} ${s})
	endforeach(s)

	add_executable(test_upnp test/test_upnp.cpp)
	target_link_libraries(test_upnp torrent-rasterbar)

	add_executable(test_natpmp test/test_natpmp.cpp)
	target_link_libraries(test_natpmp torrent-rasterbar)
endif(build_tests)
