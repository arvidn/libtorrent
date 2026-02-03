#include "torrent_utils.hpp"
#include "cache_config.hpp"
#include "cache_alerts.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/read_resume_data.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string path_append(std::string const& lhs, std::string const& rhs)
{
    if (lhs.empty() || lhs == ".") return rhs;
    if (rhs.empty() || rhs == ".") return lhs;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
    bool need_sep = lhs[lhs.size()-1] != '\\' && lhs[lhs.size()-1] != '/';
#else
#define TORRENT_SEPARATOR "/"
    bool need_sep = lhs[lhs.size()-1] != '/';
#endif
    return lhs + (need_sep?TORRENT_SEPARATOR:"") + rhs;
}

std::string to_hex(lt::sha1_hash const& s)
{
    std::stringstream ret;
    ret << s;
    return ret.str();
}

bool load_file(std::string const& filename, std::vector<char>& v, int limit = 8000000)
{
    std::fstream f(filename, std::ios_base::in | std::ios_base::binary);
    f.seekg(0, std::ios_base::end);
    auto const s = f.tellg();
    if (s > limit || s < 0) return false;
    f.seekg(0, std::ios_base::beg);
    v.resize(static_cast<std::size_t>(s));
    if (s == std::fstream::pos_type(0)) return !f.fail();
    f.read(v.data(), int(v.size()));
    return !f.fail();
}

// External declarations for global settings
extern std::string save_path;
extern int max_connections_per_torrent;
extern int torrent_upload_limit;
extern int torrent_download_limit;
extern bool seed_mode;
extern bool share_mode;
extern lt::storage_mode_t allocation_mode;

} // anonymous namespace

namespace piece_cache {

void set_torrent_params(lt::add_torrent_params& p)
{
    p.max_connections = max_connections_per_torrent;
    p.max_uploads = -1;
    p.upload_limit = torrent_upload_limit;
    p.download_limit = torrent_download_limit;

    // Use disabled disk I/O if -Z or -S flag is set
    if (g_cache_config.disable_original_storage) {
        if (g_cache_config.seed_from_cache) {
            // When seeding from cache, use cache root as save path but with disabled storage
            p.save_path = g_cache_config.cache_root;
        } else {
            // Original -Z behavior: use dummy save path
            p.save_path = "/tmp/dummy_save_path";
        }
    } else {
        p.save_path = save_path;
    }

    if (seed_mode) p.flags |= lt::torrent_flags::seed_mode;
    if (share_mode) p.flags |= lt::torrent_flags::share_mode;
    p.storage_mode = allocation_mode;
}

std::string resume_file(lt::info_hash_t const& info_hash)
{
    // Use appropriate resume directory based on mode
    std::string const resume_dir = g_cache_config.disable_original_storage ?
        path_append(g_cache_config.cache_root, ".resume") :
        path_append(save_path, ".resume");
        
    return path_append(resume_dir, to_hex(info_hash.get_best()) + ".resume");
}

lt::add_torrent_params create_cache_resume_data(
    lt::info_hash_t const& info_hash,
    std::shared_ptr<const lt::torrent_info> ti)
{
    lt::add_torrent_params p;
    p.info_hashes = info_hash;
    p.ti = std::const_pointer_cast<lt::torrent_info>(ti);
    
    if (cache_manager && ti) {
        auto cached_pieces = cache_manager->get_cached_pieces(info_hash);
        lt::bitfield pieces_bitfield(ti->num_pieces());
        
        for (auto piece : cached_pieces) {
            if (static_cast<int>(piece) < pieces_bitfield.size()) {
                pieces_bitfield.set_bit(static_cast<int>(piece));
            }
        }
        
        p.have_pieces = pieces_bitfield;
        p.flags |= lt::torrent_flags::seed_mode;
    }
    
    return p;
}

bool add_torrent(lt::session& ses, std::string torrent) try
{
    static int counter = 0;
    std::printf("[%d] %s\n", counter++, torrent.c_str());

    lt::error_code ec;
    lt::add_torrent_params atp = lt::load_torrent_file(torrent);

    std::vector<char> resume_data;
    if (load_file(resume_file(atp.info_hashes), resume_data))
    {
        lt::add_torrent_params rd = lt::read_resume_data(resume_data, ec);
        if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
        else atp = rd;
    }
    else if (g_cache_config.seed_from_cache && cache_manager)
    {
        // For cache-only seeding, create resume data from cached pieces
        if (atp.ti) {
            atp = create_cache_resume_data(atp.info_hashes, atp.ti);
            std::printf("  created resume data from cache for %s\n", atp.ti->name().c_str());
        }
    }

    set_torrent_params(atp);

    atp.flags &= ~lt::torrent_flags::duplicate_is_error;
    ses.async_add_torrent(std::move(atp));
    return true;
}
catch (lt::system_error const& e)
{
    std::printf("failed to load torrent \"%s\": %s\n"
        , torrent.c_str(), e.code().message().c_str());
    return false;
}

void add_magnet(lt::session& ses, lt::string_view uri)
{
    lt::error_code ec;
    lt::add_torrent_params p = lt::parse_magnet_uri(uri.to_string(), ec);

    if (ec)
    {
        std::printf("invalid magnet link \"%s\": %s\n"
            , uri.to_string().c_str(), ec.message().c_str());
        return;
    }

    std::vector<char> resume_data;
    if (load_file(resume_file(p.info_hashes), resume_data))
    {
        p = lt::read_resume_data(resume_data, ec);
        if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
    }

    set_torrent_params(p);

    std::printf("adding magnet: %s\n", uri.to_string().c_str());
    ses.async_add_torrent(std::move(p));
}

} // namespace piece_cache
