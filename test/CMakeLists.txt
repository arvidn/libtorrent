
file(GLOB tests "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_natpmp.cpp") # doesn't build at time of writing
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_utils.cpp") # helper file, not a test

add_library(test_common STATIC main.cpp test.cpp setup_transfer.cpp dht_server.cpp udp_tracker.cpp
	peer_server.cpp web_seed_suite.cpp swarm_suite.cpp test_utils.cpp make_torrent.cpp settings.cpp
)
target_link_libraries(test_common PUBLIC torrent-rasterbar)

foreach(TARGET_SRC ${tests})
	get_filename_component(TARGET ${TARGET_SRC} NAME_WE)
	add_executable(${TARGET} ${TARGET_SRC})
	target_link_libraries(${TARGET} test_common)
	add_test(${TARGET} ${TARGET})
endforeach()

add_executable(bdecode_benchmark bdecode_benchmark.cpp)
target_link_libraries(bdecode_benchmark torrent-rasterbar)

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
