cmake_minimum_required(VERSION 3.15.0 FATAL_ERROR) # Configurable policies: <= CMP0094

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0092 NEW)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules)
include(LibtorrentMacros)

read_version("${CMAKE_CURRENT_SOURCE_DIR}/include/libtorrent/version.hpp" VER_MAJOR VER_MINOR VER_TINY)

project(libtorrent
	DESCRIPTION "Bittorrent library"
	VERSION ${VER_MAJOR}.${VER_MINOR}.${VER_TINY}
)
set (SOVERSION "${VER_MAJOR}.${VER_MINOR}")

include(GNUInstallDirs)
include(GeneratePkgConfig)

set(libtorrent_include_files
	add_torrent_params
	address
	alert
	alert_types
	announce_entry
	assert
	bdecode
	bencode
	bitfield
	bloom_filter
	bt_peer_connection
	choker
	client_data
	close_reason
	config
	copy_ptr
	crc32c
	create_torrent
	deadline_timer
	debug
	disk_buffer_holder
	disk_interface
	disk_observer
	download_priority
	entry
	enum_net
	error
	error_code
	extensions
	file
	file_storage
	fingerprint
	flags
	fwd
	gzip
	hash_picker
	hasher
	hex
	http_connection
	http_parser
	http_seed_connection
	http_stream
	http_tracker_connection
	i2p_stream
	identify_client
	index_range
	io
	io_service
	ip_filter
	ip_voter
	libtorrent
	link
	lsd
	magnet_uri
	mmap_disk_io
	mmap_storage
	natpmp
	netlink
	operations
	optional
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
	request_blocks
	resolve_links
	session
	session_handle
	session_params
	session_settings
	session_stats
	session_status
	session_types
	settings_pack
	sha1
	sha1_hash
	sha256
	sliding_average
	socket
	socket_io
	socket_type
	socks5_stream
	span
	ssl_stream
	stack_allocator
	stat
	stat_cache
	storage_defs
	string_util
	string_view
	tailqueue
	time
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
	vector_utils
	version
	web_connection_base
	web_peer_connection
	write_resume_data
	xml_parse
	ssl)

set(libtorrent_kademlia_include_files
	announce_flags
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
	alert_manager
	aligned_storage
	aligned_union
	alloca
	allocating_handler
	array
	bandwidth_limit
	bandwidth_manager
	bandwidth_queue_entry
	bandwidth_socket
	bind_to_device
	buffer
	keepalive
	byteswap
	chained_buffer
	cpuid
	deferred_handler
	deprecated
	deque
	dev_random
	directory
	disable_warnings_pop
	disable_warnings_push
	disk_buffer_pool
	disk_io_job
	disk_io_thread_pool
	disk_job_fence
	disk_job_pool
	ed25519
	escape_string
	export
	ffs
	file_progress
	file_view_pool
	has_block
	heterogeneous_queue
	invariant_check
	instantiate_connection
	io
	ip_helpers
	ip_notifier
	listen_socket_handle
	lsd
	merkle
	merkle_tree
	noexcept_movable
	numeric_cast
	packet_buffer
	packet_pool
	path
	polymorphic_socket
	pool
	portmap
	posix_part_file
	proxy_settings
	range
	receive_buffer
	resolver
	resolver_interface
	route
	scope_end
	session_call
	session_impl
	session_interface
	session_settings
	session_udp_sockets
	set_socket_buffer
	socket_type
	storage_utils
	string_ptr
	strview_less
	suggest_piece
	throw
	time
	timestamp_history
	torrent_impl
	torrent_list
	unique_ptr
	utp_socket_manager
	utp_stream
	vector
	win_crypto_provider
	win_util)

set(try_signal_include_files
	try_signal
	signal_error_code
	try_signal_mingw
	try_signal_msvc
	try_signal_posix
)

set(sources
	web_connection_base
	alert
	alert_manager
	announce_entry
	assert
	bandwidth_limit
	bandwidth_manager
	bandwidth_queue_entry
	bdecode
	bitfield
	bloom_filter
	chained_buffer
	choker
	close_reason
	cpuid
	crc32c
	create_torrent
	directory
	disk_buffer_holder
	entry
	error_code
	file_view_pool
	file_storage
	file_progress
	generate_peer_id
	escape_string
	string_util
	file
	path
	fingerprint
	gzip
	hasher
	sha1
	sha256
	hash_picker
	hex
	http_connection
	http_parser
	i2p_stream
	identify_client
	ip_filter
	ip_helpers
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
	merkle_tree
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
	session_params
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
	lsd
	disk_io_job
	disk_job_fence
	disk_job_pool
	disk_buffer_pool
	disk_interface
	disk_io_thread_pool
	disabled_disk_io
	enum_net
	magnet_uri
	parse_url
	xml_parse
	version
	ffs
	add_torrent_params
	peer_info
	stack_allocator
	mmap
	mmap_disk_io
	mmap_storage
	posix_disk_io
	posix_part_file
	posix_storage
	ssl

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
	sha512
	hasher512
)

