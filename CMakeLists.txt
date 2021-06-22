cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR) # Configurable policies: <= CMP0097

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

project(libtorrent
	VERSION "1.2.14"
	DESCRIPTION "An efficient feature complete C++ bittorrent implementation"
	HOMEPAGE_URL "https://libtorrent.org"
	LANGUAGES C CXX
)

include(GNUInstallDirs)
include(GeneratePkgConfig)
include(LibtorrentMacros)

# This must be set before creating targets. Ignored on non-MSVC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${static_runtime}>>:DLL>")

# -- Features --
feature_option(NAME build_tests DESCRIPTION "build tests" DEFAULT OFF)
feature_option(NAME build_examples DESCRIPTION "build examples" DEFAULT OFF)
feature_option(NAME build_tools DESCRIPTION "build tools" DEFAULT OFF)
feature_option(NAME python-bindings DESCRIPTION "build python bindings" DEFAULT OFF)
feature_option(NAME python-egg-info DESCRIPTION "generate python egg info" DEFAULT OFF)
feature_option(NAME python-install-system-dir DESCRIPTION "Install python bindings to the system installation directory rather than the CMake installation prefix" DEFAULT OFF)
feature_option(NAME iconv DESCRIPTION "build libtorrent with iconv support" DEFAULT ON)
feature_option(NAME openssl DESCRIPTION "Build with OpenSSL, to provide HTTPS support to libtorrent" DEFAULT ON)
feature_option(NAME libgcrypt DESCRIPTION "Use LibGcrypt instead of OpenSSL or the built-in functions for RC4 and SHA1" DEFAULT OFF)
feature_option(NAME dht DESCRIPTION "enable support for Mainline DHT" DEFAULT ON)
feature_option(NAME encryption DESCRIPTION "Enables encryption in libtorrent" DEFAULT ON)
feature_option(NAME exceptions DESCRIPTION "build with exception support" DEFAULT ON)
feature_option(NAME deprecated-functions DESCRIPTION "enable deprecated functions for backwards compatibility" DEFAULT ON)
feature_option(NAME extensions DESCRIPTION "Enables protocol extensions" DEFAULT ON)
feature_option(NAME i2p DESCRIPTION "build with I2P support" DEFAULT ON)
feature_option(NAME logging DESCRIPTION "build with logging" DEFAULT ON)
feature_option(NAME mutable-torrents DESCRIPTION "Enables mutable torrent support" DEFAULT ON)
feature_option(NAME streaming DESCRIPTION "Enables support for piece deadline" DEFAULT ON)
feature_option(NAME BUILD_SHARED_LIBS DESCRIPTION "build libtorrent as a shared library" DEFAULT ON)
feature_option(NAME static_runtime DESCRIPTION "build libtorrent with static runtime" DEFAULT OFF)
feature_option(NAME developer-options DESCRIPTION "Activates advanced options useful for a developer" DEFAULT OFF)
if (developer-options)
	feature_option(NAME asio-debugging DESCRIPTION "Enable boost-asio debugging" DEFAULT OFF)
	feature_option(NAME picker-debugging DESCRIPTION "Enable piece picker debugging" DEFAULT OFF)
	feature_option(NAME debug-iterators DESCRIPTION "Enable iterator debugging" DEFAULT OFF)
	feature_option(NAME utp-log DESCRIPTION "Enable μTP logging" DEFAULT OFF)
	feature_option(NAME simulate-slow-read DESCRIPTION "Enable slow read simulation" DEFAULT OFF)
	feature_option(NAME profile-calls DESCRIPTION "Enable call profiling" DEFAULT OFF)
	feature_option_multivalue(NAME "asserts" DESCRIPTION "use assertions (only effective in \"Debug\" configuration)" DEFAULT "auto" ALLOWED_VALUES "auto" "off" "on" "production" "system")
	feature_option_multivalue(NAME "invariant-checks" DESCRIPTION "enable invariant checks" DEFAULT "off" ALLOWED_VALUES "off" "on" "full")
else()
	unset(asio-debugging CACHE)
	unset(picker-debugging CACHE)
	unset(debug-iterators CACHE)
	unset(utp-log CACHE)
	unset(simulate-slow-read CACHE)
	unset(profile-calls CACHE)
	unset(asserts CACHE)
	unset(invariant-checks CACHE)
endif()

feature_summary(DEFAULT_DESCRIPTION WHAT ALL)
feature_summary_multivalue(NAMES MULTIVALUE_FEATURE_NAMES DEFAULTS MULTIVALUE_FEATURE_DEFAULTS)

