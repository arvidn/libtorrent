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

#include "libtorrent/file.hpp"
#include "libtorrent/io.hpp"
#include <cstring>
#include <boost/bind.hpp>
#include <stdlib.h>
#include <map>
#include <vector>

using namespace libtorrent;
using namespace libtorrent::detail; // for write_* and read_*

void print_usage()
{
	fprintf(stderr, "usage: parse_request_log log-file\n");
	exit(1);
}

// the event format in the log is:
// uint64_t timestamp (microseconds)
// uint64_t info-hash prefix
// uint32_t peer identifier
// uint32_t piece
// uint32_t start offset
// uint32_t length

struct request
{
	boost::uint64_t timestamp;
	boost::uint64_t infohash;
	boost::uint32_t peer;
	boost::uint32_t piece;
	boost::uint32_t start;
	boost::uint32_t length;
};

struct object_entry
{
	int hits;
	int cache_hits;
	bool operator<(object_entry const& rhs) const
	{
		return hits < rhs.hits;
	}
};



struct average_time
{
	average_time() : first_request(0), num_requests(0), last_peer(0) {}

	void sample(boost::uint32_t peer, boost::uint64_t timestamp)
	{
//		if (peer == last_peer) return;
//		last_peer = peer;
		++num_requests;
		if (first_request == 0)
		{
			first_request = timestamp;
			last_request = timestamp;
		}
		else
		{
			assert(timestamp >= last_request);
			last_request = timestamp;
		}
	}

	float request_rate() const
	{
		if (num_requests <= 8) return 0.f;
		return float(num_requests) / float(last_request - first_request) * 1000000.f;
	}
	boost::uint64_t first_request;
	boost::uint64_t last_request;
	int num_requests;
	boost::uint32_t last_peer;
};

struct cache
{
	virtual bool incoming_request(request const& r) = 0;
	virtual ~cache() {}
};

struct noop_cache : cache
{
	virtual bool incoming_request(request const& r) { return false; }
};

struct lru_cache : cache
{
	lru_cache(int size) : m_size(size) {}
	virtual bool incoming_request(request const& r)
	{
		boost::uint64_t piece = (r.infohash & 0xffffffff00000000LL) | r.piece;
		cache_t::iterator i = m_cache.find(piece);
		if (i != m_cache.end())
		{
			i->second = r.timestamp;
			return true;
		}

		// cache miss, insert this piece
		if (m_cache.size() == m_size)
		{
			// need to evict the least recently used. This is
			// a stupid expensive implementation (but simple)

			i = std::min_element(m_cache.begin(), m_cache.end()
				, boost::bind(&cache_t::value_type::second, _1)
				< boost::bind(&cache_t::value_type::second, _2));

			m_cache.erase(i);
		}
		m_cache[piece] = r.timestamp;
		return false;
	}
	int m_size;
	// maps piece -> timestamp
	typedef std::map<boost::uint64_t, boost::uint64_t> cache_t;
	cache_t m_cache;
};


