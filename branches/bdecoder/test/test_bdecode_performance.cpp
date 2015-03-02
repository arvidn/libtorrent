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

sha1_hash generate_id()
{
	sha1_hash ret;
	for (int i = 0; i < 20; ++i) ret[i] = rand() & 0xff;
	return ret;
}
int main()
{
	using namespace libtorrent;

	// generate an example DHT message to use in the parser benchmark
	entry e;
	e["q"] = "find_node";
	e["t"] = 3235;
	e["y"] = "q";
	entry::dictionary_type& a = e["a"].dict();
	a["id"] = generate_id().to_string();
	a["target"] = generate_id().to_string();
	a["n"] = "test-name";
	char b[1500];
	bencode(b, e);

	{
	ptime start(time_now_hires());
	entry e;
	for (int i = 0; i < 1000000; ++i)
	{
		int len;
		e = bdecode(b, b + sizeof(b)-1, len);
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
		lazy_bdecode(b, b + sizeof(b)-1, e, ec);
	}
	ptime stop(time_now_hires());

	fprintf(stderr, "lazy_bdecode done in   %5d ns per message\n"
		, int(total_microseconds(stop - start) / 1000));
	}

	// ===============================================

	{
	ptime start(time_now_hires());
	bdecode_node e;
	for (int i = 0; i < 1000000; ++i)
	{
		error_code ec;
		bdecode(b, b + sizeof(b)-1, e, ec);
	}
	ptime stop(time_now_hires());

	fprintf(stderr, "bdecode done in        %5d ns per message\n"
		, int(total_microseconds(stop - start) / 1000));
	}

	return 0;
}