# -- Find dependencies --
if (static_runtime)
	if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))
		message(AUTHOR_WARNING "Currently, the build system does not guaranteed that the appropriate flags will be set for static runtime with Clang")
	endif()
	# set hints for FindBoost and FindOpenSSL built-in modules
	set(Boost_USE_MULTITHREADED ON)
	set(Boost_USE_STATIC_RUNTIME ON)
	set(OPENSSL_MSVC_STATIC_RT ON)
endif()

if (NOT BUILD_SHARED_LIBS)
	# set hints for FindBoost and FindOpenSSL built-in modules
	set(Boost_USE_STATIC_LIBS ON)
	set(OPENSSL_USE_STATIC_LIBS ON)
endif()

find_package(Threads REQUIRED)
list(APPEND package_config_dependencies "Threads REQUIRED")

# Boost header-only libraries have no designated component
if (deprecated-functions)
	find_package(Boost 1.58 REQUIRED COMPONENTS system date_time)
	list(APPEND package_config_dependencies "Boost REQUIRED COMPONENTS system date_time")
else()
	find_package(Boost 1.58 REQUIRED COMPONENTS system)
	list(APPEND package_config_dependencies "Boost REQUIRED COMPONENTS system")
endif()

if (iconv)
	find_package(Iconv REQUIRED)
	list(APPEND package_config_dependencies "Iconv REQUIRED")
endif()

if (openssl)
	find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto SSL)
	list(APPEND package_config_dependencies "OpenSSL REQUIRED COMPONENTS Crypto SSL")
endif()

if (libgcrypt)
	find_package(LibGcrypt REQUIRED)
	list(APPEND package_config_dependencies "LibGcrypt REQUIRED")
endif()

# -- Main target configuration --
# create interface-only target libraries with common compile options/definitions to link to
libtorrent_common_config()

# the TYPE will depend on the value of BUILD_SHARED_LIBS
add_library(torrent-rasterbar)

set_target_properties(torrent-rasterbar
	PROPERTIES
		CXX_VISIBILITY_PRESET "hidden"
		VISIBILITY_INLINES_HIDDEN "true"
		VERSION "${libtorrent_VERSION}"
		SOVERSION "10"
)

