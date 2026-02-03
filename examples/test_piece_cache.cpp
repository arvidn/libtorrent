// test_piece_cache.cpp
// Unit and integration tests for piece cache feature
// Use with Catch2 testing framework

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "../examples/piece_cache_manager.hpp"
#include "../examples/cache_config.hpp"
#include "../examples/cache_alerts.hpp"
#include "../examples/torrent_utils.hpp"
#include "../examples/file_utils.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace {

// Helper function to create a test torrent
std::shared_ptr<lt::torrent_info> create_test_torrent(
    const std::string& name = "test_file.txt",
    std::int64_t file_size = 1024 * 16, // 16KB
    int piece_size = 16 * 1024) // 16KB pieces
{
    lt::file_storage fs;
    fs.add_file(name, file_size);
    
    lt::create_torrent t(fs, piece_size);
    t.set_creator("piece_cache_test");
    t.add_tracker("http://test.tracker.com:8080/announce");
    
    // Generate torrent with dummy piece hashes
    std::vector<char> piece_data(piece_size, 'X');
    lt::sha1_hash dummy_hash;
    int num_pieces = t.num_pieces();
    
    for (int i = 0; i < num_pieces; ++i)
    {
        t.set_hash(lt::piece_index_t(i), dummy_hash);
    }
    
    std::vector<char> buf;
    lt::bencode(std::back_inserter(buf), t.generate());
    
    return std::make_shared<lt::torrent_info>(buf);
}

// Helper to create temporary directory
std::string create_temp_dir(const std::string& prefix = "test_cache_")
{
    std::string dir = prefix + std::to_string(std::time(nullptr)) + 
                      "_" + std::to_string(rand() % 10000);
    std::filesystem::create_directories(dir);
    return dir;
}

// Helper to clean up directory
void cleanup_dir(const std::string& dir)
{
    try {
        if (std::filesystem::exists(dir))
            std::filesystem::remove_all(dir);
    } catch (...) {}
}

} // anonymous namespace

// ============================================================================
// PieceCacheManager Tests
// ============================================================================

TEST_CASE("PieceCacheManager constructor", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    
    SECTION("Creates cache directory")
    {
        PieceCacheManager cache(cache_dir);
        REQUIRE(std::filesystem::exists(cache_dir));
        REQUIRE(std::filesystem::is_directory(cache_dir));
    }
    
    SECTION("Throws on invalid directory")
    {
        // Try to create cache in a file (should fail)
        std::string file_path = cache_dir + "/test.txt";
        std::ofstream(file_path) << "test";
        
        REQUIRE_THROWS(PieceCacheManager(file_path));
    }
    
    cleanup_dir(cache_dir);
}

