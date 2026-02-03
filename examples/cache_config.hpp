#pragma once

#include <string>

namespace piece_cache {

/**
 * Configuration settings for piece cache functionality
 */
struct CacheConfig {
    bool enable_cache = true;                   // Enable piece caching
    bool cache_during_download = false;         // Cache pieces as they complete
    bool disable_original_storage = false;      // Disable original content storage (-Z flag)
    bool seed_from_cache = false;               // Seed from cache only (-S flag)
    std::string cache_root = "./piece_cache";   // Root directory for cache
};

// Global cache configuration
extern CacheConfig g_cache_config;

} // namespace piece_cache