set(libtorrent_include_files
	add_torrent_params.hpp
	address.hpp
	alert.hpp
	alert_manager.hpp
	alert_types.hpp
	announce_entry.hpp
	assert.hpp
	bandwidth_limit.hpp
	bandwidth_manager.hpp
	bandwidth_queue_entry.hpp
	bandwidth_socket.hpp
	bdecode.hpp
	bencode.hpp
	bitfield.hpp
	block_cache.hpp
	bloom_filter.hpp
	broadcast_socket.hpp
	bt_peer_connection.hpp
	buffer.hpp
	chained_buffer.hpp
	choker.hpp
	close_reason.hpp
	config.hpp
	copy_ptr.hpp
	crc32c.hpp
	create_torrent.hpp
	deadline_timer.hpp
	debug.hpp
	disk_buffer_holder.hpp
	disk_buffer_pool.hpp
	disk_interface.hpp
	disk_io_job.hpp
	disk_io_thread.hpp
	disk_io_thread_pool.hpp
	disk_job_pool.hpp
	disk_observer.hpp
	download_priority.hpp
	ed25519.hpp
	entry.hpp
	enum_net.hpp
	error.hpp
	error_code.hpp
	extensions.hpp
	file.hpp
	file_pool.hpp
	file_storage.hpp
	fingerprint.hpp
	flags.hpp
	fwd.hpp
	gzip.hpp
	hasher.hpp
	heterogeneous_queue.hpp
	hex.hpp
	http_connection.hpp
	http_parser.hpp
	http_seed_connection.hpp
	http_stream.hpp
	http_tracker_connection.hpp
	i2p_stream.hpp
	identify_client.hpp
	index_range.hpp
	invariant_check.hpp
	io.hpp
	io_service.hpp
	io_service_fwd.hpp
	ip_filter.hpp
	ip_voter.hpp
	lazy_entry.hpp
	link.hpp
	linked_list.hpp
	lsd.hpp
	magnet_uri.hpp
	natpmp.hpp
	netlink.hpp
	operations.hpp
	optional.hpp
	packet_buffer.hpp
	packet_pool.hpp
	parse_url.hpp
	part_file.hpp
	peer.hpp
	peer_class.hpp
	peer_class_set.hpp
	peer_class_type_filter.hpp
	peer_connection.hpp
	peer_connection_handle.hpp
	peer_connection_interface.hpp
	peer_id.hpp
	peer_info.hpp
	peer_list.hpp
	peer_request.hpp
	performance_counters.hpp
	pex_flags.hpp
	piece_block.hpp
	piece_block_progress.hpp
	piece_picker.hpp
	platform_util.hpp
	portmap.hpp
	proxy_base.hpp
	puff.hpp
	random.hpp
	read_resume_data.hpp
	receive_buffer.hpp
	request_blocks.hpp
	resolve_links.hpp
	resolver.hpp
	resolver_interface.hpp
	session.hpp
	session_handle.hpp
	session_settings.hpp
	session_stats.hpp
	session_status.hpp
	session_types.hpp
	settings_pack.hpp
	sha1.hpp
	sha1_hash.hpp
	sha512.hpp
	sliding_average.hpp
	socket.hpp
	socket_io.hpp
	socks5_stream.hpp
	span.hpp
	ssl_stream.hpp
	stack_allocator.hpp
	stat.hpp
	stat_cache.hpp
	storage.hpp
	storage_defs.hpp
	string_util.hpp
	string_view.hpp
	tailqueue.hpp
	time.hpp
	timestamp_history.hpp
	torrent.hpp
	torrent_flags.hpp
	torrent_handle.hpp
	torrent_info.hpp
	torrent_peer.hpp
	torrent_peer_allocator.hpp
	torrent_status.hpp
	tracker_manager.hpp
	udp_socket.hpp
	udp_tracker_connection.hpp
	union_endpoint.hpp
	units.hpp
	upnp.hpp
	utf8.hpp
	utp_socket_manager.hpp
	utp_stream.hpp
	vector_utils.hpp
	version.hpp
	web_connection_base.hpp
	web_peer_connection.hpp
	write_resume_data.hpp
	xml_parse.hpp
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
	aligned_storage.hpp
	aligned_union.hpp
	alloca.hpp
	allocating_handler.hpp
	array.hpp
	bind_to_device.hpp
	block_cache_reference.hpp
	byteswap.hpp
	cppint_import_export.hpp
	cpuid.hpp
	deferred_handler.hpp
	deprecated.hpp
	deque.hpp
	dev_random.hpp
	disable_warnings_pop.hpp
	disable_warnings_push.hpp
	disk_job_fence.hpp
	escape_string.hpp
	export.hpp
	ffs.hpp
	file_progress.hpp
	has_block.hpp
	instantiate_connection.hpp
	io.hpp
	ip_notifier.hpp
	keepalive.hpp
	listen_socket_handle.hpp
	lsd.hpp
	merkle.hpp
	noexcept_movable.hpp
	numeric_cast.hpp
	openssl.hpp
	path.hpp
	pool.hpp
	portmap.hpp
	proxy_settings.hpp
	range.hpp
	route.h
	scope_end.hpp
	session_call.hpp
	session_impl.hpp
	session_interface.hpp
	session_settings.hpp
	session_udp_sockets.hpp
	set_socket_buffer.hpp
	socket_type.hpp
	storage_piece_set.hpp
	storage_utils.hpp
	string_ptr.hpp
	suggest_piece.hpp
	throw.hpp
	time.hpp
	torrent_impl.hpp
	unique_ptr.hpp
	vector.hpp
	win_crypto_provider.hpp
	win_util.hpp
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
	block_cache.cpp
	bloom_filter.cpp
	broadcast_socket.cpp
	bt_peer_connection.cpp
	chained_buffer.cpp
	choker.cpp
	close_reason.cpp
	cpuid.cpp
	crc32c.cpp
	create_torrent.cpp
	disk_buffer_holder.cpp
	disk_buffer_pool.cpp
	disk_io_job.cpp
	disk_io_thread.cpp
	disk_io_thread_pool.cpp
	disk_job_fence.cpp
	disk_job_pool.cpp
	entry.cpp
	enum_net.cpp
	error_code.cpp
	escape_string.cpp
	ffs.cpp
	file.cpp
	file_pool.cpp
	file_progress.cpp
	file_storage.cpp
	fingerprint.cpp
	generate_peer_id.cpp
	gzip.cpp
	hasher.cpp
	hex.cpp
	http_connection.cpp
	http_parser.cpp
	http_seed_connection.cpp
	http_stream.cpp
	http_tracker_connection.cpp
	i2p_stream.cpp
	identify_client.cpp
	instantiate_connection.cpp
	ip_filter.cpp
	ip_notifier.cpp
	ip_voter.cpp
	lazy_bdecode.cpp
	listen_socket_handle.cpp
	lsd.cpp
	magnet_uri.cpp
	merkle.cpp
	natpmp.cpp
	openssl.cpp
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
	proxy_base.cpp
	proxy_settings.cpp
	puff.cpp
	random.cpp
	read_resume_data.cpp
	receive_buffer.cpp
	request_blocks.cpp
	resolve_links.cpp
	resolver.cpp
	session.cpp
	session_call.cpp
	session_handle.cpp
	session_impl.cpp
	session_settings.cpp
	session_stats.cpp
	settings_pack.cpp
	sha1.cpp
	sha1_hash.cpp
	socket_io.cpp
	socket_type.cpp
	socks5_stream.cpp
	stack_allocator.cpp
	stat.cpp
	stat_cache.cpp
	storage.cpp
	storage_piece_set.cpp
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
	udp_socket.cpp
	udp_tracker_connection.cpp
	upnp.cpp
	utf8.cpp
	utp_socket_manager.cpp
	utp_stream.cpp
	version.cpp
	web_connection_base.cpp
	web_peer_connection.cpp
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
)

