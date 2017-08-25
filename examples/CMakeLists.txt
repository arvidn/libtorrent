project(libtorrent-examples)
cmake_minimum_required(VERSION 2.6)

set(single_file_examples
    simple_client
    stats_counters
    dump_torrent
    make_torrent
    connection_tester
    upnp_test)

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp")
    target_link_libraries(${example} torrent-rasterbar)
endforeach(example)

add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(client_test torrent-rasterbar)
