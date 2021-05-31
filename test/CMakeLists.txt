cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR) # Configurable policies: <= CMP0097

# Our tests contain printf formating checks therefore we disable GCC format string errors:
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	check_cxx_compiler_flag("-Wno-error=format-truncation" _WNO_ERROR_FORMAT_TRUNCATION)
	if (_WNO_ERROR_FORMAT_TRUNCATION)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=format-truncation")
	endif()
endif()

file(GLOB tests "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_natpmp.cpp") # doesn't build at time of writing
list(REMOVE_ITEM tests "${CMAKE_CURRENT_SOURCE_DIR}/test_utils.cpp") # helper file, not a test

add_library(test_common
	STATIC
	broadcast_socket.cpp
	dht_server.cpp
	main.cpp
	make_torrent.cpp
	peer_server.cpp
	settings.cpp
	setup_transfer.cpp
	swarm_suite.cpp
	test.cpp
	test_utils.cpp
	udp_tracker.cpp
	web_seed_suite.cpp
)
target_link_libraries(test_common PUBLIC torrent-rasterbar)
if (MSVC)
	target_compile_options(test_common PUBLIC
		/wd4127 # C4127: conditional expression is constant
		/wd4309 # C4309: 'conversion' : truncation of constant value
		/wd4310 # C4310: cast truncates constant value
	)
endif()

foreach(TARGET_SRC ${tests})
	get_filename_component(TARGET ${TARGET_SRC} NAME_WE)
	add_executable(${TARGET} ${TARGET_SRC})
	target_link_libraries(${TARGET} test_common)
	add_test(${TARGET} ${TARGET})
endforeach()

file(GLOB GZIP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.gz")
file(COPY ${GZIP_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB PYTHON_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
file(COPY ${PYTHON_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB XML_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/*.xml")
file(COPY ${XML_ASSETS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(COPY "utf8_test.txt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "utf8_latin1_test.txt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "mutable_test_torrents" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "ssl" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "test_torrents" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "utf8_test.txt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