list(TRANSFORM sources PREPEND "src/")
list(TRANSFORM kademlia_sources PREPEND "src/kademlia/")
list(TRANSFORM ed25519_sources PREPEND "ed25519/src/")
list(TRANSFORM libtorrent_include_files PREPEND "include/libtorrent/")
list(TRANSFORM libtorrent_extensions_include_files PREPEND "include/libtorrent/extensions/")
list(TRANSFORM libtorrent_aux_include_files PREPEND "include/libtorrent/aux_/")
list(TRANSFORM libtorrent_kademlia_include_files PREPEND "include/libtorrent/kademlia/")

target_sources(torrent-rasterbar
	PRIVATE
		${sources}
		${libtorrent_include_files}
		${libtorrent_extensions_include_files}
		${libtorrent_aux_include_files}
		$<$<BOOL:${encryption}>:include/libtorrent/pe_crypto.hpp src/pe_crypto.cpp>
		$<$<BOOL:${dht}>:
			${libtorrent_kademlia_include_files}
			include/libtorrent/hasher512.hpp
			${kademlia_sources}
			${ed25519_sources}
			src/hasher512.cpp
			src/sha512.cpp>
)

# C++ 11 support is required
target_compile_features(torrent-rasterbar
	PUBLIC
		cxx_std_11
		cxx_alias_templates
		cxx_alignas
		cxx_alignof
		cxx_attributes
		cxx_auto_type
		cxx_constexpr
		cxx_decltype
		cxx_default_function_template_args
		cxx_defaulted_functions
		cxx_defaulted_move_initializers
		cxx_delegating_constructors
		cxx_deleted_functions
		cxx_enum_forward_declarations
		cxx_explicit_conversions
		cxx_extended_friend_declarations
		cxx_extern_templates
		cxx_final
		cxx_generalized_initializers
		cxx_inheriting_constructors
		cxx_inline_namespaces
		cxx_lambdas
		cxx_local_type_template_args
		cxx_noexcept
		cxx_nonstatic_member_init
		cxx_nullptr
		cxx_override
		cxx_range_for
		cxx_raw_string_literals
		cxx_reference_qualified_functions
		cxx_right_angle_brackets
		cxx_rvalue_references
		cxx_sizeof_member
		cxx_static_assert
		cxx_strong_enums
		cxx_trailing_return_types
		cxx_unrestricted_unions
		cxx_user_literals
		cxx_variadic_templates
)

