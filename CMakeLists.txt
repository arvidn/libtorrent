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
	sha1
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

option(WITH_TCMALLOC "link against google performance tools tcmalloc" OFF)
option(USE_POOL_ALLOCATORS "Uses a pool allocator for disk and piece buffers" ON)
option(ENABLE_ENCRYPTION "link against openssl and enable encryption" ON)
option(ENABLE_GEOIP "link against LGPL GeoIP code from Maxmind, to enable geoip database support" OFF)
option(ENABLE_DHT "enable support for Mainline DHT" ON)
option(ENABLE_RESOLVE_COUNTRIES "enable support for resolving countries from peer IPs" ON)
option(ENABLE_UNICODE "enable unicode support" ON)
option(WITH_DEPRECATED_FUNCTIONS "enable deprecated functions for backwards compatibility" ON)

if (ENABLE_ENCRYPTION)
	list(APPEND sources pe_crypto)
endif (ENABLE_ENCRYPTION)

foreach(s ${sources})
	list(APPEND sources2 src/${s})
endforeach(s)
foreach(s ${zlib_sources})
	list(APPEND zlib_sources2 zlib/${s})
endforeach(s)

if (ENABLE_DHT)
	foreach(s ${kademlia_sources})
		list(APPEND sources2 src/kademlia/${s})
	endforeach(s)
else (ENABLE_DHT)
	add_definitions(-DTORRENT_DISABLE_DHT)
endif (ENABLE_DHT)

add_library(z ${zlib_sources2})
add_library(torrent SHARED ${sources2} ${kademlia_sources2} ${zlib_sources2})

set(library_search_paths /usr/local/lib /usr/lib /opt/local/lib)
find_library(fs boost_filesystem-mt-1_35 ${library_search_paths})
target_link_libraries(torrent ${fs})
find_library(th boost_thread-mt-1_35 ${library_search_paths})
target_link_libraries(torrent ${th})
find_library(sys boost_system-mt-1_35 ${library_search_paths})
target_link_libraries(torrent ${sys})

if (WIN32)
	target_link_libraries(torrent wsock32 ws2_32)
endif (WIN32)

if (ENABLE_ENCRYPTION)
	add_definitions(-DTORRENT_USE_OPENSSL)
	if (WIN32)
		target_link_libraries(torrent ssleay32 libeay32 advapi32 user32 shell32 gdi32)
	else (WIN32)
		target_link_libraries(torrent crypto ssl)
	endif (WIN32)
else (ENABLE_ENCRYPTION)
	add_definitions(-DTORRENT_DISABLE_ENCRYPTION)
endif (ENABLE_ENCRYPTION)

if (NOT USE_POOL_ALLOCATORS)
	add_definitions(-DTORRENT_DISABLE_POOL_ALLOCATOR)
endif (NOT USE_POOL_ALLOCATORS)

if (NOT ENABLE_GEOIP)
	add_definitions(-DTORRENT_DISABLE_GEO_IP)
endif (NOT ENABLE_GEOIP)

if (NOT ENABLE_RESOLVE_COUNTRIES)
	add_definitions(-DTORRENT_DISABLE_RESOLVE_COUNTRIES)
endif (NOT ENABLE_RESOLVE_COUNTRIES)

if (ENABLE_UNICODE)
	add_definitions(-DUNICODE -D_UNICODE)
endif (ENABLE_UNICODE)

if (NOT WITH_DEPRECATED_FUNCTIONS)
	add_definitions(-DTORRENT_NO_DEPRECATE)
endif (NOT WITH_DEPRECATED_FUNCTIONS)

if (MSVC)
# disable bogus deprecation warnings on msvc8
	add_definitions(-D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE)
# these compiler settings just makes the compiler standard conforming
	add_definitions(/Zc:wchar_t /Zc:forScope)

#	<toolset>msvc,<variant>release:<linkflags>/OPT:ICF=5
#	<toolset>msvc,<variant>release:<linkflags>/OPT:REF
endif(MSVC)

message(${CMAKE_CONFIGURATION_TYPES})

add_definitions(-D_FILE_OFFSET_BITS=64)

if (WITH_TCMALLOC)
	target_link_libraries(torrent tcmalloc)
endif (WITH_TCMALLOC)

target_link_libraries(torrent z)
include_directories(${includes})

set(examples client_test dump_torrent simple_client enum_if make_torrent)

foreach(s ${examples})
	add_executable(${s} examples/${s}.cpp)
	target_link_libraries(${s} torrent)
endforeach(s)

find_library(bpo boost_program_options-mt-1_35 ${library_search_paths})
target_link_libraries(client_test ${bpo})
find_library(regex boost_regex-mt-1_35 ${library_search_paths})
target_link_libraries(client_test ${regex})

