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

#include <stdlib.h>
#include <string>
#include <cstring>
#include "libtorrent/file.hpp"

using namespace libtorrent;

void print_usage()
{
	fprintf(stderr, "usage: parse_hash_fails log-directory\n");
	exit(1);
}

int main(int argc, char* argv[])
{
	if (argc != 2) print_usage();

	error_code ec;
	directory d(argv[1], ec);
	if (ec)
	{
		fprintf(stderr, "failed to open directory: %s\n%s\n"
			, argv[1], ec.message().c_str());
		return 1;
	}

	for (; !d.done(); d.next(ec))
	{
		if (ec)
		{
			fprintf(stderr, "error listing directory: %s\n", ec.message().c_str());
			return 1;
		}
		std::string filename = d.file();
		char hash[41];
		int piece;
		int block;
		char state[10]; // good, bad
		if (sscanf(filename.c_str(), "%40s_%d_%d_%4s.block", hash, &piece, &block, state) != 4)
		{
			fprintf(stderr, "no match: %s\n", filename.c_str());
			continue;
		}

		if (strcmp(state, "good") != 0) continue;

		char target_filename[1024];
		snprintf(target_filename, sizeof(target_filename), "%s_%d_%d.diff", hash, piece, block);

		fprintf(stderr, "diffing %s\n", filename.c_str());
		char cmdline[2048];
		snprintf(cmdline, sizeof(cmdline), "xxd %s >temp_good", combine_path(argv[1], filename).c_str());
		system(cmdline);
		snprintf(cmdline, sizeof(cmdline), "xxd %s/%s_%d_%d_bad.block >temp_bad", argv[1], hash, piece, block);
		system(cmdline);
		snprintf(cmdline, sizeof(cmdline), "diff -y temp_good temp_bad | colordiff >%s"
			, combine_path(argv[1], target_filename).c_str());
		system(cmdline);
	}

	FILE* log_file = fopen(argv[1], "r");
	if (log_file == 0)
	{
		fprintf(stderr, "failed to open logfile: %s\n%d: %s\n"
			, argv[1], errno, strerror(errno));
		return 1;
	}

	return 0;
}


