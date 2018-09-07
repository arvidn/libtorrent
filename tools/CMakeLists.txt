add_executable(parse_access_log parse_access_log.cpp)
target_link_libraries(parse_access_log PRIVATE torrent-rasterbar)

add_executable(dht dht_put.cpp)
target_link_libraries(dht PRIVATE torrent-rasterbar)

add_executable(session_log_alerts session_log_alerts.cpp)
target_link_libraries(session_log_alerts torrent-rasterbar)
