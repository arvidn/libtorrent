#include "libtorrent/config.hpp"
#include <boost/cstdint.hpp>

namespace libtorrent
{
	TORRENT_EXTRA_EXPORT void random_seed(boost::uint32_t v);
	boost::uint32_t random();
}
