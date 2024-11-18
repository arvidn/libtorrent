cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR) # Configurable policies: <= CMP0097

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
	add_torrent_params.hpp
	address.hpp
	alert.hpp
	alert_types.hpp
	announce_entry.hpp
	assert.hpp
	bdecode.hpp
	bencode.hpp
	bitfield.hpp
	client_data.hpp
	close_reason.hpp
	config.hpp
	create_torrent.hpp
	disk_buffer_holder.hpp
	disk_interface.hpp
	disk_observer.hpp
	download_priority.hpp
	entry.hpp
	error.hpp
	error_code.hpp
	extensions.hpp
	file_storage.hpp
	file_layout.hpp
	fingerprint.hpp
	flags.hpp
	fwd.hpp
	gzip.hpp
	hasher.hpp
	hex.hpp
	i2p_stream.hpp
	identify_client.hpp
	index_range.hpp
	io_service.hpp
	ip_filter.hpp
	libtorrent.hpp
	magnet_uri.hpp
	mmap_disk_io.hpp
	natpmp.hpp
	operations.hpp
	peer_class.hpp
	peer_class_type_filter.hpp
	peer_connection_handle.hpp
	peer_connection_interface.hpp
	peer_id.hpp
	peer_info.hpp
	peer_request.hpp
	performance_counters.hpp
	pex_flags.hpp
	piece_block.hpp
	portmap.hpp
	read_resume_data.hpp
	session.hpp
	session_handle.hpp
	session_params.hpp
	session_settings.hpp
	session_stats.hpp
	session_status.hpp
	session_types.hpp
	settings_pack.hpp
	sha1_hash.hpp
	socket.hpp
	socket_type.hpp
	socks5_stream.hpp
	span.hpp
	storage_defs.hpp
	string_view.hpp
	time.hpp
	torrent_flags.hpp
	torrent_handle.hpp
	torrent_info.hpp
	torrent_status.hpp
	tracker_event.hpp
	truncate.hpp
	units.hpp
	upnp.hpp
	version.hpp
	web_seed_entry.hpp
	write_resume_data.hpp
)

set(libtorrent_kademlia_include_files
	announce_flags.hpp
	dht_observer.hpp
	dht_settings.hpp
	dht_state.hpp
	dht_storage.hpp
	dht_tracker.hpp
	direct_request.hpp
	dos_blocker.hpp
	ed25519.hpp
	find_data.hpp
	get_item.hpp
	get_peers.hpp
	io.hpp
	item.hpp
	msg.hpp
	node.hpp
	node_entry.hpp
	node_id.hpp
	observer.hpp
	put_data.hpp
	refresh.hpp
	routing_table.hpp
	rpc_manager.hpp
	sample_infohashes.hpp
	traversal_algorithm.hpp
	types.hpp
)

set(libtorrent_extensions_include_files
	smart_ban.hpp
	ut_metadata.hpp
	ut_pex.hpp
)

