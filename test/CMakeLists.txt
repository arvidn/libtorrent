cmake_minimum_required(VERSION 2.6)

file(GLOB tests "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_natpmp.cpp") # doesn't build at time of writing
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_utils.cpp") # helper file, not a test

add_library(test_common	OBJECT main.cpp test.cpp setup_transfer.cpp dht_server.cpp udp_tracker.cpp
	peer_server.cpp web_seed_suite.cpp swarm_suite.cpp test_utils.cpp make_torrent.cpp settings.cpp
)
target_compile_definitions(test_common PRIVATE $<TARGET_PROPERTY:torrent-rasterbar,INTERFACE_COMPILE_DEFINITIONS>)

if(MSVC)
	set_property(TARGET test_common PROPERTY COMPILE_PDB_NAME "${CMAKE_CURRENT_BINARY_DIR}/test_common")
endif()

foreach(TARGET_SRC ${tests})
	get_filename_component(TARGET ${TARGET_SRC} NAME_WE)
	add_executable(${TARGET} ${TARGET_SRC} $<TARGET_OBJECTS:test_common>)
	target_link_libraries(${TARGET} torrent-rasterbar)
	add_test(${TARGET} ${TARGET})
endforeach()

if (deprecated-functions)
	add_executable(bdecode_benchmark bdecode_benchmark.cpp)
	target_link_libraries(bdecode_benchmark torrent-rasterbar)
endif()

file(GLOB GZIP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.gz")
file(COPY ${GZIP_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB PYTHON_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
file(COPY ${PYTHON_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB XML_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.xml")
file(COPY ${XML_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(COPY "utf8_test.txt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "mutable_test_torrents" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "test_torrents" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "ssl" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
