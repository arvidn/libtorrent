add_library(torrentc library.cpp)
target_include_directories(torrentc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(torrentc PRIVATE torrent-rasterbar)

install(TARGETS torrentc
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES libtorrent.h DESTINATION include/libtorrent)

if (build_examples)
	add_executable(simple_client_c simple_client.c)
	target_link_libraries(simple_client_c torrentc)
endif()

