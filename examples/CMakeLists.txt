cmake_minimum_required(VERSION 3.13)
project(libtorrent-examples)

set(single_file_examples
    simple_client
    custom_storage
    stats_counters
    dump_torrent
    make_torrent
    connection_tester
    upnp_test)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp")
    target_link_libraries(${example} PRIVATE torrent-rasterbar ${Boost_SYSTEM_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
endforeach(example)

add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp)
target_link_libraries(client_test PRIVATE torrent-rasterbar ${Boost_SYSTEM_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
