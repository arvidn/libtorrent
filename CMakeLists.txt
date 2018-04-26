cmake_minimum_required(VERSION 3.9.0 FATAL_ERROR)

project(libtorrent
	DESCRIPTION "Bittorrent library"
	VERSION 1.2.0
)
set (SOVERSION "10")

list(APPEND CMAKE_MODULE_PATH ${libtorrent_SOURCE_DIR}/cmake/Modules)
include(GNUInstallDirs)
include(GeneratePkgConfig)

set(libtorrent_include_files
	add_torrent_params
	address
	alert
	alert_manager
	alert_types
	allocator
	announce_entry
	assert
	bandwidth_limit
	bandwidth_manager
	bandwidth_queue_entry
	bandwidth_socket
	bdecode
	bencode
	bitfield
	block_cache
	bloom_filter
	broadcast_socket
	bt_peer_connection
	buffer
	chained_buffer
	choker
	close_reason
	config
	ConvertUTF
	copy_ptr
	crc32c
	create_torrent
	deadline_timer
	debug
	disk_buffer_holder
	disk_buffer_pool
	disk_interface
	disk_io_job
	disk_io_thread
	disk_io_thread_pool
	disk_job_pool
	disk_observer
	download_priority
	ed25519
	entry
	enum_net
	error
	error_code
	export
	extensions
	file
	file_pool
	file_storage
	fingerprint
	flags
	fwd
	gzip
	hasher
	hasher512
	heterogeneous_queue
	hex
	http_connection
	http_parser
	http_seed_connection
	http_stream
	http_tracker_connection
	i2p_stream
	identify_client
	instantiate_connection
	invariant_check
	io
	io_service
	io_service_fwd
	ip_filter
	ip_voter
	lazy_entry
	link
	linked_list
	lsd
	magnet_uri
	natpmp
	netlink
	operations
	optional
	packet_buffer
	packet_pool
	parse_url
	part_file
	pe_crypto
	peer
	peer_class
	peer_class_set
	peer_class_type_filter
	peer_connection
	peer_connection_handle
	peer_connection_interface
	peer_id
	peer_info
	peer_list
	peer_request
	performance_counters
	pex_flags
	piece_block
	piece_block_progress
	piece_picker
	platform_util
	portmap
	proxy_base
	puff
	random
	read_resume_data
	receive_buffer
	request_blocks
	resolve_links
	resolver
	resolver_interface
	session
	session_handle
	session_settings
	session_stats
	session_status
	session_types
	settings_pack
	sha1
	sha1_hash
	sha512
	sliding_average
	socket
	socket_io
	socks5_stream
	span
	ssl_stream
	stack_allocator
	stat
	stat_cache
	storage
	storage_defs
	string_util
	string_view
	tailqueue
	time
	timestamp_history
	torrent
	torrent_flags
	torrent_handle
	torrent_info
	torrent_peer
	torrent_peer_allocator
	torrent_status
	tracker_manager
	udp_socket
	udp_tracker_connection
	union_endpoint
	units
	upnp
	utf8
	utp_socket_manager
	utp_stream
	vector_utils
	version
	web_connection_base
	web_peer_connection
	write_resume_data
	xml_parse)

set(libtorrent_kademlia_include_files
	dht_observer
	dht_settings
	dht_state
	dht_storage
	dht_tracker
	direct_request
	dos_blocker
	ed25519
	find_data
	get_item
	get_peers
	io
	item
	msg
	node
	node_entry
	node_id
	observer
	put_data
	refresh
	routing_table
	rpc_manager
	sample_infohashes
	traversal_algorithm
	types)

set(libtorrent_extensions_include_files
	smart_ban
	ut_metadata
	ut_pex)

