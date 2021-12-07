/*

Copyright (c) 2014-2017, 2019-2021, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/session.hpp"

using namespace lt;

namespace {
std::string class_name(peer_class_t id, peer_class_pool const& p)
{
	peer_class const* c = p.at(id);
	TEST_CHECK(c != nullptr);
	if (c == nullptr) return "";
	peer_class_info i;
	c->get_info(&i);
	return i.label;
}
} // anonymous namespace

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

	TEST_CHECK(id3 == next(id2));

	// make sure refcounting works
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.incref(id3);
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.decref(id3);
	TEST_EQUAL(class_name(id3, pool), "test3");
	pool.decref(id3);
	// it should have been deleted now
	TEST_CHECK(pool.at(id3) == nullptr);

	// test setting and retrieving upload and download rates
	pool.at(id2)->set_upload_limit(1000);
	pool.at(id2)->set_download_limit(2000);

	peer_class_info cls;
	pool.at(id2)->get_info(&cls);
	TEST_EQUAL(cls.upload_limit, 1000);
	TEST_EQUAL(cls.download_limit, 2000);

	// test peer_class_type_filter
	peer_class_type_filter filter;

	for (int i = 0; i < 5; ++i)
	{
		TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(i)
			, 0xffffffff) == 0xffffffff);
	}

	filter.disallow(static_cast<lt::peer_class_type_filter::socket_type_t>(0), peer_class_t{0});
	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(0)
		, 0xffffffff) == 0xfffffffe);
	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(1)
		, 0xffffffff) == 0xffffffff);
	filter.allow(static_cast<lt::peer_class_type_filter::socket_type_t>(0), peer_class_t{0});
	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(0)
		, 0xffffffff) == 0xffffffff);

	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(0), 0) == 0);
	filter.add(static_cast<lt::peer_class_type_filter::socket_type_t>(0), peer_class_t{0});
	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(0), 0) == 1);
	filter.remove(static_cast<lt::peer_class_type_filter::socket_type_t>(0), peer_class_t{0});
	TEST_CHECK(filter.apply(static_cast<lt::peer_class_type_filter::socket_type_t>(0), 0) == 0);

	pool.decref(id2);
	pool.decref(id1);
	TEST_CHECK(pool.at(id2) == nullptr);
	TEST_CHECK(pool.at(id1) == nullptr);
}

TORRENT_TEST(session_peer_class_filter)
{
	using namespace libtorrent;
	session ses;
	peer_class_t my_class = ses.create_peer_class("200.1.x.x IP range");

	ip_filter f;
	f.add_rule(make_address_v4("200.1.1.0")
		, make_address_v4("200.1.255.255")
		, 1 << static_cast<std::uint32_t>(my_class));
	ses.set_peer_class_filter(f);

	TEST_CHECK(std::get<0>(ses.get_peer_class_filter().export_filter())
		== std::get<0>(f.export_filter()));
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
