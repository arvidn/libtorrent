#pragma once

#include "libtorrent/alert_types.hpp"
#include "piece_cache_manager.hpp"
#include <set>
#include <iostream>

namespace piece_cache {

// Global state for initialized torrents
extern std::set<lt::info_hash_t> g_initialized_torrents;
extern std::unique_ptr<PieceCacheManager> cache_manager;
extern bool cache_during_download;

/**
 * Handle piece caching alerts
 * Returns true if alert was handled and should not be logged
 */
bool handle_cache_alert(lt::alert* a);

/**
 * Initialize cache for a torrent when metadata is available
 */
void initialize_torrent_cache(lt::torrent_handle const& handle);

} // namespace piece_cache