set(libtorrent_aux_include_files
	aligned_storage
	aligned_union
	alloca
	allocating_handler
	array
	bind_to_device
	block_cache_reference
	byteswap
	cppint_import_export
	cpuid
	deferred_handler
	deque
	dev_random
	disable_warnings_pop
	disable_warnings_push
	disk_job_fence
	escape_string
	ffs
	file_progress
	has_block
	io
	ip_notifier
	listen_socket_handle
	lsd
	max_path
	merkle
	noexcept_movable
	numeric_cast
	openssl
	path
	portmap
	proxy_settings
	range
	route
	scope_end
	session_call
	session_impl
	session_interface
	session_settings
	session_udp_sockets
	set_socket_buffer
	socket_type
	storage_piece_set
	storage_utils
	string_ptr
	suggest_piece
	throw
	time
	torrent_impl
	typed_span
	unique_ptr
	vector
	win_crypto_provider
	win_util)

foreach(s ${libtorrent_include_files})
	list(APPEND include_files include/libtorrent/${s})
endforeach(s)

foreach(s ${libtorrent_kademlia_include_files})
	list(APPEND include_files include/libtorrent/kademlia/${s})
endforeach(s)

foreach(s ${libtorrent_extensions_include_files})
	list(APPEND include_files include/libtorrent/extensions/${s})
endforeach(s)

foreach(s ${libtorrent_aux_include_files})
	list(APPEND include_files include/libtorrent/aux_/${s})
endforeach(s)

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
option(python-bindings "build python bindings" OFF)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release FORCE)
endif()

# add_definitions() doesn't seem to let you say wich build type to apply it to
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DTORRENT_DEBUG")
if(UNIX)
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

find_package(Threads REQUIRED)
set(_package_dependencies "Threads") # Dependencies to be used below for generating Config.cmake file

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

list(APPEND sources2 ${include_files})

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
	add_compile_options(
		-Weverything
		-Wno-documentation
		-Wno-c++98-compat-pedantic
		-Wno-padded
		-Wno-global-constructors
		-Wno-exit-time-destructors
		-Wno-weak-vtables)
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
	add_compile_options(
		-Wall
		-Wextra
		-Wpedantic
		-Wparentheses
		-Wvla
		-Wc++11-compat
		-Wno-format-zero-length
		-ftemplate-depth=512)
elseif(MSVC)
	add_compile_options(
		/W4
		# C4251: 'identifier' : class 'type' needs to have dll-interface to be
		#        used by clients of class 'type2'
		/wd4251
		# C4275: non DLL-interface classkey 'identifier' used as base for
		#        DLL-interface classkey 'identifier'
		/wd4275
		# C4373: virtual function overrides, previous versions of the compiler
		#        did not override when parameters only differed by const/volatile qualifiers
		/wd4373
		# C4268: 'identifier' : 'const' static/global data initialized
		#        with compiler generated default constructor fills the object with zeros
		/wd4268
		# C4503: 'identifier': decorated name length exceeded, name was truncated
		/wd4503)
endif()

if (shared)
	add_library(torrent-rasterbar SHARED ${sources2})
	target_compile_definitions(torrent-rasterbar
		PRIVATE TORRENT_BUILDING_SHARED
		INTERFACE TORRENT_LINKING_SHARED
	)
	set_target_properties(torrent-rasterbar
		PROPERTIES
			CXX_VISIBILITY_PRESET "hidden"
			VISIBILITY_INLINES_HIDDEN "true"
			VERSION ${PROJECT_VERSION}
			SOVERSION ${SOVERSION}
	)
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

target_compile_features(torrent-rasterbar PUBLIC cxx_std_11)
target_compile_definitions(torrent-rasterbar
	PUBLIC
		$<$<CONFIG:Debug>:TORRENT_DEBUG>
		$<$<CONFIG:Debug>:TORRENT_USE_ASSERTS>
	PRIVATE
		TORRENT_BUILDING_LIBRARY
)
target_link_libraries(torrent-rasterbar
	PUBLIC
		Threads::Threads
)

