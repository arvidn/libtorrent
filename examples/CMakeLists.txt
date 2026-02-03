project(libtorrent-examples)

set(single_file_examples
    simple_client
    custom_storage
    stats_counters
    dump_torrent
    dump_bdecode
    make_torrent
    connection_tester
    upnp_test)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
	add_compile_options(-Wno-implicit-int-float-conversion)
endif()

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp")
    target_link_libraries(${example} PRIVATE torrent-rasterbar)
endforeach(example)

add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(client_test PRIVATE torrent-rasterbar)

# Piece cache example with modular structure
add_executable(client_test_piece_cache
    client_test_piece_cache.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp
    piece_cache_manager.cpp
    # New modular components
    cache_config.cpp
    cache_alerts.cpp
    torrent_utils.cpp
    file_utils.cpp)

target_link_libraries(client_test_piece_cache PRIVATE torrent-rasterbar)

# OpenSSL is required for SHA1 hashing in piece_cache_manager.cpp
find_package(OpenSSL)
if(OPENSSL_FOUND)
    target_link_libraries(client_test_piece_cache PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Set C++17 standard (required for std::filesystem in file_utils.cpp)
target_compile_features(client_test_piece_cache PRIVATE cxx_std_17)
