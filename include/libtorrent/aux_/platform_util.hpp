#ifndef TORRENT_PLATFORM_UTIL_HPP
#define TORRENT_PLATFORM_UTIL_HPP

#include "libtorrent/aux_/export.hpp"

#include <cstdint>

namespace libtorrent::aux {

	TORRENT_EXTRA_EXPORT int max_open_files();

	void set_thread_name(char const* name);

}

#endif // TORRENT_PLATFORM_UTIL_HPP
