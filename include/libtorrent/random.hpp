#include <boost/cstdint.hpp>

namespace libtorrent
{
	void random_seed(boost::uint32_t v);
	boost::uint32_t random();
}
