#include "libtorrent/lazy_entry.hpp"
#include <boost/lexical_cast.hpp>
#include <iostream>

#include "test.hpp"
#include "libtorrent/time.hpp"

using namespace libtorrent;

int test_main()
{
	using namespace libtorrent;

	ptime start(time_now());

	for (int i = 0; i < 100000; ++i)
	{
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbbe";
		lazy_entry e;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
	}
	ptime stop(time_now());

	std::cout << "done in " << total_milliseconds(stop - start) / 100. << " seconds per million message" << std::endl;
	return 0;
}

