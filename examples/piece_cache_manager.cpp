#include "piece_cache_manager.hpp"
#include "libtorrent/hex.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <openssl/sha.h>  // Add OpenSSL SHA1 header
//#include "libtorrent/hasher.hpp"  // Add this include

PieceCacheManager::PieceCacheManager(const std::string& cache_root)
    : m_cache_root(cache_root)
{
    if (!ensure_directory(m_cache_root))
    {
        throw std::runtime_error("Failed to create cache root directory: " + m_cache_root);
    }
}

bool PieceCacheManager::initialize_torrent(const lt::info_hash_t& info_hash,
                                         std::shared_ptr<const lt::torrent_info> torrent_info)
{
    std::cout << "DEBUG: Initializing torrent: " << torrent_info->name() << std::endl;
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    
    m_torrent_infos[info_hash] = torrent_info;
    
    std::string torrent_cache_dir = get_torrent_cache_dir(info_hash);
    
    if (!ensure_directory(torrent_cache_dir))
    {
        std::cerr << "Failed to create torrent cache directory: " << torrent_cache_dir << std::endl;
        return false;
    }
    
    // Create metadata file
    std::string metadata_file = torrent_cache_dir + "/metadata.txt";
    std::ofstream meta(metadata_file);
    if (meta.is_open())
    {
        meta << "torrent_name=" << torrent_info->name() << "\n";
        meta << "piece_length=" << torrent_info->piece_length() << "\n";
        meta << "num_pieces=" << torrent_info->num_pieces() << "\n";
        meta << "total_size=" << torrent_info->total_size() << "\n";
        meta << "info_hash=" << info_hash_to_string(info_hash) << "\n";
        meta.close();
    }
    
    std::cout << "Initialized cache for torrent: " << torrent_info->name() 
              << " at " << torrent_cache_dir << std::endl;
    
    return true;
}

bool PieceCacheManager::cache_piece_data(const lt::info_hash_t& info_hash,
                                       lt::piece_index_t piece_index,
                                       const char* piece_data,
                                       size_t piece_size)
{
    std::cout << "DEBUG: cache_piece_data called for piece " 
              << static_cast<int>(piece_index) << std::endl;
    
    if (!piece_data || piece_size == 0)
    {
        std::cout << "DEBUG: Invalid piece data or size" << std::endl;
        return false;
    }    
    
    std::string piece_path = get_piece_path(info_hash, piece_index);
    std::string torrent_dir = get_torrent_cache_dir(info_hash);
    
    if (!ensure_directory(torrent_dir))
        return false;
    
    // Verify piece hash before caching
    std::shared_ptr<const lt::torrent_info> torrent_info;
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        auto it = m_torrent_infos.find(info_hash);
        if (it == m_torrent_infos.end())
        {
            std::cerr << "Torrent info not found for hash verification" << std::endl;
            return false;
        }
        torrent_info = it->second;
    }
    
    // Use OpenSSL's SHA1 for hashing
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(piece_data), piece_size, digest);
    
    // Convert OpenSSL digest to libtorrent sha1_hash
    lt::sha1_hash calculated_hash(reinterpret_cast<const char*>(digest));
    lt::sha1_hash expected_hash = torrent_info->hash_for_piece(piece_index);

    if (calculated_hash != expected_hash)
    {
        std::cerr << "Piece " << static_cast<int>(piece_index) 
                  << " hash verification failed, not caching" << std::endl;
        std::cerr << "Expected: " << expected_hash << std::endl;
        std::cerr << "Calculated: " << calculated_hash << std::endl;
        return false;
    }
    else
    {
        std::cout << "DEBUG: Hash verification passed for piece " 
                  << static_cast<int>(piece_index) << std::endl;
    }
    
    // Write piece to cache
    std::ofstream piece_file(piece_path, std::ios::binary);
    if (!piece_file.is_open())
    {
        std::cerr << "Failed to open piece file for writing: " << piece_path << std::endl;
        return false;
    }
    
    piece_file.write(piece_data, piece_size);
    piece_file.close();
    
    if (piece_file.fail())
    {
        std::cerr << "Failed to write piece data to: " << piece_path << std::endl;
        return false;
    }
    
    // Update statistics
    update_statistics(0, 0, piece_size);
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_statistics.total_cached_pieces++;
        m_statistics.cache_writes++;
    }
    
    std::cout << "Cached piece " << static_cast<int>(piece_index) 
              << " (" << piece_size << " bytes)" << std::endl;
    
    return true;
}

bool PieceCacheManager::has_piece(const lt::info_hash_t& info_hash,
                                 lt::piece_index_t piece_index) const
{
    std::string piece_path = get_piece_path(info_hash, piece_index);
    return std::filesystem::exists(piece_path);
}

