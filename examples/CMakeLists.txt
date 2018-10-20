project(libtorrent-examples)
cmake_minimum_required(VERSION 3.10)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

if (TARGET torrent-rasterbar)
	add_library(LibtorrentRasterbar::torrent-rasterbar ALIAS torrent-rasterbar)
else()
	find_package(LibtorrentRasterbar REQUIRED)
endif()

set(single_file_examples
    simple_client
    stats_counters
    dump_torrent
    make_torrent
    connection_tester
    upnp_test)

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp")
    target_link_libraries(${example} LibtorrentRasterbar::torrent-rasterbar)
endforeach(example)

add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(client_test LibtorrentRasterbar::torrent-rasterbar)
