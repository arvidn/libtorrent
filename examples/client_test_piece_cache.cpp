#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <deque>
#include <fstream>
#include <regex>
#include <algorithm>
#include <numeric>

#include "libtorrent/config.hpp"

#ifdef TORRENT_WINDOWS
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/load_torrent.hpp"

#include "torrent_view.hpp"
#include "session_view.hpp"
#include "print.hpp"

// Piece cache includes
#include "piece_cache_manager.hpp"
#include "cache_config.hpp"
#include "cache_alerts.hpp"
#include "torrent_utils.hpp"
#include "file_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#include <csignal>
#include <iostream>
#include <dirent.h>
#endif

// Forward declarations of client_test.cpp functions/utilities
// These are copy-pasted from client_test but should ideally be in a shared header
namespace {

using lt::total_milliseconds;
using lt::alert;
using lt::piece_index_t;
using lt::file_index_t;
using lt::torrent_handle;
using lt::add_torrent_params;
using lt::total_seconds;
using lt::torrent_flags_t;
using lt::seconds;
using lt::operator ""_sv;
using lt::address_v4;
using lt::address_v6;
using lt::make_address_v6;
using lt::make_address_v4;
using lt::make_address;
using lt::torrent_status;
using lt::operation_t;
using lt::errors;
using lt::tcp;
using lt::error_code

using std::chrono::duration_cast;
using std::stoi;

// Global settings (declared extern in torrent_utils.cpp)
lt::storage_mode_t allocation_mode = lt::storage_mode_sparse;
std::string save_path(".");
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
std::string monitor_dir;
int poll_interval = 5;
int max_connections_per_torrent = 50;
bool seed_mode = false;
bool stats_enabled = false;
bool exit_on_finish = false;
bool share_mode = false;
bool quit = false;

// Print settings
bool print_trackers = false;
bool print_peers = false;
bool print_peers_legend = false;
bool print_connecting_peers = false;
bool print_log = false;
bool print_downloads = false;
bool print_matrix = false;
bool print_file_progress = false;
bool print_piece_availability = false;
bool show_pad_files = false;
bool show_dht_status = false;
bool print_ip = true;
bool print_peaks = false;
bool print_local_ip = false;
bool print_timers = false;
bool print_block = false;
bool print_fails = false;
bool print_send_bufs = true;
bool print_disk_stats = false;

int num_outstanding_resume_data = 0;

#ifndef TORRENT_DISABLE_DHT
std::vector<lt::dht_lookup> dht_active_requests;
std::vector<lt::dht_routing_bucket> dht_routing_table;
#endif

std::string peer;
FILE* g_log_file = nullptr;

struct client_state_t
{
    torrent_view& view;
    session_view& ses_view;
    std::deque<std::string> events;
    std::vector<lt::peer_info> peers;
    std::vector<std::int64_t> file_progress;
    std::vector<lt::partial_piece_info> download_queue;
    std::vector<lt::block_info> download_queue_block_info;
    std::vector<int> piece_availability;
    std::vector<lt::announce_entry> trackers;

    void clear()
    {
        peers.clear();
        file_progress.clear();
        download_queue.clear();
        download_queue_block_info.clear();
        piece_availability.clear();
        trackers.clear();
    }
};

// Include helper functions from client_test.cpp
// (sleep_and_input, print functions, etc. - copied from original)
// For brevity, I'll include minimal versions

#ifdef _WIN32
bool sleep_and_input(int* c, lt::time_duration const sleep)
{
    for (int i = 0; i < 2; ++i)
    {
        if (_kbhit())
        {
            *c = _getch();
            return true;
        }
        std::this_thread::sleep_for(sleep / 2);
    }
    return false;
}
#else
struct set_keypress
{
    enum terminal_mode {
        echo = 1,
        canonical = 2
    };

