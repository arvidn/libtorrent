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

char rss1[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE torrent PUBLIC \"-//bitTorrent//DTD torrent 0.1//EN\" \"http://xmlns.ezrss.it/0.1/dtd/\">\n"
"<rss version=\"2.0\">\n"
"	<channel>\n"
"		<title>ezRSS - Search Results</title>\n"
"		<ttl>15</ttl>\n"
"		<link>http://ezrss.it/search/index.php?show_name=daily+show&amp;date=&amp;quality=&amp;release_group=&amp;mode=rss</link>\n"
"		<image>\n"
"			<title>ezRSS - Search Results</title>\n"
"			<url>http://ezrss.it/images/ezrssit.png</url>\n"
"			<link>http://ezrss.it/search/index.php?show_name=daily+show&amp;date=&amp;quality=&amp;release_group=&amp;mode=rss</link>\n"
"		</image>\n"
"		<description>Custom RSS feed based off search filters.</description>\n"
"		<item>\n"
"			<title><![CDATA[The Daily Show 2012-02-16 [HDTV - LMAO]]]></title>\n"
"			<link>http://torrent.zoink.it/The.Daily.Show.2012.02.16.(HDTV-LMAO)[VTV].torrent</link>\n"
"			<category domain=\"http://eztv.it/shows/67/the-daily-show/\"><![CDATA[TV Show / The Daily Show]]></category>\n"
"			<pubDate>Thu, 16 Feb 2012 22:54:01 -0500</pubDate>\n"
"			<description><![CDATA[Show Name: The Daily Show; Episode Title: N/A; Episode Date: 2012-02-16]]></description>\n"
"			<enclosure url=\"http://torrent.zoink.it/The.Daily.Show.2012.02.16.(HDTV-LMAO)[VTV].torrent\" length=\"183442338\" type=\"application/x-bittorrent\" />\n"
"			<comments>http://eztv.it/forum/discuss/33253/</comments>\n"
"			<guid>http://eztv.it/ep/33253/the-daily-show-2012-02-16-hdtv-lmao/</guid>\n"
"			<torrent xmlns=\"http://xmlns.ezrss.it/0.1/\">\n"
"				<fileName><![CDATA[The.Daily.Show.2012.02.16.(HDTV-LMAO)[VTV].torrent]]></fileName>\n"
"				<contentLength>183442338</contentLength>\n"
"				<infoHash>1F270E0BCC87575748362788CD5775EFB59C8E1F</infoHash>\n"
"				<magnetURI><![CDATA[magnet:?xt=urn:btih:1F270E0BCC87575748362788CD5775EFB59C8E1F&dn=The.Daily.Show.2012.02.16.(HDTV-LMAO)]]></magnetURI>\n"
"			</torrent>\n"
"		</item>\n"
"		<item>\n"
"			<title><![CDATA[The Daily Show 2012-02-15 [HDTV - FQM]]]></title>\n"
"			<link>http://torrent.zoink.it/The.Daily.Show.2012.02.15.(HDTV-FQM)[VTV].torrent</link>\n"
"			<category domain=\"http://eztv.it/shows/67/the-daily-show/\"><![CDATA[TV Show / The Daily Show]]></category>\n"
"			<pubDate>Wed, 15 Feb 2012 23:13:45 -0500</pubDate>\n"
"			<description><![CDATA[Show Name: The Daily Show; Episode Title: N/A; Episode Date: 2012-02-15]]></description>\n"
"			<enclosure url=\"http://torrent.zoink.it/The.Daily.Show.2012.02.15.(HDTV-FQM)[VTV].torrent\" length=\"183790660\" type=\"application/x-bittorrent\" />\n"
"			<comments>http://eztv.it/forum/discuss/33226/</comments>\n"
"			<guid>http://eztv.it/ep/33226/the-daily-show-2012-02-15-hdtv-fqm/</guid>\n"
"			<torrent xmlns=\"http://xmlns.ezrss.it/0.1/\">\n"
"				<fileName><![CDATA[The.Daily.Show.2012.02.15.(HDTV-FQM)[VTV].torrent]]></fileName>\n"
"				<contentLength>183790660</contentLength>\n"
"				<infoHash>94200845B30F888DD0DFF518F7AA52363A299EF9</infoHash>\n"
"				<magnetURI><![CDATA[magnet:?xt=urn:btih:94200845B30F888DD0DFF518F7AA52363A299EF9&dn=The.Daily.Show.2012.02.15.(HDTV-FQM)]]></magnetURI>\n"
"			</torrent>\n"
"		</item>\n"
"	</channel>\n"
"</rss>\n";


void print_feed(feed_status const& f)
{
	printf("FEED: %s\n",f.url.c_str());
	if (f.error)
		printf("ERROR: %s\n", f.error.message().c_str());

	printf("   %s\n   %s\n", f.title.c_str(), f.description.c_str());
	printf("   ttl: %d minutes\n", f.ttl);

	for (std::vector<feed_item>::const_iterator i = f.items.begin()
		, end(f.items.end()); i != end; ++i)
	{
		printf("\033[32m%s\033[0m\n------------------------------------------------------\n"
			"   url: %s\n   size: %"PRId64"\n   info-hash: %s\n   uuid: %s\n   description: %s\n"
			"   comment: %s\n   category: %s\n"
			, i->title.c_str(), i->url.c_str(), i->size
			, i->info_hash.is_all_zeros() ? "" : to_hex(i->info_hash.to_string()).c_str()
			, i->uuid.c_str(), i->description.c_str(), i->comment.c_str(), i->category.c_str());
	}
}

int test_main()
{

	char* buf = rss1;
	int len = sizeof(rss1);

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
	
	TEST_CHECK(st.items.size() == 2);
	if (st.items.size() == 2)
	{
		TEST_CHECK(st.items[0].url == "http://torrent.zoink.it/The.Daily.Show.2012.02.16.(HDTV-LMAO)[VTV].torrent");
		TEST_CHECK(st.items[0].size == 183442338);
		TEST_CHECK(st.items[0].title == "The Daily Show 2012-02-16 [HDTV - LMAO]");

		TEST_CHECK(st.items[1].url == "http://torrent.zoink.it/The.Daily.Show.2012.02.15.(HDTV-FQM)[VTV].torrent");
		TEST_CHECK(st.items[1].size == 183790660);
		TEST_CHECK(st.items[1].title == "The Daily Show 2012-02-15 [HDTV - FQM]");
	}
	return 0;
}

