#include "libtorrent/hasher.hpp"
#include <boost/lexical_cast.hpp>

#include "test.hpp"

using namespace libtorrent;

// test vectors from RFC 3174
// http://www.faqs.org/rfcs/rfc3174.html

char const* test_array[4] =
{
	"abc",
	"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
	"a",
	"0123456701234567012345670123456701234567012345670123456701234567"
};

long int repeat_count[4] = { 1, 1, 1000000, 10 };

char const* result_array[4] =
{
	"A9993E364706816ABA3E25717850C26C9CD0D89D",
	"84983E441C3BD26EBAAE4AA1F95129E5E54670F1",
	"34AA973CD4C4DAA4F61EEB2BDBAD27316534016F",
	"DEA356A2CDDD90C7A7ECEDC5EBB563934F460452"
};


int test_main()
{
	using namespace libtorrent;

	for (int test = 0; test < 4; ++test)
	{
		hasher h;
		for (int i = 0; i < repeat_count[test]; ++i)
			h.update(test_array[test], std::strlen(test_array[test]));

		sha1_hash result = boost::lexical_cast<sha1_hash>(result_array[test]);
		TEST_CHECK(result == h.final());
	}

	return 0;
}

