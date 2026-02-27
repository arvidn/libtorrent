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

# TODO remove
set(single_file_examples)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
	add_compile_options(-Wno-implicit-int-float-conversion)
endif()

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp")
    target_link_libraries(${example} PRIVATE torrent-rasterbar)
endforeach(example)

# TODO remove
if(false)
add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(client_test PRIVATE torrent-rasterbar)
endif()

# TODO refactor with client_test
add_executable(exact_source_client
    exact_source_client.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(exact_source_client PRIVATE torrent-rasterbar)
