
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

	session_settings sett;
	sett.active_downloads = 2;
	sett.active_seeds = 1;
	sett.active_limit = 3;
	ses.set_settings(sett);

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
			std::string name = i->name();
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

