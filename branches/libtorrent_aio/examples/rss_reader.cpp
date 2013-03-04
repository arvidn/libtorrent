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


#include "libtorrent/rss.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/bencode.hpp"
#include <signal.h>
#include <stdio.h>

using namespace libtorrent;

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

std::string const& progress_bar(int progress, int width)
{
	static std::string bar;
	bar.clear();
	bar.reserve(width + 10);

	int progress_chars = (progress * width + 500) / 1000;
	std::fill_n(std::back_inserter(bar), progress_chars, '#');
	std::fill_n(std::back_inserter(bar), width - progress_chars, '-');
	return bar;
}

int save_file(std::string const& filename, std::vector<char>& v)
{
	using namespace libtorrent;

	file f;
	error_code ec;
	if (!f.open(filename, file::write_only, ec)) return -1;
	if (ec) return -1;
	file::iovec_t b = {&v[0], v.size()};
	size_type written = f.writev(0, &b, 1, ec);
	if (written != int(v.size())) return -3;
	if (ec) return -3;
	return 0;
}

volatile bool quit = false;

void sig(int num)
{
	quit = true;
}

int main(int argc, char* argv[])
{
	if ((argc == 2 && strcmp(argv[1], "--help") == 0) || argc > 2)
	{
		fprintf(stderr, "usage: rss_reader [rss-url]\n");
		return 0;
	}

	session ses;

	settings_pack pack;
	pack.set_int(settings_pack::active_downloads, 2);
	pack.set_int(settings_pack::active_seeds, 1);
	pack.set_int(settings_pack::active_limit, 3);
	ses.apply_settings(pack);

	std::vector<char> in;
	error_code ec;
	if (load_file(".ses_state", in, ec) == 0)
	{
		lazy_entry e;
		if (lazy_bdecode(&in[0], &in[0] + in.size(), e, ec) == 0)
			ses.load_state(e);
	}

	feed_handle fh;
	if (argc == 2)
	{
		feed_settings feed;
		feed.url = argv[1];
		feed.add_args.save_path = ".";
		fh = ses.add_feed(feed);
		fh.update_feed();
	}
	else
	{
		std::vector<feed_handle> handles;
		ses.get_feeds(handles);
		if (handles.empty())
		{
			printf("usage: rss_reader rss-url\n");
			return 1;
		}
		fh = handles[0];
	}
	feed_status fs = fh.get_feed_status();
	int i = 0;
	char spinner[] = {'|', '/', '-', '\\'};
	fprintf(stderr, "fetching feed ... %c", spinner[i]);
	while (fs.updating)
	{
		sleep(100);
		i = (i + 1) % 4;
		fprintf(stderr, "\b%c", spinner[i]);
		fs = fh.get_feed_status();
	}
	fprintf(stderr, "\bDONE\n");

	print_feed(fs);

	signal(SIGTERM, &sig);
	signal(SIGINT, &sig);

	while (!quit)
	{
		std::vector<torrent_handle> t = ses.get_torrents();
		for (std::vector<torrent_handle>::iterator i = t.begin()
			, end(t.end()); i != end; ++i)
		{
			torrent_status st = i->status();
			std::string const& progress = progress_bar(st.progress_ppm / 1000, 40);
			std::string name = st.name;
			if (name.size() > 70) name.resize(70);
			std::string error = st.error;
			if (error.size() > 40) error.resize(40);

			static char const* state_str[] =
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::string status = st.paused ? "queued" : state_str[st.state];

			int attribute = 0;
			if (st.paused) attribute = 33;
			else if (st.state == torrent_status::downloading) attribute = 1;

			printf("\033[%dm%2d %-70s d:%-4d u:%-4d %-40s %4d(%4d) %-12s\033[0m\n"
				, attribute, st.queue_position
				, name.c_str(), st.download_rate / 1000
				, st.upload_rate / 1000, !error.empty() ? error.c_str() : progress.c_str()
				, st.num_peers, st.num_seeds, status.c_str());
		}
	
		sleep(500);
		if (quit) break;
		printf("\033[%dA", int(t.size()));
	}

	printf("saving session state\n");
	{
		entry session_state;
		ses.save_state(session_state);

		std::vector<char> out;
		bencode(std::back_inserter(out), session_state);
		save_file(".ses_state", out);
	}

	printf("closing session");
	return 0;
}

