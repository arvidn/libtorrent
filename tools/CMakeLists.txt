add_executable(dht dht_put.cpp)
target_link_libraries(dht PRIVATE torrent-rasterbar)

add_executable(session_log_alerts session_log_alerts.cpp)
target_link_libraries(session_log_alerts PRIVATE torrent-rasterbar)
