#include "cache_alerts.hpp"

namespace piece_cache {

std::set<lt::info_hash_t> g_initialized_torrents;
std::unique_ptr<PieceCacheManager> cache_manager;
bool cache_during_download = false;

void initialize_torrent_cache(lt::torrent_handle const& handle)
{
    if (!cache_manager || !handle.is_valid()) return;
    
    if (!handle.status().has_metadata) return;
    
    try {
        lt::info_hash_t ih = handle.info_hashes();
        if (g_initialized_torrents.find(ih) == g_initialized_torrents.end())
        {
            cache_manager->initialize_torrent(ih, handle.torrent_file());
            g_initialized_torrents.insert(ih);
            std::cout << "Initialized cache for torrent: " << handle.status().name << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error initializing cache: " << e.what() << std::endl;
    }
}

bool handle_cache_alert(lt::alert* a)
{
    if (!cache_manager) return false;
    
    // Handle read_piece_alert - cache piece data
    if (a->type() == lt::read_piece_alert::alert_type)
    {
        auto* rp = lt::alert_cast<lt::read_piece_alert>(a);
        if (rp)
        {
            auto handle = rp->handle;
            if (handle.is_valid())
            {
                try {
                    lt::info_hash_t ih = handle.info_hashes();
                    if (g_initialized_torrents.find(ih) != g_initialized_torrents.end())
                    {
                        cache_manager->cache_piece_data(
                            ih,
                            rp->piece,
                            rp->buffer.get(),
                            rp->size
                        );
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error caching piece: " << e.what() << std::endl;
                }
            }
        }
        return false; // Let it also be logged
    }
    
    // Handle piece_finished_alert - cache during download if enabled
    if (a->type() == lt::piece_finished_alert::alert_type)
    {
        auto* pf = lt::alert_cast<lt::piece_finished_alert>(a);
        if (pf && cache_during_download)
        {
            auto handle = pf->handle;
            if (handle.is_valid())
            {
                try {
                    lt::info_hash_t ih = handle.info_hashes();
                    if (g_initialized_torrents.find(ih) != g_initialized_torrents.end())
                    {
                        std::cout << "Piece " << static_cast<int>(pf->piece_index) 
                                  << " finished, reading for cache..." << std::endl;
                        handle.read_piece(pf->piece_index);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error reading finished piece: " << e.what() << std::endl;
                }
            }
        }
        return false;
    }
    
    // Handle add_torrent_alert - initialize cache
    if (a->type() == lt::add_torrent_alert::alert_type)
    {
        auto* ata = lt::alert_cast<lt::add_torrent_alert>(a);
        if (ata && !ata->error)
        {
            initialize_torrent_cache(ata->handle);
        }
        return false;
    }
    
    // Handle metadata_received_alert - initialize cache for magnet links
    if (a->type() == lt::metadata_received_alert::alert_type)
    {
        auto* mra = lt::alert_cast<lt::metadata_received_alert>(a);
        if (mra)
        {
            initialize_torrent_cache(mra->handle);
        }
        return false;
    }
    
    return false;
}

} // namespace piece_cache
