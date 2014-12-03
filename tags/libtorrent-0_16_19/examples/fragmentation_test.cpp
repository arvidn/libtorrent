/*

Copyright (c) 2010, Arvid Norberg
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

#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"

#include <boost/utility.hpp>

#include <stdlib.h>

using namespace libtorrent;

int main(int argc, char* argv[])
{
	if (argc != 3 && argc != 2)
	{
		fprintf(stderr, "Usage: fragmentation_test torrent-file file-storage-path\n"
			"       fragmentation_test file\n\n");
		return 1;
	}

	if (argc == 2)
	{
		error_code ec;
		file f(argv[1], file::read_only, ec);
		if (ec)
		{
			fprintf(stderr, "Error opening file %s: %s\n", argv[1], ec.message().c_str());
			return 1;
		}
		size_type off = f.phys_offset(0);
		printf("physical offset of file %s: %" PRId64 "\n", argv[1], off);
		return 0;
	}

	error_code ec;
	boost::intrusive_ptr<torrent_info> ti(new torrent_info(argv[1], ec));

	if (ec)
	{
		fprintf(stderr, "Error while loading torrent file: %s\n", ec.message().c_str());
		return 1;
	}

	file_pool fp;
	boost::shared_ptr<storage_interface> st(default_storage_constructor(ti->files(), 0, argv[2], fp, std::vector<boost::uint8_t>()));

	// the first field is the piece index, the second
	// one is the physical location of the piece on disk
	std::vector<std::pair<int, size_type> > pieces;

	// make sure all the files are there
/*	std::vector<std::pair<size_type, std::time_t> > files = get_filesizes(ti->files(), argv[2]);
	for (int i = 0; i < ti->num_files(); ++i)
	{
		if (ti->file_at(i).size == files[i].first) continue;
		fprintf(stderr, "Files for this torrent are missing or incomplete: %s was %" PRId64 " bytes, expected %" PRId64 " bytes\n"
			, ti->files().file_path(ti->file_at(i)).c_str(), files[i].first, ti->file_at(i).size);
		return 1;
	}
*/
	bool warned = false;
	for (int i = 0; i < ti->num_pieces(); ++i)
	{
		pieces.push_back(std::make_pair(i, st->physical_offset(i, 0)));
		if (pieces.back().second == size_type(i) * ti->piece_length())
		{
			if (!warned)
			{
				fprintf(stderr, "The files are incomplete\n");
				warned = true;
			}
			pieces.pop_back();
		}
	}

	// this suggests that the OS doesn't support physical offset
	// or that the file doesn't exist or is incomplete
	if (pieces.empty())
	{
		fprintf(stderr, "Your operating system or filesystem "
			"does not appear to support querying physical disk offset\n");
	}

	FILE* f = fopen("fragmentation.log", "w+");
	if (f == 0)
	{
		fprintf(stderr, "error while opening log file: %s\n", strerror(errno));
		return 1;
	}

	for (int i = 0; i < pieces.size(); ++i)
	{
		fprintf(f, "%d %" PRId64 "\n", pieces[i].first, pieces[i].second);
	}

	fclose(f);

	f = fopen("fragmentation.gnuplot", "w+");
	if (f == 0)
	{
		fprintf(stderr, "error while opening gnuplot file: %s\n", strerror(errno));
		return 1;
	}

	fprintf(f,
		"set term png size 1200,800\n"
		"set output \"fragmentation.png\"\n"
		"set xrange [*:*]\n"
		"set xlabel \"piece\"\n"
		"set ylabel \"drive offset\"\n"
		"set key box\n"
		"set title \"fragmentation for '%s'\"\n"
		"set tics nomirror\n"
		"plot \"fragmentation.log\" using 1:2 with points lt rgb \"#e07070\" notitle axis x1y1\n"
		, ti->name().c_str());

	fclose(f);

	system("gnuplot fragmentation.gnuplot");
}


