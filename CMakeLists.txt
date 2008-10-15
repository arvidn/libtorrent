cmake_minimum_required(VERSION 2.6)
project(libtorrent)

set(sources
	alert
	assert
	connection_queue
	create_torrent
	disk_buffer_holder
	entry
	error_code
	file_storage
	lazy_bdecode
	escape_string
	file
	gzip
	http_connection
	http_stream
	http_parser
	identify_client
	ip_filter
	peer_connection
	bt_peer_connection
	web_peer_connection
	instantiate_connection
	natpmp
	piece_picker
	policy
	session
	session_impl
	socks4_stream
	socks5_stream
	stat
	storage
#	mapped_storage
	torrent
	torrent_handle
	torrent_info
	tracker_manager
	http_tracker_connection
	udp_tracker_connection
	udp_socket
	upnp
	logger
	file_pool
	lsd
	disk_io_thread
	enum_net
	broadcast_socket
	magnet_uri
	parse_url

# -- extensions --
	metadata_transfer
	ut_pex
	ut_metadata
	smart_ban
)

# -- kademlia --
set(kademlia_sources
	closest_nodes
	dht_tracker
	node
	refresh
	rpc_manager
	find_data
	node_id
	routing_table
	traversal_algorithm
)

set(zlib_sources
	adler32
	compress
	crc32
	deflate
	gzio
	infback
	inffast
	inflate
	inftrees
	trees
	uncompr
	zutil
)

set(includes include zlib)

option(shared "build libtorrent as a shared library" ON)
option(tcmalloc "link against google performance tools tcmalloc" OFF)
option(pool-allocators "Uses a pool allocator for disk and piece buffers" ON)
option(encryption "link against openssl and enable encryption" ON)
option(geoip "link against LGPL GeoIP code from Maxmind, to enable geoip database support" OFF)
option(dht "enable support for Mainline DHT" ON)
option(resolve-countries "enable support for resolving countries from peer IPs" ON)
option(unicode "enable unicode support" ON)
option(deprecated-functions "enable deprecated functions for backwards compatibility" ON)

if (encryption)
	list(APPEND sources pe_crypto)
endif (encryption)

foreach(s ${sources})
	list(APPEND sources2 src/${s})
endforeach(s)
foreach(s ${zlib_sources})
	list(APPEND zlib_sources2 zlib/${s})
endforeach(s)

if (dht)
	foreach(s ${kademlia_sources})
		list(APPEND sources2 src/kademlia/${s})
	endforeach(s)
else (dht)
	add_definitions(-DTORRENT_DISABLE_DHT)
endif (dht)

if (shared)
	add_library(torrent SHARED ${sources2} ${zlib_sources2})
else (shared)
	add_library(torrent STATIC ${sources2} ${zlib_sources2})
endif (shared)

FIND_PACKAGE( Boost 1.34 COMPONENTS filesystem thread)
if (NOT Boost_VERSION LESS 103500)
	FIND_PACKAGE( Boost 1.35 COMPONENTS filesystem thread system)
endif (NOT Boost_VERSION LESS 103500)
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(torrent ${Boost_LIBRARIES})

if (WIN32)
	target_link_libraries(torrent wsock32 ws2_32)
endif (WIN32)

if (encryption)
	add_definitions(-DTORRENT_USE_OPENSSL)
	if (WIN32)
		target_link_libraries(torrent ssleay32 libeay32 advapi32 user32 shell32 gdi32)
	else (WIN32)
		target_link_libraries(torrent crypto ssl)
	endif (WIN32)
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

if (MSVC)
# disable bogus deprecation warnings on msvc8
	add_definitions(-D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE)
# these compiler settings just makes the compiler standard conforming
	add_definitions(/Zc:wchar_t /Zc:forScope)

#	<toolset>msvc,<variant>release:<linkflags>/OPT:ICF=5
#	<toolset>msvc,<variant>release:<linkflags>/OPT:REF
endif(MSVC)

add_definitions(-D_FILE_OFFSET_BITS=64)

if (tcmalloc)
	target_link_libraries(torrent tcmalloc)
endif (tcmalloc)

target_link_libraries(torrent z)
include_directories(${includes})

# === build examples ===

set(examples client_test dump_torrent simple_client enum_if make_torrent)

foreach(s ${examples})
	add_executable(${s} examples/${s}.cpp)
	target_link_libraries(${s} torrent)
endforeach(s)

FIND_PACKAGE( Boost 1.34 COMPONENTS program_options regex)
target_link_libraries(client_test ${Boost_LIBRARIES})
include_directories(${Boost_INCLUDE_DIR})

# === build tests ===

set(tests
	test_auto_unchoke
	test_http_connection
	test_buffer
	test_storage
	test_torrent
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
	test_swarm
	test_lsd
	test_pex
	test_web_seed
	test_bandwidth_limiter
	)

add_library(test_common STATIC test/main.cpp test/setup_transfer.cpp)
enable_testing()

foreach(s ${tests})
	add_executable(${s} test/${s}.cpp)
	target_link_libraries(${s} torrent test_common)
	add_test(${s} ${s})
endforeach(s)

add_executable(test_upnp test/test_upnp.cpp)
target_link_libraries(test_upnp torrent)

add_executable(test_natpmp test/test_natpmp.cpp)
target_link_libraries(test_natpmp torrent)

