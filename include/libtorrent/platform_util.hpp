#ifndef TORRENT_PLATFORM_UTIL_HPP
#define TORRENT_PLATFORM_UTIL_HPP

#include <cstdint>

namespace libtorrent {

	int max_open_files();

	std::int64_t total_physical_ram();
}

#endif // TORRENT_PLATFORM_UTIL_HPP