int main(int argc, char* argv[])
{
	if (argc != 2 && argc != 4) print_usage();

	cache* disk_cache = 0;

	if (argc == 4)
	{
		int size = atoi(argv[2]);
		if (strcmp(argv[1], "lru") == 0)
		{
			disk_cache = new lru_cache(size);
		}

		argv += 2;
	}

	if (disk_cache == 0)
	{
		disk_cache = new noop_cache;
	}

	FILE* log_file = fopen(argv[1], "r");
	if (log_file == 0)
	{
		fprintf(stderr, "failed to open logfile: %s\n%d: %s\n"
			, argv[1], errno, strerror(errno));
		return 1;
	}

	FILE* expand_file = fopen("expanded_requests.log", "w+");
	std::map<boost::uint64_t, object_entry> torrent_map;
	std::map<boost::uint64_t, object_entry> piece_map;
	std::map<boost::uint64_t, average_time> piece_frequency_map;
	std::map<boost::uint32_t, object_entry> peer_map;

	boost::uint64_t first_timestamp = 0;

	for (;;)
	{
		char entry[sizeof(request)];
		char* ptr = entry;
		int ret = fread(&entry, 1, sizeof(entry), log_file);
		if (ret != sizeof(entry)) break;

		request r;
		r.timestamp = read_uint64(ptr);
		r.infohash = read_uint64(ptr);
		r.peer = read_uint32(ptr);
		r.piece = read_uint32(ptr);
		r.start = read_uint32(ptr);
		r.length = read_uint32(ptr);

		if (first_timestamp == 0) first_timestamp = r.timestamp;

		fprintf(expand_file, "%"PRIu64"\t%"PRIu64"\t%"PRIu32"\t%"PRIu32"\t%"PRIu32"\t%"PRIu32"\n"
			, r.timestamp, r.infohash, r.peer, r.piece, r.start, r.length);

		bool hit = disk_cache->incoming_request(r);

		boost::uint64_t piece = (r.infohash & 0xffffffff00000000LL) | r.piece;
		torrent_map[r.infohash].hits += 1;
		peer_map[r.peer].hits += 1;
		piece_map[piece].hits += 1;
		piece_frequency_map[piece].sample(r.peer, r.timestamp);
		if (hit)
		{
			torrent_map[r.infohash].cache_hits += 1;
			peer_map[r.peer].cache_hits += 1;
			piece_map[piece].cache_hits += 1;
		}
	}

	fclose(expand_file);

	// === torrents ===
	FILE* file = fopen("torrent_dist.log", "w+");
	std::vector<std::pair<object_entry, boost::uint64_t> > histogram;
	for (std::map<boost::uint64_t, object_entry>::iterator i = torrent_map.begin()
		, end(torrent_map.end()); i != end; ++i)
	{
		histogram.push_back(std::pair<object_entry, boost::uint64_t>(i->second, i->first));
	}
	std::sort(histogram.begin(), histogram.end());

	int count = 0;
	for (std::vector<std::pair<object_entry, boost::uint64_t> >::iterator i = histogram.begin()
		, end(histogram.end()); i != end; ++i, ++count)
	{
		fprintf(file, "%d\t%d\t%d\n", count, i->first.hits, i->first.cache_hits);
	}
	fclose(file);

	// === peers ===
	file = fopen("peer_dist.log", "w+");
	histogram.clear();
	for (std::map<boost::uint32_t, object_entry>::iterator i = peer_map.begin()
		, end(peer_map.end()); i != end; ++i)
	{
		histogram.push_back(std::pair<object_entry, boost::uint64_t>(i->second, i->first));
	}
	std::sort(histogram.begin(), histogram.end());

	count = 0;
	for (std::vector<std::pair<object_entry, boost::uint64_t> >::iterator i = histogram.begin()
		, end(histogram.end()); i != end; ++i, ++count)
	{
		fprintf(file, "%d\t%d\t%d\n", count, i->first.hits, i->first.cache_hits);
	}
	fclose(file);

	// === pieces ===
	file = fopen("piece_dist.log", "w+");
	histogram.clear();
	for (std::map<boost::uint64_t, object_entry>::iterator i = piece_map.begin()
		, end(piece_map.end()); i != end; ++i)
	{
		histogram.push_back(std::pair<object_entry, boost::uint64_t>(i->second, i->first));
	}
	std::sort(histogram.begin(), histogram.end());

	count = 0;
	for (std::vector<std::pair<object_entry, boost::uint64_t> >::iterator i = histogram.begin()
		, end(histogram.end()); i != end; ++i, ++count)
	{
		fprintf(file, "%d\t%d\t%d\n", count, i->first.hits, i->first.cache_hits);
	}
	fclose(file);

	// === piece frequency ===
	file = fopen("piece_frequency_dist.log", "w+");
	// just update the histogram to get the piece mapping the same

	count = 0;
	for (std::vector<std::pair<object_entry, boost::uint64_t> >::iterator i = histogram.begin()
		, end(histogram.end()); i != end; ++i, ++count)
	{
		fprintf(file, "%d\t%f\n", count, piece_frequency_map[i->second].request_rate());
	}
	fclose(file);

	FILE* gnuplot = fopen("requests.gnuplot", "w+");

	char const* gnuplot_file =
		"set term png size 1400,1024\n"
		"set output \"requests-torrent-histogram.png\"\n"
		"set xlabel \"torrent\"\n"
		"set ylabel \"number of requests\"\n"
		"plot \"torrent_dist.log\" using 1:2 title \"torrent request\" with boxes, "
		"\"torrent_dist.log\" using 1:3 title \"torrent cache hits\" with boxes\n"

		"set output \"requests-peer-histogram.png\"\n"
		"set xlabel \"peer\"\n"
		"set ylabel \"number of requests\"\n"
		"plot \"peer_dist.log\" using 1:2 title \"peer request\" with boxes, "
		"\"peer_dist.log\" using 1:3 title \"peer cache hits\" with boxes\n"

		"set output \"requests-piece-histogram.png\"\n"
		"set xlabel \"piece\"\n"
		"set ylabel \"number of requests\"\n"
		"plot \"piece_dist.log\" using 1:2 title \"piece requests\" with boxes, "
		"\"piece_dist.log\" using 1:3 title \"piece cache hits\" with boxes\n"

		"set output \"requests-piece-frequency-histogram.png\"\n"
		"set xlabel \"piece\"\n"
		"set ylabel \"average requests per second\"\n"
		"set yrange [0: 0.02]\n"
		"plot \"piece_frequency_dist.log\" using 1:2 title \"piece request frequency\" with boxes\n"
		;


	fwrite(gnuplot_file, strlen(gnuplot_file), 1, gnuplot);
	fclose(gnuplot);

	system("gnuplot requests.gnuplot");

	return 0;
}