set(libtorrent_aux_include_files
	alert_manager.hpp
	alloca.hpp
	allocating_handler.hpp
	apply_pad_files.hpp
	array.hpp
	bandwidth_limit.hpp
	bandwidth_manager.hpp
	bandwidth_queue_entry.hpp
	bandwidth_socket.hpp
	bencoder.hpp
	bind_to_device.hpp
	bloom_filter.hpp
	bt_peer_connection.hpp
	buffer.hpp
	byteswap.hpp
	chained_buffer.hpp
	choker.hpp
	copy_ptr.hpp
	cpuid.hpp
	crc32c.hpp
	deadline_timer.hpp
	debug.hpp
	debug_disk_thread.hpp
	deferred_handler.hpp
	deprecated.hpp
	deque.hpp
	dev_random.hpp
	directory.hpp
	disable_warnings_pop.hpp
	disable_warnings_push.hpp
	disk_buffer_pool.hpp
	disk_cache.hpp
	visit_block_iovecs.hpp
	disk_completed_queue.hpp
	mmap_disk_job.hpp
	disk_job.hpp
	disk_io_thread_pool.hpp
	disk_job_fence.hpp
	disk_job_pool.hpp
	drive_info.hpp
	ed25519.hpp
	enum_net.hpp
	escape_string.hpp
	export.hpp
	ffs.hpp
	file.hpp
	file_descriptor.hpp
	file_progress.hpp
	file_view_pool.hpp
	file_pool.hpp
	file_pool_impl.hpp
	has_block.hpp
	hash_picker.hpp
	heterogeneous_queue.hpp
	http_connection.hpp
	http_parser.hpp
	http_stream.hpp
	http_tracker_connection.hpp
	instantiate_connection.hpp
	invariant_check.hpp
	io.hpp
	io_bytes.hpp
	ip_helpers.hpp
	ip_notifier.hpp
	ip_voter.hpp
	keepalive.hpp
	link.hpp
	listen_socket_handle.hpp
	lsd.hpp
	merkle.hpp
	merkle_tree.hpp
	mmap_storage.hpp
	netlink.hpp
	netlink_utils.hpp
	noexcept_movable.hpp
	numeric_cast.hpp
	packet_buffer.hpp
	packet_pool.hpp
	parse_url.hpp
	part_file.hpp
	path.hpp
	peer.hpp
	peer_class_set.hpp
	peer_connection.hpp
	peer_list.hpp
	piece_block_progress.hpp
	piece_picker.hpp
	platform_util.hpp
	polymorphic_socket.hpp
	pool.hpp
	portmap.hpp
	posix_part_file.hpp
	posix_storage.hpp
	pread_disk_job.hpp
	proxy_base.hpp
	proxy_settings.hpp
	puff.hpp
	random.hpp
	range.hpp
	readwrite.hpp
	receive_buffer.hpp
	request_blocks.hpp
	resolve_links.hpp
	resolver.hpp
	resolver_interface.hpp
	rtc_signaling.hpp
	rtc_stream.hpp
	scope_end.hpp
	session_call.hpp
	session_impl.hpp
	session_interface.hpp
	session_settings.hpp
	session_udp_sockets.hpp
	set_socket_buffer.hpp
	set_traffic_class.hpp
	sha1.hpp
	sha256.hpp
	sha512.hpp
	sliding_average.hpp
	socket_io.hpp
	socket_type.hpp
	ssl.hpp
	ssl_stream.hpp
	stack_allocator.hpp
	stat.hpp
	stat_cache.hpp
	storage_array.hpp
	storage_free_list.hpp
	storage_utils.hpp
	string_ptr.hpp
	string_util.hpp
	strview_less.hpp
	suggest_piece.hpp
	tailqueue.hpp
	throw.hpp
	time.hpp
	timestamp_history.hpp
	torrent.hpp
	torrent_impl.hpp
	torrent_list.hpp
	torrent_peer.hpp
	torrent_peer_allocator.hpp
	tracker_list.hpp
	tracker_manager.hpp
	udp_socket.hpp
	udp_tracker_connection.hpp
	union_endpoint.hpp
	unique_ptr.hpp
	utf8.hpp
	utp_socket_manager.hpp
	utp_stream.hpp
	vector.hpp
	vector_utils.hpp
	web_connection_base.hpp
	web_peer_connection.hpp
	websocket_stream.hpp
	websocket_tracker_connection.hpp
	win_crypto_provider.hpp
	win_file_handle.hpp
	win_util.hpp
	xml_parse.hpp
)

set(try_signal_include_files
	try_signal
	signal_error_code
	try_signal_mingw
	try_signal_msvc
	try_signal_posix
)