set(try_signal_sources
	try_signal
	signal_error_code
)

list(TRANSFORM sources PREPEND "src/")
list(TRANSFORM kademlia_sources PREPEND "src/kademlia/")
list(TRANSFORM ed25519_sources PREPEND "src/ed25519/")
list(TRANSFORM libtorrent_include_files PREPEND "include/libtorrent/")
list(TRANSFORM libtorrent_extensions_include_files PREPEND "include/libtorrent/extensions/")
list(TRANSFORM libtorrent_aux_include_files PREPEND "include/libtorrent/aux_/")
list(TRANSFORM libtorrent_kademlia_include_files PREPEND "include/libtorrent/kademlia/")
list(TRANSFORM try_signal_sources PREPEND "deps/try_signal/")

# these options control target creation and thus have to be declared before the add_library() call
feature_option(BUILD_SHARED_LIBS "build libtorrent as a shared library" ON)
feature_option(static_runtime "build libtorrent with static runtime" OFF)

find_public_dependency(Threads REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
	add_compile_options(
		-Weverything
		-Wno-documentation
		-Wno-c++98-compat-pedantic
		-Wno-c++11-compat-pedantic
		-Wno-padded
		-Wno-alloca
		-Wno-global-constructors
		-Wno-exit-time-destructors
		-Wno-weak-vtables
		-Wno-return-std-move-in-c++11
		-Wno-unknown-warning-option)
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
	add_compile_options(
		-Wall
		-Wextra
		-Wpedantic
		-Wparentheses
		-Wvla
		-Wno-c++11-compat
		-Wno-format-zero-length
		-Wno-noexcept-type
		-ftemplate-depth=512)
elseif(MSVC)
	add_compile_options(
		# https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
		/Zc:__cplusplus
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

if(static_runtime)
	if (MSVC)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	else()
		include(ucm_flags)
		ucm_set_runtime(STATIC)
	endif()
	set(Boost_USE_MULTITHREADED ON)
	set(Boost_USE_STATIC_RUNTIME ON)
	set(OPENSSL_MSVC_STATIC_RT ON)
endif()

if (NOT BUILD_SHARED_LIBS)
	set(Boost_USE_STATIC_LIBS ON)
	set(OPENSSL_USE_STATIC_LIBS ON)
endif()

add_library(torrent-rasterbar
	${sources}
	${try_signal_sources}
	${libtorrent_include_files}
	${libtorrent_extensions_include_files}
	${libtorrent_aux_include_files}
)

# C++ 14 support is required
target_compile_features(torrent-rasterbar
	PUBLIC
		cxx_std_14
		cxx_contextual_conversions
		cxx_binary_literals
		cxx_decltype_auto
		cxx_lambda_init_captures
		cxx_generic_lambdas
		cxx_variable_templates
		cxx_relaxed_constexpr
		cxx_attribute_deprecated
		cxx_digit_separators
)

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

target_include_directories(torrent-rasterbar PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	PRIVATE deps/try_signal
)

target_compile_definitions(torrent-rasterbar
	PUBLIC
		$<$<CONFIG:Debug>:TORRENT_USE_ASSERTS>
		BOOST_ASIO_ENABLE_CANCELIO
		BOOST_ASIO_NO_DEPRECATED
	PRIVATE
		TORRENT_BUILDING_LIBRARY
		_FILE_OFFSET_BITS=64
		BOOST_EXCEPTION_DISABLE
		BOOST_ASIO_HAS_STD_CHRONO
)

target_link_libraries(torrent-rasterbar
	PUBLIC
		Threads::Threads
)

# Unconditional platform-specific settings
if (WIN32)
	target_link_libraries(torrent-rasterbar
		PUBLIC
			bcrypt mswsock ws2_32 Iphlpapi
			debug dbghelp crypt32
	)

	add_definitions(-D_WIN32_WINNT=0x0600) # target Windows Vista or later

	target_compile_definitions(torrent-rasterbar
		PUBLIC WIN32_LEAN_AND_MEAN # prevent winsock1 to be included
	)

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
		set_target_properties(torrent-rasterbar PROPERTIES LINK_FLAGS_RELEASE "/OPT:ICF=5 /OPT:REF")
	endif()
endif()

if (ANDROID)
	target_link_libraries(torrent-rasterbar PRIVATE ${CMAKE_DL_LIBS})
endif()

if (APPLE)
	# for ip_notifier
	target_link_libraries(torrent-rasterbar PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
endif()

# check if we need to link with libatomic (not needed on MSVC)
if (NOT Windows)
	# TODO: migrate to CheckSourceCompiles in CMake >= 3.19
	include(CheckCXXSourceCompiles)

	set(ATOMICS_TEST_SOURCE [=[
		#include <atomic>
		#include <cstdint>
		std::atomic<int> x{0};
		int main() {
			x.fetch_add(1, std::memory_order_relaxed);
			return 0;
		}
	]=])
	string(REPLACE "std::atomic<int>" "std::atomic<std::int64_t>" ATOMICS64_TEST_SOURCE "${ATOMICS_TEST_SOURCE}")

	check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITHOUT_LIB)
	check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITHOUT_LIB)
	if((NOT HAVE_CXX_ATOMICS_WITHOUT_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB))
		set(CMAKE_REQUIRED_LIBRARIES "atomic")
		check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITH_LIB)
		check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITH_LIB)
		if ((NOT HAVE_CXX_ATOMICS_WITH_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITH_LIB))
			message(FATAL_ERROR "No native support for std::atomic, or libatomic not found!")
		else()
			message(STATUS "Linking with libatomic for atomics support")
			unset(CMAKE_REQUIRED_LIBRARIES)
			target_link_libraries(torrent-rasterbar PUBLIC atomic)
		endif()
	endif()
