#ifndef TORRENT_PLATFORM_UTIL_HPP
#define TORRENT_PLATFORM_UTIL_HPP

#include <cstdint>

namespace libtorrent {

	int max_open_files();

	void set_thread_name(char const* name);

}

#endif // TORRENT_PLATFORM_UTIL_HPP
