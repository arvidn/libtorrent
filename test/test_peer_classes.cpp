/*

Copyright (c) 2012, Arvid Norberg
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

#include "test.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_class_set.hpp"
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/session.hpp"

using namespace libtorrent;

std::string class_name(peer_class_t id, peer_class_pool const& p)
{
	peer_class const* c = p.at(id);
	TEST_CHECK(c != NULL);
	if (c == NULL) return "";
	peer_class_info i;
	c->get_info(&i);
	return i.label;
}

TORRENT_TEST(peer_class)
{
	peer_class_pool pool;

	peer_class_t id1 = pool.new_peer_class("test1");
	peer_class_t id2 = pool.new_peer_class("test2");

	// make sure there's no leak
	for (int i = 0; i < 1000; ++i)
	{
		peer_class_t tmp = pool.new_peer_class("temp");
		pool.decref(tmp);
	}

	peer_class_t id3 = pool.new_peer_class("test3");

	TEST_CHECK(id3 == id2 + 1);

	// make sure refcounting works
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.incref(id3);
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.decref(id3);
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.decref(id3);
	// it should have been deleted now
	TEST_CHECK(pool.at(id3) == NULL);

	// test setting and retrieving upload and download rates
	pool.at(id2)->set_upload_limit(1000);
	pool.at(id2)->set_download_limit(2000);

	peer_class_info i;
	pool.at(id2)->get_info(&i);
	TEST_EQUAL(i.upload_limit, 1000);
	TEST_EQUAL(i.download_limit, 2000);

	// test peer_class_type_filter
	peer_class_type_filter filter;

	for (int i = 0; i < 5; ++i)
	{
		TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)i
			, 0xffffffff) == 0xffffffff);
	}

	filter.disallow((libtorrent::peer_class_type_filter::socket_type_t)0, 0);
	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)0
		, 0xffffffff) == 0xfffffffe);
	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)1
		, 0xffffffff) == 0xffffffff);
	filter.allow((libtorrent::peer_class_type_filter::socket_type_t)0, 0);
	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)0
		, 0xffffffff) == 0xffffffff);

	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)0, 0) == 0);
	filter.add((libtorrent::peer_class_type_filter::socket_type_t)0, 0);
	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)0, 0) == 1);
	filter.remove((libtorrent::peer_class_type_filter::socket_type_t)0, 0);
	TEST_CHECK(filter.apply((libtorrent::peer_class_type_filter::socket_type_t)0, 0) == 0);

	pool.decref(id2);
	pool.decref(id1);
	TEST_CHECK(pool.at(id2) == NULL);
	TEST_CHECK(pool.at(id1) == NULL);
}

TORRENT_TEST(session_peer_class_filter)
{
	using namespace libtorrent;
	session ses;
	peer_class_t my_class = ses.create_peer_class("200.1.x.x IP range");

	ip_filter f;
	f.add_rule(address_v4::from_string("200.1.1.0")
		, address_v4::from_string("200.1.255.255")
		, 1 << my_class);
	ses.set_peer_class_filter(f);

#if TORRENT_USE_IPV6
	TEST_CHECK(boost::get<0>(ses.get_peer_class_filter().export_filter())
		== boost::get<0>(f.export_filter()));
#else
	TEST_CHECK(ses.get_peer_class_filter().export_filter() == f.export_filter());
#endif
}

TORRENT_TEST(session_peer_class_type_filter)
{
	using namespace libtorrent;
	session ses;
	peer_class_t my_class = ses.create_peer_class("all utp sockets");

	peer_class_type_filter f;
	f.add(peer_class_type_filter::utp_socket, my_class);
	f.disallow(peer_class_type_filter::utp_socket, session::global_peer_class_id);
	ses.set_peer_class_type_filter(f);

	TEST_CHECK(ses.get_peer_class_type_filter() == f);
}