endif()

feature_option(build_tests "build tests" OFF)
feature_option(build_examples "build examples" OFF)
feature_option(build_tools "build tools" OFF)
feature_option(python-bindings "build python bindings" OFF)
feature_option(python-egg-info "generate python egg info" OFF)
feature_option(python-install-system-dir "Install python bindings to the system installation directory rather than the CMake installation prefix" OFF)

# these options require existing target
feature_option(dht "enable support for Mainline DHT" ON)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME deprecated-functions DEFAULT ON
	DESCRIPTION "enable deprecated functions for backwards compatibility" DISABLED TORRENT_NO_DEPRECATE)
feature_option(encryption "Enables encryption in libtorrent" ON)
feature_option(exceptions "build with exception support" ON)
feature_option(gnutls "build using GnuTLS instead of OpenSSL" OFF)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME extensions DEFAULT ON
	DESCRIPTION "Enables protocol extensions" DISABLED TORRENT_DISABLE_EXTENSIONS)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME i2p DEFAULT ON
	DESCRIPTION "build with I2P support" DISABLED TORRENT_USE_I2P=0)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME logging DEFAULT ON
	DESCRIPTION "build with logging" DISABLED TORRENT_DISABLE_LOGGING)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME mutable-torrents DEFAULT ON
	DESCRIPTION "Enables mutable torrent support" DISABLED TORRENT_DISABLE_MUTABLE_TORRENTS)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME streaming DEFAULT ON
	DESCRIPTION "Enables support for piece deadline" DISABLED TORRENT_DISABLE_STREAMING)

if(NOT gnutls)
	find_public_dependency(OpenSSL)
	set_package_properties(OpenSSL
		PROPERTIES
			URL "https://www.openssl.org/"
			DESCRIPTION "Full-strength general purpose cryptography library"
			TYPE RECOMMENDED
			PURPOSE "Provides HTTPS support to libtorrent"
	)

	if(TARGET OpenSSL::SSL)
		# TODO: needed until https://gitlab.kitware.com/cmake/cmake/issues/19263 is fixed
		if(WIN32 AND OPENSSL_USE_STATIC_LIBS)
			target_link_libraries(torrent-rasterbar PRIVATE crypt32)
		endif()
		target_link_libraries(torrent-rasterbar PUBLIC OpenSSL::SSL)
		target_compile_definitions(torrent-rasterbar
			PUBLIC
				TORRENT_USE_OPENSSL
				TORRENT_USE_LIBCRYPTO
				TORRENT_SSL_PEERS
				OPENSSL_NO_SSL2)
	endif()
endif()

if(gnutls OR NOT TARGET OpenSSL::SSL)
	find_public_dependency(GnuTLS)
	set_package_properties(GnuTLS
		PROPERTIES
			URL "https://www.gnutls.org/"
			DESCRIPTION "GnuTLS is a free software implementation of the TLS and DTLS protocols"
			TYPE RECOMMENDED
			PURPOSE "Provides HTTPS support to libtorrent"
	)
	if(GNUTLS_FOUND)
		target_link_libraries(torrent-rasterbar PUBLIC GnuTLS::GnuTLS)
		target_compile_definitions(torrent-rasterbar
			PUBLIC
				TORRENT_USE_GNUTLS
				TORRENT_SSL_PEERS)
		target_include_directories(torrent-rasterbar PUBLIC
			$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/asio-gnutls/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
		install(DIRECTORY deps/asio-gnutls/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
	elseif(gnutls)
		message(FATAL_ERROR "GnuTLS library not found")
	endif()
endif()

if (NOT GNUTLS_FOUND AND NOT TARGET OpenSSL::SSL)
	if(TARGET OpenSSL::Crypto)
		target_link_libraries(torrent-rasterbar PUBLIC OpenSSL::Crypto)
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_LIBCRYPTO)
	else()
		find_public_dependency(LibGcrypt)
		set_package_properties(LibGcrypt
			PROPERTIES
				URL "https://www.gnupg.org/software/libgcrypt/index.html"
				DESCRIPTION "A general purpose cryptographic library"
				TYPE RECOMMENDED
				PURPOSE "Use GCrypt instead of the built-in functions for RC4 and SHA1"
		)
		if (LibGcrypt_FOUND)
			target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_LIBGCRYPT)
			target_link_libraries(torrent-rasterbar PRIVATE LibGcrypt::LibGcrypt)
		endif()
	endif()
