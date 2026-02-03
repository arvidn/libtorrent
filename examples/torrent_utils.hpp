#pragma once

#include "libtorrent/session.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/info_hash.hpp"
#include <string>

namespace piece_cache {

/**
 * Set torrent parameters based on cache configuration
 */
void set_torrent_params(lt::add_torrent_params& p);

/**
 * Get resume file path for a given info hash
 */
std::string resume_file(lt::info_hash_t const& info_hash);

/**
 * Create resume data for cache-only seeding
 */
lt::add_torrent_params create_cache_resume_data(
    lt::info_hash_t const& info_hash,
    std::shared_ptr<const lt::torrent_info> ti
);

/**
 * Add torrent from file
 */
bool add_torrent(lt::session& ses, std::string torrent);

/**
 * Add magnet link
 */
void add_magnet(lt::session& ses, lt::string_view uri);

} // namespace piece_cache