target_link_libraries(torrent-rasterbar
	PUBLIC
		# -- Boost --
		Boost::headers Boost::system
		$<$<BOOL:${deprecated-functions}>:Boost::date_time>
		$<$<BOOL:${MSVC}>:Boost::disable_autolinking> # Conveninece interface target to add `-DBOOST_ALL_NO_LIB`
		# -- OpenSSL --
		$<$<BOOL:${openssl}>:OpenSSL::SSL>
		$<$<AND:$<BOOL:${openssl}>,$<NOT:$<BOOL:${libgcrypt}>>>:OpenSSL::Crypto>
		# -- Threads --
		Threads::Threads
		# -- Windows (MSYS2/MinGW only) --
		"$<$<OR:$<BOOL:${MSYS}>,$<BOOL:${MINGW}>>:mswsock;ws2_32>"
	PRIVATE
		libtorrent_common_cfg
		# -- Iconv --
		$<$<BOOL:${iconv}>:Iconv::Iconv>
		# -- LibGcrypt --
		$<$<BOOL:${libgcrypt}>:LibGcrypt::LibGcrypt>
		# -- Windows --
		"$<$<BOOL:${WIN32}>:crypt32;iphlpapi>"
		# -- Android --
		"$<$<BOOL:${ANDROID}>:${CMAKE_DL_LIBS}>"
		# -- Apple --
		"$<$<BOOL:${APPLE}>:-framework CoreFoundation;-framework SystemConfiguration>" # for ip_notifier
		# TODO: the following workaround is not needed anymore in CMake >= 3.21.0
		"$<$<AND:$<BOOL:${WIN32}>,$<BOOL:${OPENSSL_USE_STATIC_LIBS}>>:crypt32;ws2_32>"
)

# explicitly link against libatomic if needed
libtorrent_ensure_link_libatomic()

target_compile_definitions(torrent-rasterbar
	INTERFACE
		$<$<BOOL:${BUILD_SHARED_LIBS}>:TORRENT_LINKING_SHARED>
	PUBLIC
		# -- features ---
		$<$<BOOL:${iconv}>:TORRENT_USE_ICONV>
		$<$<BOOL:${libgcrypt}>:TORRENT_USE_LIBGCRYPT>
		$<$<BOOL:${openssl}>:TORRENT_USE_OPENSSL>
		$<$<AND:$<BOOL:${openssl}>,$<NOT:$<BOOL:${libgcrypt}>>>:TORRENT_USE_LIBCRYPTO>
		$<$<NOT:$<BOOL:${dht}>>:TORRENT_DISABLE_DHT>
		$<$<NOT:$<BOOL:${encryption}>>:TORRENT_DISABLE_ENCRYPTION>
		$<$<NOT:$<BOOL:${deprecated-functions}>>:TORRENT_NO_DEPRECATE>
		$<$<NOT:$<BOOL:${extensions}>>:TORRENT_DISABLE_EXTENSIONS>
		$<$<NOT:$<BOOL:${i2p}>>:TORRENT_USE_I2P=0>
		$<$<NOT:$<BOOL:${logging}>>:TORRENT_DISABLE_LOGGING>
		$<$<NOT:$<BOOL:${mutable-torrents}>>:TORRENT_DISABLE_MUTABLE_TORRENTS>
		$<$<NOT:$<BOOL:${streaming}>>:TORRENT_DISABLE_STREAMING>
		# the following are gated behind the developer-options feature:
		$<$<BOOL:${asio-debugging}>:TORRENT_ASIO_DEBUGGING>
		$<$<BOOL:${picker-debugging}>:TORRENT_DEBUG_REFCOUNTS>
		$<$<AND:$<BOOL:${debug-iterators}>,$<CXX_COMPILER_ID:GNU>>:_GLIBCXX_DEBUG _GLIBCXX_DEBUG_PEDANTIC>
		$<$<AND:$<BOOL:${debug-iterators}>,$<BOOL:${MSVC}>>:_ITERATOR_DEBUG_LEVEL=2>
		$<$<BOOL:${utp-log}>:TORRENT_UTP_LOG_ENABLE>
		$<$<BOOL:${simulate-slow-read}>:TORRENT_SIMULATE_SLOW_READ>
		$<$<BOOL:${profile-calls}>:TORRENT_PROFILE_CALLS=1>
		$<$<BOOL:$<STREQUAL:$<LOWER_CASE:${asserts}>,production>>:TORRENT_PRODUCTION_ASSERTS=1>
		$<$<BOOL:$<STREQUAL:$<LOWER_CASE:${asserts}>,system>>:TORRENT_USE_SYSTEM_ASSERTS=1>
		$<$<BOOL:$<STREQUAL:$<LOWER_CASE:${invariant-checks}>,full>>:TORRENT_EXPENSIVE_INVARIANT_CHECKS>
		# -- Boost --
		BOOST_ASIO_ENABLE_CANCELIO
		# -- Windows/MSVC --
		$<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN> # prevent winsock1 to be included
		$<$<AND:$<NOT:$<BOOL:${exceptions}>>,$<BOOL:${MSVC}>>:_HAS_EXCEPTIONS=0> # exceptions
		$<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS> # disable warnings about potentially unsafe string functions
	PRIVATE
		$<$<BOOL:${BUILD_SHARED_LIBS}>:TORRENT_BUILDING_SHARED>
		$<$<NOT:$<BOOL:${deprecated-functions}>>:BOOST_DATE_TIME_NO_LIB>
		TORRENT_BUILDING_LIBRARY
		_FILE_OFFSET_BITS=64
		BOOST_EXCEPTION_DISABLE
)