TEST_CASE("PieceCacheManager initialize_torrent", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    PieceCacheManager cache(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    SECTION("Successfully initializes")
    {
        REQUIRE(cache.initialize_torrent(info_hash, ti));
        
        // Check that torrent directory was created
        std::string torrent_dir = cache.get_torrent_cache_dir(info_hash);
        REQUIRE(std::filesystem::exists(torrent_dir));
        
        // Check metadata file
        std::string metadata_file = torrent_dir + "/metadata.txt";
        REQUIRE(std::filesystem::exists(metadata_file));
    }
    
    SECTION("Metadata file contains correct information")
    {
        cache.initialize_torrent(info_hash, ti);
        std::string metadata_file = cache.get_torrent_cache_dir(info_hash) + "/metadata.txt";
        
        std::ifstream meta(metadata_file);
        std::string content;
        std::string line;
        while (std::getline(meta, line))
            content += line + "\n";
        
        REQUIRE(content.find("torrent_name=" + ti->name()) != std::string::npos);
        REQUIRE(content.find("piece_length=") != std::string::npos);
        REQUIRE(content.find("num_pieces=") != std::string::npos);
    }
    
    cleanup_dir(cache_dir);
}

TEST_CASE("PieceCacheManager has_piece", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    PieceCacheManager cache(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    cache.initialize_torrent(info_hash, ti);
    
    SECTION("Returns false for non-existent piece")
    {
        REQUIRE_FALSE(cache.has_piece(info_hash, lt::piece_index_t(0)));
    }
    
    SECTION("Returns true after piece is cached")
    {
        // Manually create a piece file
        std::string piece_path = cache.get_piece_path(info_hash, lt::piece_index_t(0));
        std::ofstream(piece_path) << "dummy data";
        
        REQUIRE(cache.has_piece(info_hash, lt::piece_index_t(0)));
    }
    
    cleanup_dir(cache_dir);
}

TEST_CASE("PieceCacheManager get_cached_pieces", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    PieceCacheManager cache(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    cache.initialize_torrent(info_hash, ti);
    
    SECTION("Returns empty vector initially")
    {
        auto pieces = cache.get_cached_pieces(info_hash);
        REQUIRE(pieces.empty());
    }
    
    SECTION("Returns cached pieces")
    {
        // Create some piece files
        for (int i : {0, 2, 5})
        {
            std::string piece_path = cache.get_piece_path(info_hash, lt::piece_index_t(i));
            std::ofstream(piece_path) << "dummy";
        }
        
        auto pieces = cache.get_cached_pieces(info_hash);
        REQUIRE(pieces.size() == 3);
        REQUIRE(std::find(pieces.begin(), pieces.end(), lt::piece_index_t(0)) != pieces.end());
        REQUIRE(std::find(pieces.begin(), pieces.end(), lt::piece_index_t(2)) != pieces.end());
        REQUIRE(std::find(pieces.begin(), pieces.end(), lt::piece_index_t(5)) != pieces.end());
    }
    
    cleanup_dir(cache_dir);
}

TEST_CASE("PieceCacheManager statistics", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    PieceCacheManager cache(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    cache.initialize_torrent(info_hash, ti);
    
    SECTION("Initial statistics are zero")
    {
        auto stats = cache.get_statistics();
        REQUIRE(stats.total_cached_pieces == 0);
        REQUIRE(stats.cache_hits == 0);
        REQUIRE(stats.cache_misses == 0);
    }
    
    // Note: Testing statistics updates requires actual cache operations
    // which involve hash verification, so we skip detailed stats testing
    
    cleanup_dir(cache_dir);
}

TEST_CASE("PieceCacheManager clear_torrent_cache", "[piece_cache][unit]")
{
    std::string cache_dir = create_temp_dir();
    PieceCacheManager cache(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    cache.initialize_torrent(info_hash, ti);
    
    // Create some cached pieces
    for (int i = 0; i < 3; ++i)
    {
        std::string piece_path = cache.get_piece_path(info_hash, lt::piece_index_t(i));
        std::ofstream(piece_path) << "dummy";
    }
    
    SECTION("Removes all cached data")
    {
        std::string torrent_dir = cache.get_torrent_cache_dir(info_hash);
        REQUIRE(std::filesystem::exists(torrent_dir));
        
        REQUIRE(cache.clear_torrent_cache(info_hash));
        REQUIRE_FALSE(std::filesystem::exists(torrent_dir));
    }
    
    cleanup_dir(cache_dir);
}

// ============================================================================
// File Utilities Tests
// ============================================================================

TEST_CASE("file_utils: path operations", "[file_utils][unit]")
{
    using namespace piece_cache;
    
    SECTION("is_absolute_path")
    {
#ifdef _WIN32
        REQUIRE(is_absolute_path("C:\\path\\to\\file"));
        REQUIRE(is_absolute_path("D:/path/to/file"));
        REQUIRE_FALSE(is_absolute_path("relative/path"));
#else
        REQUIRE(is_absolute_path("/absolute/path"));
        REQUIRE_FALSE(is_absolute_path("relative/path"));
        REQUIRE_FALSE(is_absolute_path("./relative"));
#endif
    }
    
    SECTION("path_append")
    {
        REQUIRE(path_append("dir", "file") == "dir/file" || 
                path_append("dir", "file") == "dir\\file");
        REQUIRE(path_append("", "file") == "file");
        REQUIRE(path_append("dir", "") == "dir");
    }
}

TEST_CASE("file_utils: load and save file", "[file_utils][unit]")
{
    using namespace piece_cache;
    
    std::string test_dir = create_temp_dir();
    std::string test_file = test_dir + "/test.dat";
    
    SECTION("Save and load roundtrip")
    {
        std::vector<char> data = {'t', 'e', 's', 't', ' ', 'd', 'a', 't', 'a'};
        
        REQUIRE(save_file(test_file, data));
        REQUIRE(std::filesystem::exists(test_file));
        
        std::vector<char> loaded;
        REQUIRE(load_file(test_file, loaded));
        REQUIRE(loaded == data);
    }
    
    SECTION("Load non-existent file fails")
    {
        std::vector<char> data;
        REQUIRE_FALSE(load_file(test_dir + "/nonexistent.dat", data));
    }
    
    cleanup_dir(test_dir);
}

TEST_CASE("file_utils: is_resume_file", "[file_utils][unit]")
{
    using namespace piece_cache;
    
    SECTION("Valid resume files")
    {
        REQUIRE(is_resume_file("0123456789abcdef0123456789abcdef01234567.resume"));
        REQUIRE(is_resume_file("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.resume"));
    }
    
    SECTION("Invalid resume files")
    {
        REQUIRE_FALSE(is_resume_file("too_short.resume"));
        REQUIRE_FALSE(is_resume_file("0123456789abcdef0123456789abcdef01234567.txt"));
        REQUIRE_FALSE(is_resume_file("not_hex_chars_here_zzzzzzzzzzzzzzzzzzz.resume"));
    }
}

// ============================================================================
// Cache Configuration Tests
// ============================================================================

TEST_CASE("CacheConfig default values", "[cache_config][unit]")
{
    piece_cache::CacheConfig config;
    
    REQUIRE(config.enable_cache == true);
    REQUIRE(config.cache_during_download == false);
    REQUIRE(config.disable_original_storage == false);
    REQUIRE(config.seed_from_cache == false);
    REQUIRE(config.cache_root == "./piece_cache");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("Integration: Session with piece cache", "[integration][!mayfail]")
{
    std::string cache_dir = create_temp_dir();
    std::string save_dir = create_temp_dir("test_save_");
    
    // Configure cache
    piece_cache::g_cache_config.enable_cache = true;
    piece_cache::g_cache_config.cache_root = cache_dir;
    piece_cache::cache_manager = std::make_unique<PieceCacheManager>(cache_dir);
    
    // Create session
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, 
        lt::alert_category::all);
    lt::session ses(pack);
    
    // Create and add torrent
    auto ti = create_test_torrent();
    
    lt::add_torrent_params atp;
    atp.ti = ti;
    atp.save_path = save_dir;
    atp.flags |= lt::torrent_flags::seed_mode; // Start in seed mode for testing
    
    lt::torrent_handle h = ses.add_torrent(atp);
    
    // Wait for torrent to be added and cache initialized
    bool cache_initialized = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() < deadline && !cache_initialized)
    {
        ses.wait_for_alert(lt::milliseconds(100));
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        
        for (auto* a : alerts)
        {
            piece_cache::handle_cache_alert(a);
            
            if (auto* ata = lt::alert_cast<lt::add_torrent_alert>(a))
            {
                if (!ata->error)
                {
                    lt::info_hash_t ih = ti->info_hashes();
                    cache_initialized = piece_cache::g_initialized_torrents.find(ih) != 
                                       piece_cache::g_initialized_torrents.end();
                }
            }
        }
    }
    
    REQUIRE(cache_initialized);
    
    // Cleanup
    ses.remove_torrent(h);
    piece_cache::cache_manager.reset();
    piece_cache::g_initialized_torrents.clear();
    cleanup_dir(cache_dir);
    cleanup_dir(save_dir);
}

TEST_CASE("Integration: Resume data creation", "[integration]")
{
    std::string cache_dir = create_temp_dir();
    piece_cache::cache_manager = std::make_unique<PieceCacheManager>(cache_dir);
    
    auto ti = create_test_torrent();
    lt::info_hash_t info_hash;
    info_hash.v1 = ti->info_hash();
    
    // Initialize cache and add some pieces
    piece_cache::cache_manager->initialize_torrent(info_hash, ti);
    piece_cache::g_initialized_torrents.insert(info_hash);
    
    // Manually create cached pieces
    for (int i = 0; i < 3; ++i)
    {
        std::string piece_path = piece_cache::cache_manager->get_piece_path(
            info_hash, lt::piece_index_t(i));
        std::ofstream(piece_path) << "dummy";
    }
    
    SECTION("Create resume data from cache")
    {
        auto resume_params = piece_cache::create_cache_resume_data(info_hash, ti);
        
        REQUIRE(resume_params.ti);
        REQUIRE(resume_params.have_pieces.size() > 0);
        REQUIRE(resume_params.flags & lt::torrent_flags::seed_mode);
        
        // Verify the correct pieces are marked
        REQUIRE(resume_params.have_pieces[0]);
        REQUIRE(resume_params.have_pieces[1]);
        REQUIRE(resume_params.have_pieces[2]);
    }
    
    // Cleanup
    piece_cache::cache_manager.reset();
    piece_cache::g_initialized_torrents.clear();
    cleanup_dir(cache_dir);
}
