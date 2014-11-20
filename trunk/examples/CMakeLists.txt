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

FILE(GLOB examples RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")
foreach(s ${examples})
    get_filename_component (sn ${s} NAME_WE)
    add_executable(${sn} ${s})
    target_link_libraries(${sn} ${LibtorrentRasterbar_LIBRARIES} ${Boost_LIBRARIES})
endforeach(s)