# These options are also gated behind the developer-options feature
if (asserts MATCHES "on|production|system")
	target_compile_definitions(torrent-rasterbar PUBLIC $<$<CONFIG:Debug>:TORRENT_USE_ASSERTS=1>)
endif()
if (invariant-checks MATCHES "on|full")
	target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_USE_INVARIANT_CHECKS=1)
endif()

# best-effort workaround for when building with C++11
libtorrent_cxx11_abi_issue_workaround()

target_compile_options(torrent-rasterbar
	PUBLIC
		# exceptions
		$<$<BOOL:${exceptions}>:$<IF:$<BOOL:${MSVC}>,/EHsc,-fexceptions>>
		$<$<AND:$<NOT:$<BOOL:${exceptions}>>,$<NOT:$<BOOL:${MSVC}>>>:-fno-exceptions>
	PRIVATE
		$<$<BOOL:${MSVC}>:/bigobj> # increase the number of sections for obj files
		$<$<AND:$<BOOL:${static_runtime}>,$<CXX_COMPILER_ID:GNU>>:-static-libgcc -static-libstdc++>
)

target_include_directories(torrent-rasterbar
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
		"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_options(torrent-rasterbar
	PRIVATE
		# some optimization flags for non-Debug builds
		$<$<AND:$<BOOL:${MSVC}>,$<BOOL:${BUILD_SHARED_LIBS}>,$<NOT:$<CONFIG:Debug>>>:LINKER:/OPT:ICF=5,/OPT:REF>
)

# target_link_options() cannot be used  to add options for static library targets, since they do not use a linker.
# To add archiver or MSVC librarian flags, we must manipulate the STATIC_LIBRARY_OPTIONS target property.
if (MSVC AND (NOT BUILD_SHARED_LIBS))
	get_target_property(_PREV_STATIC_LIBRARY_OPTIONS torrent-rasterbar STATIC_LIBRARY_OPTIONS)
	# the actual value might be "_PREV_STATIC_LIBRARY_OPTIONS-NOTFOUND" (which is flase-y) set it to the empty string
	if (NOT _PREV_STATIC_LIBRARY_OPTIONS)
		set(_PREV_STATIC_LIBRARY_OPTIONS "")
	endif()
	set_target_properties(torrent-rasterbar PROPERTIES STATIC_LIBRARY_OPTIONS "${_PREV_STATIC_LIBRARY_OPTIONS};$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF=5;/OPT:REF>")
endif()

# There is little to none support for using pkg-config with MSVC and most users won't bother with it.
# However, msys is a linux-like platform on Windows that do support/prefer using pkg-config.
if (NOT MSVC)
	generate_and_install_pkg_config_file(torrent-rasterbar libtorrent-rasterbar)
endif()

add_subdirectory(bindings)

install(TARGETS torrent-rasterbar EXPORT LibtorrentRasterbarTargets
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(DIRECTORY include/libtorrent DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h*")

# === generate a CMake Config File ===
include(CMakePackageConfigHelpers)
set(ConfigPackageLocation "${CMAKE_INSTALL_LIBDIR}/cmake/LibtorrentRasterbar")
foreach(_pkg IN LISTS package_config_dependencies)
	string(APPEND _find_dependency_calls "find_dependency(${_pkg})\n")
endforeach()

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfigVersion.cmake"
	VERSION "${libtorrent_VERSION}"
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
		"${ConfigPackageLocation}"
)
install(
	FILES
		"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/LibtorrentRasterbar/LibtorrentRasterbarConfigVersion.cmake"
	DESTINATION
		"${ConfigPackageLocation}"
)

install(
	FILES
		"${CMAKE_CURRENT_SOURCE_DIR}/examples/cmake/FindLibtorrentRasterbar.cmake"
	DESTINATION
		"${CMAKE_INSTALL_DATADIR}/cmake/Modules"
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
