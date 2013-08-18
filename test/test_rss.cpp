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

#include "libtorrent/config.hpp"
#include "libtorrent/rss.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/http_parser.hpp"

#include "test.hpp"

using namespace libtorrent;

void print_feed(feed_status const& f)
{
	fprintf(stderr, "FEED: %s\n",f.url.c_str());
	if (f.error)
		fprintf(stderr, "ERROR: %s\n", f.error.message().c_str());

	fprintf(stderr, "   %s\n   %s\n", f.title.c_str(), f.description.c_str());
	fprintf(stderr, "   ttl: %d minutes\n", f.ttl);
	fprintf(stderr, "   num items: %d\n", int(f.items.size()));

	for (std::vector<feed_item>::const_iterator i = f.items.begin()
		, end(f.items.end()); i != end; ++i)
	{
		fprintf(stderr, "\033[32m%s\033[0m\n------------------------------------------------------\n"
			"   url: %s\n   size: %"PRId64"\n   info-hash: %s\n   uuid: %s\n   description: %s\n"
			"   comment: %s\n   category: %s\n"
			, i->title.c_str(), i->url.c_str(), i->size
			, i->info_hash.is_all_zeros() ? "" : to_hex(i->info_hash.to_string()).c_str()
			, i->uuid.c_str(), i->description.c_str(), i->comment.c_str(), i->category.c_str());
	}
}

struct rss_expect
{
	rss_expect(int nitems, std::string url, std::string title, size_type size)
		: num_items(nitems), first_url(url), first_title(title), first_size(size)
	{}

	int num_items;
	std::string first_url;
	std::string first_title;
	size_type first_size;
};

void test_feed(std::string const& filename, rss_expect const& expect)
{
	std::vector<char> buffer;
	error_code ec;
	load_file(filename, buffer, ec);
	if (ec)
	{
		fprintf(stderr, "failed to load file \"%s\": %s\n", filename.c_str(), ec.message().c_str());
	}
	TEST_CHECK(!ec);

	char* buf = &buffer[0];
	int len = buffer.size();

	char const header[] = "HTTP/1.1 200 OK\r\n"
		"\r\n";

	boost::shared_ptr<aux::session_impl> s = boost::shared_ptr<aux::session_impl>(new aux::session_impl(
		std::make_pair(100, 200), fingerprint("TT", 0, 0, 0 ,0), NULL, 0
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				, "."
#endif
		));
	s->start_session();

	feed_settings sett;
	sett.auto_download = false;
	sett.auto_map_handles = false;
	boost::shared_ptr<feed> f = boost::shared_ptr<feed>(new feed(*s, sett));
	http_parser parser;
	bool err = false;
	parser.incoming(buffer::const_interval(header, header + sizeof(header)-1), err);
	TEST_CHECK(err == false);

	f->on_feed(error_code(), parser, buf, len);

	feed_status st;
	f->get_feed_status(&st);
	TEST_CHECK(!st.error);

	print_feed(st);

	TEST_CHECK(st.items.size() == expect.num_items);
	if (st.items.size() > 0)
	{
		TEST_CHECK(st.items[0].url == expect.first_url);
		TEST_CHECK(st.items[0].size == expect.first_size);
		TEST_CHECK(st.items[0].title == expect.first_title);
	}

	entry state;
	f->save_state(state);

	fprintf(stderr, "feed_state:\n");
#ifdef TORRENT_DEBUG
	state.print(std::cerr);
#endif

	// TODO: verify some key state is saved in 'state'
}

int test_main()
{
	test_feed("eztv.xml", rss_expect(30, "http://torrent.zoink.it/The.Daily.Show.2012.02.16.(HDTV-LMAO)[VTV].torrent", "The Daily Show 2012-02-16 [HDTV - LMAO]", 183442338));
	test_feed("cb.xml", rss_expect(50, "http://www.clearbits.net/get/1911-norbergfestival-2011.torrent", "Norbergfestival 2011", 1160773632));
	test_feed("kat.xml", rss_expect(25, "http://kat.ph/torrents/benito-di-paula-1975-benito-di-paula-lp-rip-ogg-at-500-jarax4u-t6194897/", "Benito Di Paula - 1975 - Benito Di Paula (LP Rip OGG at 500) [jarax4u]", 168773863));
	test_feed("mn.xml", rss_expect(20, "http://www.mininova.org/get/13203100", "Dexcell - January TwentyTwelve Mix", 137311179));
	test_feed("pb.xml", rss_expect(60, "magnet:?xt=urn:btih:FD4CDDB7BBE722D17A018EFD875EB0695ED7159C&dn=Thompson+Twins+-+1989+-+Big+Trash+%5BMP3%5D", "Thompson Twins - 1989 - Big Trash [MP3]", 100160904));
	return 0;
}