set(sources
	add_torrent_params.cpp
	alert.cpp
	alert_manager.cpp
	announce_entry.cpp
	assert.cpp
	bandwidth_limit.cpp
	bandwidth_manager.cpp
	bandwidth_queue_entry.cpp
	bdecode.cpp
	bitfield.cpp
	bloom_filter.cpp
	bt_peer_connection.cpp
	chained_buffer.cpp
	choker.cpp
	close_reason.cpp
	copy_file.cpp
	cpuid.cpp
	crc32c.cpp
	create_torrent.cpp
	directory.cpp
	disabled_disk_io.cpp
	disk_buffer_holder.cpp
	disk_buffer_pool.cpp
	disk_cache.cpp
	disk_completed_queue.cpp
	disk_io_thread_pool.cpp
	disk_job_fence.cpp
	disk_job_pool.cpp
	drive_info.cpp
	entry.cpp
	enum_net.cpp
	error_code.cpp
	escape_string.cpp
	ffs.cpp
	file.cpp
	file_progress.cpp
	file_storage.cpp
	file_pool_impl.cpp
	fingerprint.cpp
	generate_peer_id.cpp
	gzip.cpp
	hash_picker.cpp
	hasher.cpp
	hex.cpp
	http_connection.cpp
	http_parser.cpp
	http_tracker_connection.cpp
	i2p_stream.cpp
	identify_client.cpp
	instantiate_connection.cpp
	ip_filter.cpp
	ip_helpers.cpp
	ip_notifier.cpp
	ip_voter.cpp
	listen_socket_handle.cpp
	load_torrent.cpp
	lsd.cpp
	magnet_uri.cpp
	merkle.cpp
	merkle_tree.cpp
	mmap.cpp
	mmap_disk_io.cpp
	disk_job.cpp
	mmap_storage.cpp
	natpmp.cpp
	packet_buffer.cpp
	parse_url.cpp
	part_file.cpp
	path.cpp
	peer_class.cpp
	peer_class_set.cpp
	peer_connection.cpp
	peer_connection_handle.cpp
	peer_info.cpp
	peer_list.cpp
	performance_counters.cpp
	piece_picker.cpp
	platform_util.cpp
	posix_disk_io.cpp
	posix_part_file.cpp
	posix_storage.cpp
	pread_disk_io.cpp
	pread_storage.cpp
	proxy_base.cpp
	proxy_settings.cpp
	puff.cpp
	random.cpp
	read_resume_data.cpp
	receive_buffer.cpp
	request_blocks.cpp
	resolve_links.cpp
	resolver.cpp
	rtc_signaling.cpp
	rtc_stream.cpp
	session.cpp
	session_call.cpp
	session_handle.cpp
	session_impl.cpp
	session_params.cpp
	session_settings.cpp
	session_stats.cpp
	settings_pack.cpp
	sha1.cpp
	sha1_hash.cpp
	sha256.cpp
	socket_io.cpp
	socket_type.cpp
	socks5_stream.cpp
	ssl.cpp
	stack_allocator.cpp
	stat.cpp
	stat_cache.cpp
	storage_utils.cpp
	string_util.cpp
	time.cpp
	timestamp_history.cpp
	torrent.cpp
	torrent_handle.cpp
	torrent_info.cpp
	torrent_peer.cpp
	torrent_peer_allocator.cpp
	torrent_status.cpp
	tracker_manager.cpp
	tracker_list.cpp
	truncate.cpp
	udp_socket.cpp
	udp_tracker_connection.cpp
	upnp.cpp
	utf8.cpp
	utp_socket_manager.cpp
	utp_stream.cpp
	version.cpp
	web_connection_base.cpp
	web_peer_connection.cpp
	web_seed_entry.cpp
	websocket_stream.cpp
	websocket_tracker_connection.cpp
	write_resume_data.cpp
	xml_parse.cpp

# -- extensions --
	smart_ban.cpp
	ut_pex.cpp
	ut_metadata.cpp
)

# -- kademlia --
set(kademlia_sources
	dht_settings.cpp
	dht_state.cpp
	dht_storage.cpp
	dht_tracker.cpp
	dos_blocker.cpp
	ed25519.cpp
	find_data.cpp
	get_item.cpp
	get_peers.cpp
	item.cpp
	msg.cpp
	node.cpp
	node_entry.cpp
	node_id.cpp
	put_data.cpp
	refresh.cpp
	routing_table.cpp
	rpc_manager.cpp
	sample_infohashes.cpp
	traversal_algorithm.cpp
)

# -- ed25519 --
set(ed25519_sources
	add_scalar.cpp
	fe.cpp
	ge.cpp
	key_exchange.cpp
	keypair.cpp
	sc.cpp
	sign.cpp
	verify.cpp
	sha512.cpp
	hasher512.cpp
)

