add_executable(dht dht_put.cpp)
target_link_libraries(dht PRIVATE torrent-rasterbar)

add_executable(dht_sample dht_sample.cpp)
target_link_libraries(dht_sample PRIVATE torrent-rasterbar)

add_executable(session_log_alerts session_log_alerts.cpp)
target_link_libraries(session_log_alerts PRIVATE torrent-rasterbar)

add_executable(gen_torture_torrent gen_torture_torrent.cpp)
target_link_libraries(gen_torture_torrent PRIVATE torrent-rasterbar)

add_executable(benchmark_load_torrent benchmark_load_torrent.cpp)
target_link_libraries(benchmark_load_torrent PRIVATE torrent-rasterbar)

# bencher uses do_not_optimize(), which relies on GNU inline asm and is
# not supported by MSVC.
if (NOT MSVC)
	add_executable(bencher bencher.cpp)
	target_link_libraries(bencher PRIVATE torrent-rasterbar)
endif()