int PieceCacheManager::read_piece(const lt::info_hash_t& info_hash,
                                 lt::piece_index_t piece_index,
                                 char* buffer,
                                 size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return -1;
    
    std::string piece_path = get_piece_path(info_hash, piece_index);
    
    if (!std::filesystem::exists(piece_path))
    {
        update_statistics(0, 1, 0);
        return -1;
    }
    
    std::ifstream piece_file(piece_path, std::ios::binary);
    if (!piece_file.is_open())
    {
        std::cerr << "Failed to open cached piece file: " << piece_path << std::endl;
        update_statistics(0, 1, 0);
        return -1;
    }
    
    piece_file.seekg(0, std::ios::end);
    std::size_t file_size = piece_file.tellg();
    piece_file.seekg(0, std::ios::beg);
    
    if (file_size > buffer_size)
    {
        std::cerr << "Buffer too small for piece " << static_cast<int>(piece_index)
                  << ". Need " << file_size << ", have " << buffer_size << std::endl;
        return -1;
    }
    
    piece_file.read(buffer, file_size);
    
    if (piece_file.fail())
    {
        std::cerr << "Failed to read piece data from: " << piece_path << std::endl;
        update_statistics(0, 1, 0);
        return -1;
    }
    
    update_statistics(1, 0, 0);
    std::cout << "Read cached piece " << static_cast<int>(piece_index)
              << " (" << file_size << " bytes)" << std::endl;
    
    return static_cast<int>(file_size);
}

std::vector<lt::piece_index_t> PieceCacheManager::get_cached_pieces(const lt::info_hash_t& info_hash) const
{
    std::vector<lt::piece_index_t> cached_pieces;
    std::string torrent_dir = get_torrent_cache_dir(info_hash);
    
    if (!std::filesystem::exists(torrent_dir))
        return cached_pieces;
    
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(torrent_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (filename.substr(0, 6) == "piece_" && filename.size() >= 4 && filename.substr(filename.size() - 4) == ".dat")
                {
                    std::string piece_num_str = filename.substr(6, filename.length() - 10);
                    try
                    {
                        int piece_num = std::stoi(piece_num_str);
                        cached_pieces.push_back(lt::piece_index_t(piece_num));
                    }
                    catch (const std::exception&)
                    {
                        continue;
                    }
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Error scanning cache directory: " << e.what() << std::endl;
    }
    
    std::sort(cached_pieces.begin(), cached_pieces.end());
    return cached_pieces;
}

PieceCacheStatistics PieceCacheManager::get_statistics() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_statistics;
}

bool PieceCacheManager::clear_torrent_cache(const lt::info_hash_t& info_hash)
{
    std::string torrent_dir = get_torrent_cache_dir(info_hash);
    
    if (!std::filesystem::exists(torrent_dir))
        return true;
    
    try
    {
        std::uintmax_t removed_size = 0;
        std::uintmax_t removed_count = 0;
        
        for (const auto& entry : std::filesystem::directory_iterator(torrent_dir))
        {
            if (entry.is_regular_file())
            {
                removed_size += entry.file_size();
                removed_count++;
            }
        }
        
        std::filesystem::remove_all(torrent_dir);
        
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            m_statistics.total_cached_pieces -= removed_count;
            m_statistics.total_cache_size -= removed_size;
        }
        
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            m_torrent_infos.erase(info_hash);
        }
        
        std::cout << "Cleared cache for torrent " << info_hash_to_string(info_hash)
                  << " (" << removed_count << " pieces, " << removed_size << " bytes)" << std::endl;
        
        return true;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Error clearing torrent cache: " << e.what() << std::endl;
        return false;
    }
}

std::string PieceCacheManager::get_piece_path(const lt::info_hash_t& info_hash,
                                            lt::piece_index_t piece_index) const
{
    std::string torrent_dir = get_torrent_cache_dir(info_hash);
    std::ostringstream piece_filename;
    piece_filename << "piece_" << std::setfill('0') << std::setw(6) 
                   << static_cast<int>(piece_index) << ".dat";
    return torrent_dir + "/" + piece_filename.str();
}

std::string PieceCacheManager::get_torrent_cache_dir(const lt::info_hash_t& info_hash) const
{
    return m_cache_root + "/" + info_hash_to_string(info_hash);
}

std::string PieceCacheManager::info_hash_to_string(const lt::info_hash_t& info_hash) const
{
    if (info_hash.has_v2())
        return lt::aux::to_hex(info_hash.v2);
    return lt::aux::to_hex(info_hash.v1);
}

bool PieceCacheManager::ensure_directory(const std::string& path) const
{
    try
    {
        std::filesystem::create_directories(path);
        return true;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Failed to create directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

void PieceCacheManager::update_statistics(int hits_delta, int misses_delta, int size_delta)
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_statistics.cache_hits += hits_delta;
    m_statistics.cache_misses += misses_delta;
    m_statistics.total_cache_size += size_delta;
    m_statistics.cache_reads += (hits_delta > 0 || misses_delta > 0) ? 1 : 0;
}

