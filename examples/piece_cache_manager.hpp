#pragma once

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/error_code.hpp"
//#include "libtorrent/piece_index_t.hpp"
//#include "libtorrent/info_hash_t.hpp"
#include "libtorrent/info_hash.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <mutex>
#include <fstream>
#include <vector>

struct PieceCacheStatistics
{
    size_t total_cached_pieces = 0;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    size_t total_cache_size = 0;
    size_t cache_writes = 0;
    size_t cache_reads = 0;
};

class PieceCacheManager
{
public:
    explicit PieceCacheManager(const std::string& cache_root);
    ~PieceCacheManager() = default;

    bool initialize_torrent(const lt::info_hash_t& info_hash, 
                          std::shared_ptr<const lt::torrent_info> torrent_info);

    bool cache_piece_data(const lt::info_hash_t& info_hash,
                         lt::piece_index_t piece_index,
                         const char* piece_data,
                         size_t piece_size);

    bool has_piece(const lt::info_hash_t& info_hash, 
                  lt::piece_index_t piece_index) const;

    int read_piece(const lt::info_hash_t& info_hash,
                  lt::piece_index_t piece_index,
                  char* buffer,
                  size_t buffer_size);

    std::vector<lt::piece_index_t> get_cached_pieces(const lt::info_hash_t& info_hash) const;
    PieceCacheStatistics get_statistics() const;
    bool clear_torrent_cache(const lt::info_hash_t& info_hash);

    std::string get_piece_path(const lt::info_hash_t& info_hash, 
                             lt::piece_index_t piece_index) const;
    std::string get_torrent_cache_dir(const lt::info_hash_t& info_hash) const;

private:
    std::string m_cache_root;
    mutable std::mutex m_stats_mutex;
    mutable std::mutex m_cache_mutex;
    PieceCacheStatistics m_statistics;
    
    std::unordered_map<lt::info_hash_t, std::shared_ptr<const lt::torrent_info>> m_torrent_infos;

    std::string info_hash_to_string(const lt::info_hash_t& info_hash) const;
    bool ensure_directory(const std::string& path) const;
    void update_statistics(int hits_delta, int misses_delta, int size_delta);
};