endif()

if (encryption)
	target_sources(torrent-rasterbar PRIVATE src/pe_crypto)
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_ENCRYPTION)
endif()

if (dht)
	target_sources(torrent-rasterbar PRIVATE
		${kademlia_sources}
		${ed25519_sources}
		${libtorrent_kademlia_include_files}
	)
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_DHT)
endif()

# Boost
find_public_dependency(Boost REQUIRED)
target_include_directories(torrent-rasterbar PUBLIC ${Boost_INCLUDE_DIRS})
if (Boost_MAJOR_VERSION LESS_EQUAL 1 AND Boost_MINOR_VERSION LESS 69)
	find_package(Boost REQUIRED COMPONENTS system)
	target_link_libraries(torrent-rasterbar PUBLIC ${Boost_SYSTEM_LIBRARY})
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

# developer options
option(developer-options "Activates options useful for a developer")
if(developer-options)
	set(asserts "auto" CACHE STRING "use assertions")
	set_property(CACHE asserts PROPERTY STRINGS auto on off production system)
	if ("${asserts}" MATCHES "on|production|system")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_ASSERTS=1)
	endif()
	if ("${asserts}" STREQUAL "production")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_PRODUCTION_ASSERTS=1)
	elseif("${asserts}" STREQUAL "system")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_SYSTEM_ASSERTS=1)
	endif()

	target_optional_compile_definitions(torrent-rasterbar PUBLIC NAME asio-debugging DEFAULT OFF
		ENABLED TORRENT_ASIO_DEBUGGING)
	target_optional_compile_definitions(torrent-rasterbar PUBLIC NAME picker-debugging DEFAULT OFF
		ENABLED TORRENT_DEBUG_REFCOUNTS)
	set(invariant-checks "off" CACHE STRING "")
	set_property(CACHE invariant-checks PROPERTY STRINGS off on full)
	if (invariant-checks MATCHES "on|full")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_INVARIANT_CHECKS=1)
	endif()
	if (invariant-checks STREQUAL "full")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_EXPENSIVE_INVARIANT_CHECKS)
	endif()

	target_optional_compile_definitions(torrent-rasterbar PUBLIC NAME utp-log DEFAULT OFF
		ENABLED TORRENT_UTP_LOG_ENABLE)
	target_optional_compile_definitions(torrent-rasterbar PUBLIC NAME simulate-slow-read DEFAULT OFF
		ENABLED TORRENT_SIMULATE_SLOW_READ)
	option(debug-iterators "" OFF)
	if (debug-iterators)
		if (MSVC)
			target_compile_definitions(torrent-rasterbar PUBLIC _ITERATOR_DEBUG_LEVEL=2)
		endif()
		if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
			target_compile_definitions(torrent-rasterbar PUBLIC _GLIBCXX_DEBUG _GLIBCXX_DEBUG_PEDANTIC)
		endif()
	endif()
	target_optional_compile_definitions(torrent-rasterbar PUBLIC NAME profile-calls DEFAULT OFF
		ENABLED TORRENT_PROFILE_CALLS=1)
endif()

# This is best effort attempt to propagate whether the library was built with
# C++11 or not. It affects the ABI of entry. A client building with C++14 and
# linking against a libtorrent binary built with C++11 can still define
# TORRENT_CXX11_ABI
if ("${CMAKE_CXX_STANDARD}" STREQUAL "11")
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_CXX11_ABI)
endif()

# There is little to none support for using pkg-config with MSVC and most users won't bother with it.
# However, msys is a linux-like platform on Windows that do support/prefer using pkg-config.
if (NOT MSVC)
	generate_and_install_pkg_config_file(torrent-rasterbar libtorrent-rasterbar)
endif()

include(CheckCXXCompilerFlag)

add_subdirectory(bindings)

if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
	file(RELATIVE_PATH CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_PREFIX}" "${CMAKE_INSTALL_LIBDIR}")
endif()

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

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfigVersion.cmake"
	VERSION ${libtorrent_VERSION}
	COMPATIBILITY AnyNewerVersion
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
