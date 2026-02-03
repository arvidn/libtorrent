#pragma once

#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include <string>
#include <vector>

namespace piece_cache {

/**
 * Load file contents into a vector
 */
bool load_file(std::string const& filename, std::vector<char>& v, int limit = 8000000);

/**
 * Save vector contents to a file
 */
int save_file(std::string const& filename, std::vector<char> const& v);

/**
 * Check if path is absolute
 */
bool is_absolute_path(std::string const& f);

/**
 * Append path components
 */
std::string path_append(std::string const& lhs, std::string const& rhs);

/**
 * Make path absolute
 */
std::string make_absolute_path(std::string const& p);

/**
 * List directory contents with filter
 */
std::vector<std::string> list_dir(
    std::string path,
    bool (*filter_fun)(lt::string_view),
    lt::error_code& ec
);

/**
 * Scan directory for .torrent files and add them to session
 */
void scan_dir(std::string const& dir_path, lt::session& ses);

/**
 * Check if filename is a resume file
 */
bool is_resume_file(std::string const& s);

} // namespace piece_cache
