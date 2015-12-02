#ifndef TORRENT_PLATFORM_UTIL_HPP
#define TORRENT_PLATFORM_UTIL_HPP

#include <boost/cstdint.hpp>

namespace libtorrent
{
	int max_open_files();

	boost::uint64_t total_physical_ram();
}

#endif // TORRENT_PLATFORM_UTIL_HPP

