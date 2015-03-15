/*

Copyright (c) 2009, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/sha1_hash.hpp"
#include <boost/lexical_cast.hpp>
#include <iostream>

#include "test.hpp"
#include "libtorrent/time.hpp"

using namespace libtorrent;

int load_file(std::string const& filename, std::vector<char>& v
	, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::generic_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	if (argc != 2)
	{
		fputs("usage: bdecode_benchmark torrent-file\n", stderr);
		return 1;
	}

	std::vector<char> buf;
	error_code ec;
	int ret = load_file(argv[1], buf, ec, 40 * 1000000);
	if (ret == -1)
	{
		fprintf(stderr, "file too big, aborting\n");
		return 1;
	}

	if (ret != 0)
	{
		fprintf(stderr, "failed to load file: %s\n", ec.message().c_str());
		return 1;
	}

	{
	time_point start(clock_type::now());
	entry e;
	for (int i = 0; i < 1000000; ++i)
	{
		int len;
		e = bdecode(&buf[0], &buf[0] + buf.size(), len);
//		entry& info = e["info"];
	}
	ptime stop(time_now_hires());

	fprintf(stderr, "(slow) bdecode done in %5d ns per message\n"
		, int(total_microseconds(stop - start) / 1000));
	}

	// ===============================================

	{
	ptime start(time_now_hires());
	lazy_entry e;
	for (int i = 0; i < 1000000; ++i)
	{
		error_code ec;
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec);
//		lazy_entry* info = e.dict_find("info");
	}
	time_point stop(clock_type::now());

	fprintf(stderr, "lazy_bdecode done in   %5d ns per message\n"
		, int(total_microseconds(stop - start) / 1000));
	}

	// ===============================================

	{
	ptime start(time_now_hires());
	bdecode_node e;
	e.reserve(100);
	for (int i = 0; i < 1000000; ++i)
	{
		error_code ec;
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);
//		bdecode_node info = e.dict_find("info");
	}
	ptime stop(time_now_hires());

	fprintf(stderr, "bdecode done in        %5d ns per message\n"
		, int(total_microseconds(stop - start) / 1000));
	}

	return 0;
}

