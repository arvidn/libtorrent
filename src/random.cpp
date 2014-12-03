#include "libtorrent/config.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	namespace
	{
		boost::uint32_t x = 123456789;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool seeded = false;
#endif
	}

	void random_seed(boost::uint32_t v)
	{
		x = v;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		seeded = true;
#endif
	}

	// this is an xorshift random number generator
	// see: http://en.wikipedia.org/wiki/Xorshift
	boost::uint32_t random()
	{
		TORRENT_ASSERT(seeded);

		static boost::uint32_t y = 362436069;
		static boost::uint32_t z = 521288629;
		static boost::uint32_t w = 88675123;
		boost::uint32_t t;

		t = x ^ (x << 11);
		x = y; y = z; z = w;
		return w = w ^ (w >> 19) ^ (t ^ (t >> 8));
	}
}