    explicit set_keypress(std::uint8_t const mode = 0)
    {
        using ul = unsigned long;
        termios new_settings;
        tcgetattr(0, &stored_settings);
        new_settings = stored_settings;
        if (mode & echo) new_settings.c_lflag |= ECHO;
        else new_settings.c_lflag &= ul(~ECHO);
        if (mode & canonical) new_settings.c_lflag |= ICANON;
        else new_settings.c_lflag &= ul(~ICANON);
        new_settings.c_cc[VTIME] = 0;
        new_settings.c_cc[VMIN] = 1;
        tcsetattr(0,TCSANOW,&new_settings);
    }
    ~set_keypress() { tcsetattr(0, TCSANOW, &stored_settings); }
private:
    termios stored_settings;
};

bool sleep_and_input(int* c, lt::time_duration const sleep)
{
    lt::time_point const done = lt::clock_type::now() + sleep;
    int ret = 0;
retry:
    fd_set set;
    FD_ZERO(&set);
    FD_SET(0, &set);
    auto const delay = total_milliseconds(done - lt::clock_type::now());
    timeval tv = {int(delay / 1000), int((delay % 1000) * 1000) };
    ret = select(1, &set, nullptr, nullptr, &tv);
    if (ret > 0)
    {
        *c = getc(stdin);
        return true;
    }
    if (errno == EINTR)
    {
        if (lt::clock_type::now() < done)
            goto retry;
        return false;
    }
    if (ret < 0 && errno != 0 && errno != ETIMEDOUT)
    {
        std::fprintf(stderr, "select failed: %s\n", strerror(errno));
        std::this_thread::sleep_for(lt::milliseconds(500));
    }
    return false;
}
#endif

#ifndef _WIN32
void signal_handler(int)
{
    quit = true;
}
#endif

char const* timestamp()
{
    time_t t = std::time(nullptr);
#ifdef TORRENT_WINDOWS
    std::tm const* timeinfo = localtime(&t);
#else
    std::tm buf;
    std::tm const* timeinfo = localtime_r(&t, &buf);
#endif
    static char str[200];
    std::strftime(str, 200, "%b %d %X", timeinfo);
    return str;
}

void print_alert(lt::alert const* a, std::string& str)
{
    using namespace lt;

    if (a->category() & alert_category::error)
    {
        str += esc("31");
    }
    else if (a->category() & (alert_category::peer | alert_category::storage))
    {
        str += esc("33");
    }
    str += "[";
    str += timestamp();
    str += "] ";
    str += a->message();
    str += esc("0");

    static auto const first_ts = a->timestamp();

    if (g_log_file)
        std::fprintf(g_log_file, "[%" PRId64 "] %s\n"
            , std::int64_t(duration_cast<std::chrono::milliseconds>(a->timestamp() - first_ts).count())
            ,  a->message().c_str());
}

// Alert handler - integrates cache handling
bool handle_alert(client_state_t& client_state, lt::alert* a)
{
    using namespace lt;

    // First, handle cache-related alerts
    if (piece_cache::handle_cache_alert(a))
        return true;

    // Handle torrent finished alert with cache reading
    if (torrent_finished_alert* p = alert_cast<torrent_finished_alert>(a))
    {
        p->handle.set_max_connections(max_connections_per_torrent / 2);

        // Read all pieces when torrent finishes to cache them
        if (piece_cache::cache_manager)
        {
            auto handle = p->handle;
            if (handle.is_valid() && handle.status().has_metadata)
            {
                try {
                    lt::info_hash_t ih = handle.info_hashes();
                    if (piece_cache::g_initialized_torrents.find(ih) != piece_cache::g_initialized_torrents.end())
                    {
                        auto ti = handle.torrent_file();
                        if (ti)
                        {
                            std::cout << "Torrent finished, caching all " << ti->num_pieces() << " pieces..." << std::endl;
                            for (lt::piece_index_t i(0); i < ti->num_pieces(); ++i)
                            {
                                handle.read_piece(i);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error reading pieces for cache: " << e.what() << std::endl;
                }
            }
        }

        torrent_handle h = p->handle;
        h.save_resume_data(torrent_handle::save_info_dict | torrent_handle::if_download_progress);
        ++num_outstanding_resume_data;
        if (exit_on_finish) quit = true;
    }

    // Rest of alert handling from client_test.cpp
    // (session_stats, peer_info, file_progress, etc.)
    if (session_stats_alert* s = alert_cast<session_stats_alert>(a))
    {
        client_state.ses_view.update_counters(s->counters(), s->timestamp());
        return !stats_enabled;
    }

    if (auto* p = alert_cast<peer_info_alert>(a))
    {
        if (client_state.view.get_active_torrent().handle == p->handle)
            client_state.peers = std::move(p->peer_info);
        return true;
    }

    if (auto* p = alert_cast<file_progress_alert>(a))
    {
        if (client_state.view.get_active_torrent().handle == p->handle)
            client_state.file_progress = std::move(p->files);
        return true;
    }

    if (auto* p = alert_cast<piece_info_alert>(a))
    {
        if (client_state.view.get_active_torrent().handle == p->handle)
        {
            client_state.download_queue = std::move(p->piece_info);
            client_state.download_queue_block_info = std::move(p->block_data);
        }
        return true;
    }

    if (auto* p = alert_cast<piece_availability_alert>(a))
    {
        if (client_state.view.get_active_torrent().handle == p->handle)
            client_state.piece_availability = std::move(p->piece_availability);
        return true;
    }

    if (auto* p = alert_cast<tracker_list_alert>(a))
    {
        if (client_state.view.get_active_torrent().handle == p->handle)
            client_state.trackers = std::move(p->trackers);
        return true;
    }

#ifndef TORRENT_DISABLE_DHT
    if (dht_stats_alert* p = alert_cast<dht_stats_alert>(a))
    {
        dht_active_requests = p->active_requests;
        dht_routing_table = p->routing_table;
        return true;
    }
#endif

    if (metadata_received_alert* p = alert_cast<metadata_received_alert>(a))
    {
        torrent_handle h = p->handle;
        h.save_resume_data(torrent_handle::save_info_dict);
        ++num_outstanding_resume_data;
    }

    if (add_torrent_alert* p = alert_cast<add_torrent_alert>(a))
    {
        if (p->error)
        {
            std::fprintf(stderr, "failed to add torrent: %s %s\n"
                , p->params.ti ? p->params.ti->name().c_str() : p->params.name.c_str()
                , p->error.message().c_str());
        }
        else
        {
            torrent_handle h = p->handle;
            h.save_resume_data(torrent_handle::save_info_dict | torrent_handle::if_metadata_changed);
            ++num_outstanding_resume_data;

            if (!peer.empty())
            {
                auto port = peer.find_last_of(':');
                if (port != std::string::npos)
                {
                    peer[port++] = '\0';
                    char const* ip = peer.data();
                    int const peer_port = atoi(peer.data() + port);
                    error_code ec;
                    if (peer_port > 0)
                        h.connect_peer(tcp::endpoint(make_address(ip, ec), std::uint16_t(peer_port)));
                }
            }
        }
    }

    if (save_resume_data_alert* p = alert_cast<save_resume_data_alert>(a))
    {
        --num_outstanding_resume_data;
        auto const buf = write_resume_data_buf(p->params);
        piece_cache::save_file(piece_cache::resume_file(p->params.info_hashes), buf);
    }

    if (save_resume_data_failed_alert* p = alert_cast<save_resume_data_failed_alert>(a))
    {
        --num_outstanding_resume_data;
        return p->error == lt::errors::resume_data_not_modified;
    }

    if (torrent_paused_alert* p = alert_cast<torrent_paused_alert>(a))
    {
        if (!quit)
        {
            torrent_handle h = p->handle;
            h.save_resume_data(torrent_handle::save_info_dict);
            ++num_outstanding_resume_data;
        }
    }

    if (state_update_alert* p = alert_cast<state_update_alert>(a))
    {
        lt::torrent_handle const prev = client_state.view.get_active_handle();
        client_state.view.update_torrents(std::move(p->status));

        if (client_state.view.get_active_handle() != prev)
            client_state.clear();
        return true;
    }

    if (torrent_removed_alert* p = alert_cast<torrent_removed_alert>(a))
    {
        client_state.view.remove_torrent(std::move(p->handle));
    }

    if (alert_cast<peer_connect_alert>(a)) return true;

    if (peer_disconnected_alert* pd = alert_cast<peer_disconnected_alert>(a))
    {
        if (pd->op == operation_t::connect
            || pd->error == errors::timed_out_no_handshake)
            return true;
    }

    return false;
}

void pop_alerts(client_state_t& client_state, lt::session& ses)
{
    std::vector<lt::alert*> alerts;
    ses.pop_alerts(&alerts);
    for (auto a : alerts)
    {
        if (::handle_alert(client_state, a)) continue;

        std::string event_string;
        print_alert(a, event_string);
        client_state.events.push_back(event_string);
        if (client_state.events.size() >= 20) client_state.events.pop_front();
    }
}

void print_usage()
{
    std::fprintf(stderr, R"(usage: client_test_piece_cache [OPTIONS] [TORRENT|MAGNETURL]
OPTIONS:

CLIENT OPTIONS
  -h                    print this message
  -f <log file>         logs all events to the given file
  -s <path>             sets the save path for downloads
  -m <path>             sets the .torrent monitor directory
  -t <seconds>          sets the scan interval of the monitor dir
  -F <milliseconds>     sets the UI refresh rate
  -k                    enable high performance settings
  -G                    add torrents in seed-mode
  -e <loops>            exit after N main loop iterations
  -O                    print session stats counters
  -1                    exit on first torrent completing
  -C                    cache pieces during download
  -Z                    disable original content storage (fileless mode)
  -S                    seed from piece cache only (no files created)
  --cache_root=<path>   set custom cache directory

BITTORRENT OPTIONS
  -T <limit>            max connections per torrent
  -U <rate>             per-torrent upload rate
  -D <rate>             per-torrent download rate
  -Q                    enable share mode
  -r <IP:port>          connect to specified peer

NETWORK OPTIONS
  -x <file>             loads an emule IP-filter file
  -Y                    rate limit local peers

DISK OPTIONS
  -a <mode>             allocation mode [sparse|allocate]
  -0                    disable disk I/O
)");
}

void assign_setting(lt::settings_pack& settings, std::string const& key, char const* value)
{
    int const sett_name = lt::setting_by_name(key);
    if (sett_name < 0)
    {
        std::fprintf(stderr, "unknown setting: \"%s\"\n", key.c_str());
        std::exit(1);
    }

    using lt::settings_pack;

    switch (sett_name & settings_pack::type_mask)
    {
        case settings_pack::string_type_base:
            settings.set_str(sett_name, value);
            break;
        case settings_pack::bool_type_base:
            if (value == "1"_sv || value == "on"_sv || value == "true"_sv)
                settings.set_bool(sett_name, true);
            else if (value == "0"_sv || value == "off"_sv || value == "false"_sv)
                settings.set_bool(sett_name, false);
            else
            {
                std::fprintf(stderr, "invalid value for \"%s\"\n", key.c_str());
                std::exit(1);
            }
            break;
        case settings_pack::int_type_base:
            // Handle int settings (simplified from original)
            try {
                settings.set_int(sett_name, std::stoi(value));
            } catch (...) {
                std::fprintf(stderr, "invalid int value for \"%s\"\n", key.c_str());
                std::exit(1);
            }
            break;
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
#ifndef _WIN32
    set_keypress s_;
#endif

    if (argc == 1)
    {
        print_usage();
        return 0;
    }

    using lt::settings_pack;
    using lt::session_handle;

    torrent_view view;
    session_view ses_view;

    // Initialize piece cache
    if (piece_cache::g_cache_config.enable_cache || piece_cache::g_cache_config.disable_original_storage)
    {
        try
        {
            piece_cache::cache_manager = std::make_unique<PieceCacheManager>(piece_cache::g_cache_config.cache_root);
            piece_cache::cache_during_download = piece_cache::g_cache_config.cache_during_download;
            
            if (piece_cache::g_cache_config.disable_original_storage) {
                std::cout << "Original content storage disabled, using piece cache only" << std::endl;
            }
            std::cout << "Piece cache initialized at: " << piece_cache::g_cache_config.cache_root << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to initialize piece cache: " << e.what() << std::endl;
            piece_cache::g_cache_config.enable_cache = false;
        }
    }

    lt::session_params params;

    // Set session-wide disabled storage if -Z or -S flag is set
    if (piece_cache::g_cache_config.disable_original_storage) {
        params.disk_io_constructor = lt::disabled_disk_io_constructor;
    }

#ifndef TORRENT_DISABLE_DHT
    std::vector<char> in;
    if (piece_cache::load_file(".ses_state", in))
        params = read_session_params(in, session_handle::save_dht_state);
#endif

    auto& settings = params.settings;
    settings.set_str(settings_pack::user_agent, "client_test/" LIBTORRENT_VERSION);
    settings.set_int(settings_pack::alert_mask
        , lt::alert_category::error
        | lt::alert_category::peer
        | lt::alert_category::port_mapping
        | lt::alert_category::storage
        | lt::alert_category::tracker
        | lt::alert_category::connect
        | lt::alert_category::status
        | lt::alert_category::ip_block
        | lt::alert_category::performance_warning
        | lt::alert_category::dht
        | lt::alert_category::incoming_request
        | lt::alert_category::dht_operation
        | lt::alert_category::port_mapping_log
        | lt::alert_category::file_progress
        | lt::alert_category::piece_progress);

    lt::time_duration refresh_delay = lt::milliseconds(500);
    bool rate_limit_locals = false;

    client_state_t client_state{view, ses_view, {}, {}, {}, {}, {}, {}, {}};
    int loop_limit = -1;

    lt::time_point next_dir_scan = lt::clock_type::now();

    std::vector<lt::string_view> torrents;
    lt::ip_filter loaded_ip_filter;

    // Parse command line
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-')
        {
            torrents.push_back(argv[i]);
            continue;
        }

        // Handle cache_root setting
        if (std::string(argv[i]).substr(0, 13) == "--cache_root=")
        {
            piece_cache::g_cache_config.cache_root = std::string(argv[i] + 13);
            continue;
        }

        // Handle libtorrent settings
        if (argv[i][1] == '-' && strchr(argv[i], '=') != nullptr)
        {
            char const* equal = strchr(argv[i], '=');
            char const* start = argv[i]+2;
            std::string const key(start, std::size_t(equal - start));
            char const* value = equal + 1;
            assign_setting(settings, key, value);
            continue;
        }

        // Switches without arguments
        switch (argv[i][1])
        {
            case 'k': settings = lt::high_performance_seed(); continue;
            case 'G': seed_mode = true; continue;
            case 'O': stats_enabled = true; continue;
            case '1': exit_on_finish = true; continue;
            case 'C': piece_cache::g_cache_config.cache_during_download = true; continue;
            case 'Z': piece_cache::g_cache_config.disable_original_storage = true; continue;
            case 'S':
                piece_cache::g_cache_config.seed_from_cache = true;
                piece_cache::g_cache_config.disable_original_storage = true;
                continue;
            case 'Q': share_mode = true; continue;
            case 'Y': rate_limit_locals = true; continue;
            case '0': params.disk_io_constructor = lt::disabled_disk_io_constructor; continue;
            case 'h': print_usage(); return 0;
        }

        // Switches with arguments
        if (argc == i + 1) continue;
        char const* arg = argv[i+1];
        if (arg == nullptr) arg = "";

        switch (argv[i][1])
        {
            case 'f': g_log_file = std::fopen(arg, "w+"); break;
            case 's': save_path = piece_cache::make_absolute_path(arg); break;
            case 'U': torrent_upload_limit = atoi(arg) * 1000; break;
            case 'D': torrent_download_limit = atoi(arg) * 1000; break;
            case 'm': monitor_dir = piece_cache::make_absolute_path(arg); break;
            case 't': poll_interval = atoi(arg); break;
            case 'F': refresh_delay = lt::milliseconds(atoi(arg)); break;
            case 'a':
                allocation_mode = (arg == std::string("sparse"))
                    ? lt::storage_mode_sparse
                    : lt::storage_mode_allocate;
                break;
            case 'T': max_connections_per_torrent = atoi(arg); break;
            case 'r': peer = arg; break;
            case 'e': loop_limit = atoi(arg); break;
        }
        ++i;
    }

    // Create resume directory
    std::string resume_path = piece_cache::path_append(save_path, ".resume");
#ifdef TORRENT_WINDOWS
    _mkdir(resume_path.c_str());
#else
    mkdir(resume_path.c_str(), 0777);
#endif

    // Create cache resume directory if needed
    if (piece_cache::g_cache_config.disable_original_storage)
    {
        std::string cache_resume_path = piece_cache::path_append(piece_cache::g_cache_config.cache_root, ".resume");
#ifdef TORRENT_WINDOWS
        _mkdir(cache_resume_path.c_str());
#else
        mkdir(cache_resume_path.c_str(), 0777);
#endif
    }

    lt::session ses(std::move(params));

    if (rate_limit_locals)
    {
        lt::ip_filter pcf;
        pcf.add_rule(make_address_v4("0.0.0.0")
            , make_address_v4("255.255.255.255")
            , 1 << static_cast<std::uint32_t>(lt::session::global_peer_class_id));
        pcf.add_rule(make_address_v6("::")
            , make_address_v6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 1);
        ses.set_peer_class_filter(pcf);
    }

    ses.set_ip_filter(loaded_ip_filter);

    // Add torrents from command line
    for (auto const& i : torrents)
    {
        if (i.substr(0, 7) == "magnet:") piece_cache::add_magnet(ses, i);
        else piece_cache::add_torrent(ses, i.to_string());
    }

    // Load resume files
    std::thread resume_data_loader([&ses]
    {
        lt::error_code ec;
        std::string const resume_dir = piece_cache::g_cache_config.disable_original_storage ?
            piece_cache::path_append(piece_cache::g_cache_config.cache_root, ".resume") :
            piece_cache::path_append(save_path, ".resume");
        
        std::vector<std::string> ents = piece_cache::list_dir(resume_dir
            , [](lt::string_view p) { return p.size() > 7 && p.substr(p.size() - 7) == ".resume"; }, ec);
        
        if (ec)
        {
            std::fprintf(stderr, "failed to list resume directory \"%s\": (%s : %d) %s\n"
                , resume_dir.c_str(), ec.category().name(), ec.value(), ec.message().c_str());
        }
        else
        {
            for (auto const& e : ents)
            {
                if (!piece_cache::is_resume_file(e)) continue;
                std::string const file = piece_cache::path_append(resume_dir, e);

                std::vector<char> resume_data;
                if (!piece_cache::load_file(file, resume_data))
                    continue;
                
                add_torrent_params p = lt::read_resume_data(resume_data, ec);
                if (ec) continue;

                ses.async_add_torrent(std::move(p));
            }
        }
    });

    // Main loop
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
#endif

    while (!quit && loop_limit != 0)
    {
        if (loop_limit > 0) --loop_limit;

        ses.post_torrent_updates();
        ses.post_session_stats();
        ses.post_dht_stats();

        int terminal_width = 80;
        int terminal_height = 50;
        std::tie(terminal_width, terminal_height) = terminal_size();

        int const height = std::min(terminal_height / 2
            , std::max(5, view.num_visible_torrents() + 2));
        view.set_size(terminal_width, height);
        ses_view.set_pos(height);
        ses_view.set_width(terminal_width);

        int c = 0;
        if (sleep_and_input(&c, refresh_delay))
        {
            // Handle keyboard input (simplified)
            if (c == 'q')
            {
                quit = true;
                break;
            }
            
            if (c == 'm')
            {
                char url[4096];
                url[0] = '\0';
                puts("Enter magnet link:\n");
#ifndef _WIN32
                set_keypress echo_(set_keypress::echo | set_keypress::canonical);
#endif
                if (std::scanf("%4095s", url) == 1) piece_cache::add_magnet(ses, url);
            }
        }

        pop_alerts(client_state, ses);

        // Render UI (simplified - original has full UI code)
        std::printf("\r                                                                \r");
        std::fflush(stdout);

        lt::time_point const now = lt::clock_type::now();
        if (!monitor_dir.empty() && next_dir_scan < now)
        {
            piece_cache::scan_dir(monitor_dir, ses);
            next_dir_scan = now + seconds(poll_interval);
        }
    }

    resume_data_loader.join();

    quit = true;
    ses.pause();
    std::printf("saving resume data\n");

    std::vector<torrent_status> const temp = ses.get_torrent_status(
        [](torrent_status const& st)
        { return st.handle.is_valid() && st.has_metadata && st.need_save_resume; }, {});

    for (auto const& st : temp)
    {
        st.handle.save_resume_data(torrent_handle::save_info_dict);
        ++num_outstanding_resume_data;
    }

    while (num_outstanding_resume_data > 0)
    {
        alert const* a = ses.wait_for_alert(seconds(10));
        if (a == nullptr) continue;
        pop_alerts(client_state, ses);
    }

    if (g_log_file) std::fclose(g_log_file);

#ifndef TORRENT_DISABLE_DHT
    std::printf("\nsaving session state\n");
    {
        std::vector<char> out = write_session_params_buf(ses.session_state(lt::session::save_dht_state));
        piece_cache::save_file(".ses_state", out);
    }
#endif

    std::printf("closing session\n");

    return 0;
}