set(try_signal_sources
	try_signal.cpp
	signal_error_code.cpp
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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_public_dependency(Threads REQUIRED)

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

add_library(torrent-rasterbar
	${sources}
	${try_signal_sources}
	${libtorrent_include_files}
	${libtorrent_extensions_include_files}
	${libtorrent_aux_include_files}
)

# C++ 17 support is required
target_compile_features(torrent-rasterbar
	PUBLIC
		cxx_std_17
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
		BOOST_SYSTEM_USE_UTF8
		_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
	PRIVATE
		TORRENT_BUILDING_LIBRARY
		BOOST_EXCEPTION_DISABLE
		BOOST_ASIO_HAS_STD_CHRONO
)

if (NOT WIN32)
	target_compile_definitions(torrent-rasterbar
		PRIVATE
			_FILE_OFFSET_BITS=64
	)
endif()

target_link_libraries(torrent-rasterbar
	PUBLIC
		Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
	target_compile_options(torrent-rasterbar PRIVATE
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
		-Wno-unsafe-buffer-usage
		-Wno-unknown-warning-option
	)
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
	target_compile_options(torrent-rasterbar PRIVATE
		-Wall
		-Wextra
		-Wpedantic
		-Wvla
		-Wno-c++11-compat
		-Wno-format-zero-length
		-Wno-noexcept-type
		-ftemplate-depth=512
	)
elseif(MSVC)
	target_compile_options(torrent-rasterbar PRIVATE
		# https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
		/Zc:__cplusplus
		/W4
		# C4251: 'identifier' : class 'type' needs to have dll-interface to be
		#        used by clients of class 'type2'
		/wd4251
		# C4268: 'identifier' : 'const' static/global data initialized
		#        with compiler generated default constructor fills the object with zeros
		/wd4268
		# C4275: non DLL-interface classkey 'identifier' used as base for
		#        DLL-interface classkey 'identifier'
		/wd4275
		# C4373: virtual function overrides, previous versions of the compiler
		#        did not override when parameters only differed by const/volatile qualifiers
		/wd4373
		# C4503: 'identifier': decorated name length exceeded, name was truncated
		/wd4503
	)
endif()

# Unconditional platform-specific settings
if (WIN32)
	target_link_libraries(torrent-rasterbar
		PUBLIC
			bcrypt mswsock ws2_32 iphlpapi
			debug dbghelp crypt32
	)

	add_definitions(-D_WIN32_WINNT=0x0A00) # target Windows 10 or later

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
				# allow larger .obj files (with more sections)
				/bigobj
				# https://learn.microsoft.com/en-us/cpp/build/reference/permissive-standards-conformance?view=msvc-170
				/permissive-
				# https://docs.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8?view=msvc-170
				/utf-8
				# https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
				/Zc:__cplusplus
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
	string(REPLACE "std::atomic<int>" "std::atomic<std::int8_t>" ATOMICS8_TEST_SOURCE "${ATOMICS_TEST_SOURCE}")
	string(REPLACE "std::atomic<int>" "std::atomic<std::int64_t>" ATOMICS64_TEST_SOURCE "${ATOMICS_TEST_SOURCE}")

	if(APPLE)
		set(CMAKE_REQUIRED_FLAGS "-std=c++11")
	endif()
	check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITHOUT_LIB)
	check_cxx_source_compiles("${ATOMICS8_TEST_SOURCE}" HAVE_CXX_ATOMICS8_WITHOUT_LIB)
	check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITHOUT_LIB)
	if((NOT HAVE_CXX_ATOMICS_WITHOUT_LIB) OR (NOT HAVE_CXX_ATOMICS8_WITHOUT_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB))
		set(CMAKE_REQUIRED_LIBRARIES "atomic")
		check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITH_LIB)
		check_cxx_source_compiles("${ATOMICS8_TEST_SOURCE}" HAVE_CXX_ATOMICS8_WITH_LIB)
		check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITH_LIB)
		if ((NOT HAVE_CXX_ATOMICS_WITH_LIB) OR (NOT HAVE_CXX_ATOMICS8_WITH_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITH_LIB))
			message(STATUS, "No native support for std::atomic, or libatomic not found! Build link step may fail")
		else()
			message(STATUS "Linking with libatomic for atomics support")
			unset(CMAKE_REQUIRED_LIBRARIES)
			target_link_libraries(torrent-rasterbar PUBLIC atomic)
		endif()
	endif()
	if(APPLE)
		unset(CMAKE_REQUIRED_FLAGS)
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
	DESCRIPTION "enable deprecated functions for backwards compatibility"
	ENABLED TORRENT_ABI_VERSION=2
	DISABLED TORRENT_ABI_VERSION=100)