if (libiconv)
	find_package(Iconv REQUIRED)
	list(APPEND _package_dependencies "Iconv")
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_ICONV)
	target_include_directories(torrent-rasterbar PUBLIC ${ICONV_INCLUDE_DIR})
	target_link_libraries(torrent-rasterbar PRIVATE ${ICONV_LIBRARIES})
endif (libiconv)

if (encryption)
	if(NOT TARGET OpenSSL::SSL)
		find_package(OpenSSL REQUIRED)
	endif()
	list(APPEND _package_dependencies "OpenSSL")
	target_link_libraries(torrent-rasterbar PUBLIC OpenSSL::SSL)
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_OPENSSL TORRENT_USE_LIBCRYPTO)
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
find_package(Boost REQUIRED COMPONENTS system)
target_include_directories(torrent-rasterbar PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(torrent-rasterbar PUBLIC ${Boost_SYSTEM_LIBRARY})
list(APPEND _package_dependencies "Boost COMPONENTS system")

if (WIN32)
	target_link_libraries(torrent-rasterbar
		PRIVATE
			wsock32 ws2_32 Iphlpapi
			debug dbghelp
	)

	target_compile_definitions(torrent-rasterbar
		PUBLIC
			_WIN32_WINNT=0x0600 # target Windows Vista or later
			WIN32_LEAN_AND_MEAN # prevent winsock1 to be included
	)
	if (MSVC)
		target_compile_definitions(torrent-rasterbar PUBLIC BOOST_ALL_NO_LIB)
	endif()
endif()

if(ANDROID)
    target_link_libraries(torrent-rasterbar PRIVATE ${CMAKE_DL_LIBS})
endif()

if(APPLE)
	# for ip_notifier
	target_link_libraries(torrent-rasterbar PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
endif (APPLE)

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
	target_compile_definitions(torrent-rasterbar
		PUBLIC
			_SCL_SECURE_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE # disable bogus deprecation warnings on msvc8
	)
	target_compile_options(torrent-rasterbar
		PRIVATE
			/Zc:wchar_t /Zc:forScope # these compiler settings just make the compiler standard conforming
			/MP # for multi-core compilation
			/bigobj # increase the number of sections for obj files
	)
endif()

target_compile_definitions(torrent-rasterbar
	PUBLIC
		_FILE_OFFSET_BITS=64
		BOOST_EXCEPTION_DISABLE
		BOOST_ASIO_ENABLE_CANCELIO
		BOOST_ASIO_HAS_STD_CHRONO
)

generate_and_install_pkg_config_file(torrent-rasterbar)

include(CheckCXXCompilerFlag)

add_subdirectory(bindings)

install(TARGETS torrent-rasterbar EXPORT torrent-rasterbarTargets
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/libtorrent DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h*")

# === generate a CMake Config File ===
include(CMakePackageConfigHelpers)
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/libtorrent-rasterbar)
string(REGEX REPLACE "([^;]+)" "find_dependency(\\1)" _find_dependency_calls "${_package_dependencies}")
string(REPLACE ";" "\n" _find_dependency_calls "${_find_dependency_calls}")

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfigVersion.cmake"
	VERSION ${libtorrent_VERSION}
	COMPATIBILITY ExactVersion
)

export(EXPORT torrent-rasterbarTargets
	FILE "${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarTargets.cmake"
	NAMESPACE Libtorrent::
)

configure_package_config_file(libtorrent-rasterbarConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfig.cmake"
	INSTALL_DESTINATION "${ConfigPackageLocation}"
	NO_SET_AND_CHECK_MACRO
	NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

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
		"${CMAKE_CURRENT_BINARY_DIR}/libtorrent-rasterbar/libtorrent-rasterbarConfig.cmake"
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
	enable_testing()
	# this will make some internal functions available in the DLL interface
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_EXPORT_EXTRA)
	add_subdirectory(test)
endif()
