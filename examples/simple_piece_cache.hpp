// simple_piece_cache.hpp
#pragma once

#include "libtorrent/session.hpp"
#include "piece_cache_manager.hpp"

class SimplePieceCache
{
public:
    SimplePieceCache(lt::session& ses, const std::string& cache_root);
    ~SimplePieceCache();
    
    void enable();
    void disable();
    bool is_enabled() const;
    
    // Intercept piece downloads and uploads
    void on_piece_downloaded(lt::torrent_handle const& h, lt::piece_index_t piece);
    bool try_serve_from_cache(lt::torrent_handle const& h, lt::piece_index_t piece);
    
private:
    lt::session& m_session;
    std::unique_ptr<PieceCacheManager> m_cache_manager;
    bool m_enabled;
    
    void setup_alert_handler();
};