feature_option(encryption "Enables encryption in libtorrent" ON)
feature_option(exceptions "build with exception support" ON)
feature_option(gnutls "build using GnuTLS instead of OpenSSL" OFF)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME extensions DEFAULT ON
	DESCRIPTION "Enables protocol extensions" DISABLED TORRENT_DISABLE_EXTENSIONS)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME i2p DEFAULT ON
	DESCRIPTION "build with I2P support" DISABLED TORRENT_USE_I2P=0)
target_optional_compile_definitions(torrent-rasterbar PUBLIC FEATURE NAME webtorrent DEFAULT OFF
	DESCRIPTION "build with WebTorrent support" DISABLED TORRENT_USE_RTC=0)
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
	target_sources(torrent-rasterbar PRIVATE include/libtorrent/aux_/pe_crypto.hpp src/pe_crypto.cpp)
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_ENCRYPTION)
endif()

if (dht)
	target_sources(torrent-rasterbar PRIVATE
		${libtorrent_kademlia_include_files}
		include/libtorrent/aux_/hasher512.hpp
		${kademlia_sources}
		${ed25519_sources}
	)
else()
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_DISABLE_DHT)
endif()

if (webtorrent)
	option(NO_WEBSOCKET "Disable WebSocket support in libdatachannel" ON)
	option(NO_MEDIA "Disable media transport support in libdatachannel" ON)
	if(GNUTLS_FOUND)
		option(USE_GNUTLS "Use GnuTLS instead of OpenSSL for libdatachannel" ON)
	else()
		option(USE_GNUTLS "Use GnuTLS instead of OpenSSL for libdatachannel" OFF)
	endif()
	add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)
	if(CMAKE_CXX_COMPILER_ID MATCHES Clang|GNU)
		target_compile_options(datachannel-static PRIVATE
			-Wno-pedantic
			-Wno-unused-parameter
			-Wno-unused-variable)
	endif()
endif()

# Boost
find_public_dependency(Boost REQUIRED)
target_link_libraries(torrent-rasterbar PUBLIC Boost::headers)
if (Boost_MAJOR_VERSION LESS_EQUAL 1 AND Boost_MINOR_VERSION LESS 69)
	find_public_dependency(Boost REQUIRED COMPONENTS system)
	target_link_libraries(torrent-rasterbar PUBLIC Boost::system)
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

if(webtorrent)
	target_link_libraries(torrent-rasterbar PRIVATE LibDataChannel::LibDataChannelStatic)

	# Boost.JSON was added to Boost in version 1.75
	find_package(Boost OPTIONAL_COMPONENTS json)
	target_compile_definitions(torrent-rasterbar PRIVATE BOOST_JSON_HEADER_ONLY)
	if(Boost_JSON_FOUND)
		target_link_libraries(torrent-rasterbar PUBLIC Boost::json)
	else()
		# Fallback to the submodule
		target_include_directories(torrent-rasterbar PRIVATE deps/json/include)
	endif()
endif()

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

if (MSVC)
	set_target_properties(torrent-rasterbar
	PROPERTIES
		PDB_NAME torrent-rasterbar
		PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
		COMPILE_PDB_NAME torrent-rasterbar
		COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
	)

	if (static_runtime)
		set(PDB_INSTALL_DIR lib)
	else()
		set(PDB_INSTALL_DIR bin)
	endif()

	install(
		FILES
			${CMAKE_BINARY_DIR}/torrent-rasterbar.pdb
		DESTINATION
			${PDB_INSTALL_DIR}
		CONFIGURATIONS
			Debug RelWithDebInfo
		OPTIONAL
	)
endif()

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
