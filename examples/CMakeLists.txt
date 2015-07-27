project(libtorrent-examples)
cmake_minimum_required(VERSION 2.6)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

# Add extra include and library search directories so examples can optionally
# be built without a prior "make install" of libtorrent.
list(INSERT CMAKE_INCLUDE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/../include")
list(INSERT CMAKE_LIBRARY_PATH 0 "${CMAKE_CURRENT_BINARY_DIR}/..")

# Also use the generated pkg-config file prior to "make install".
# In an independent project, these lines would simply not exist.
set(PKG_CONFIG_CHANGED_PATH "${CMAKE_CURRENT_BINARY_DIR}/..;$ENV{PKG_CONFIG_PATH}")
if(UNIX)
    string(REPLACE ";" ":" PKG_CONFIG_CHANGED_PATH "${PKG_CONFIG_CHANGED_PATH}")
endif ()
set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_CHANGED_PATH}")

find_package(LibtorrentRasterbar REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

include_directories(${LibtorrentRasterbar_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
add_definitions(${LibtorrentRasterbar_DEFINITIONS})

set(asio_sources ${CMAKE_CURRENT_SOURCE_DIR}/../src/asio.cpp)
if(LibtorrentRasterbar_OPENSSL_ENABLED)
	list(APPEND asio_sources ${CMAKE_CURRENT_SOURCE_DIR}/../src/asio_ssl.cpp)
endif()

add_library(asio_src OBJECT ${asio_sources})
set(asio_src_objects $<TARGET_OBJECTS:asio_src>)
add_definitions(-DBOOST_ASIO_SEPARATE_COMPILATION)

set(single_file_examples
    simple_client
    stats_counters
    dump_torrent
    make_torrent
    connection_tester
    upnp_test)

foreach(example ${single_file_examples})
    add_executable(${example} "${example}.cpp" ${asio_src_objects})
    target_link_libraries(${example} ${LibtorrentRasterbar_LIBRARIES} ${Boost_LIBRARIES})
endforeach(example)

add_executable(client_test
    client_test.cpp
    print.cpp
    torrent_view.cpp
    session_view.cpp
    ${asio_src_objects})
target_link_libraries(client_test ${LibtorrentRasterbar_LIBRARIES} ${Boost_LIBRARIES})
