/*

Copyright (c) 2011, Arvid Norberg
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

using namespace libtorrent;
using namespace libtorrent::detail; // for write_* and read_*

void print_usage()
{
	fprintf(stderr, "usage: parse_access_log log-file data-file\n\n"
		"prints a gnuplot readable data file to stdout\n");
	exit(1);
}

struct file_op
{
	boost::uint64_t timestamp;
	boost::uint64_t offset;
	boost::uint32_t file;
	boost::uint8_t event;
};

int main(int argc, char* argv[])
{
	if (argc != 3) print_usage();

	FILE* log_file = fopen(argv[1], "r");
	if (log_file == 0)
	{
		fprintf(stderr, "failed to open logfile: %s\n%d: %s\n"
			, argv[1], errno, strerror(errno));
		return 1;
	}

	FILE* writes_file = fopen("writes.log", "w+");
	FILE* reads_file = fopen("reads.log", "w+");

	FILE* writes_elev_file = fopen("writes_elevator.log", "w+");
	FILE* reads_elev_file = fopen("reads_elevator.log", "w+");

	// TODO: in order to generalize this, the filenames need to be
	// saved in the log itself

	error_code ec;
	file data_file(argv[2], file::read_only, ec);
	if (ec)
	{
		fprintf(stderr, "failed to open data file: %s\n", ec.message().c_str());
		return 1;
	}

	typedef std::map<boost::uint64_t, file_op> op_map;
	op_map outstanding_ops;

	boost::uint64_t first_timestamp = 0;

	for (;;)
	{
		char entry[29];
		char* ptr = entry;
		int ret = fread(&entry, 1, sizeof(entry), log_file);
		if (ret != sizeof(entry)) break;

		file_op op;
		op.timestamp = read_uint64(ptr);
		op.offset = read_uint64(ptr);
		boost::uint64_t event_id = read_uint64(ptr);
		op.file = read_uint32(ptr);
		op.event = read_uint8(ptr);

		if (first_timestamp == 0) first_timestamp = op.timestamp;

		bool write = op.event & 1;
		bool complete = op.event & 2;
		FILE* out_file = 0;
		if (complete)
		{
			op_map::iterator i = outstanding_ops.find(event_id);
			if (i != outstanding_ops.end())
			{
				if (i->second.timestamp > op.timestamp)
				{
					fprintf(stderr, "end-event stamped before "
						"end-event: %"PRId64" started at: %f file: %u\n"
						, op.offset, double(i->second.timestamp) / 1000000.f
						, op.file);
					i->second.timestamp = op.timestamp;
				}
				
				boost::uint64_t offset = data_file.phys_offset(op.offset);
				out_file = write ? writes_file : reads_file;
				double start_time = double(i->second.timestamp - first_timestamp) / 1000000.0;
				double end_time = double(op.timestamp - first_timestamp) / 1000000.0;
				double duration_time = double(op.timestamp - i->second.timestamp) / 1000000.0;
				fprintf(out_file, "%f\t%"PRId64"\t%f\t%"PRId64"\n"
					, start_time, offset, duration_time, op.offset);

				out_file = write ? writes_elev_file : reads_elev_file;
				fprintf(out_file, "%f\t%"PRId64"\n", end_time, offset);

				outstanding_ops.erase(i);
			}
			else
			{
				fprintf(stderr, "no start event for (%"PRId64"): %"PRId64" ended at: %f file: %u\n"
					, event_id, op.offset, double(op.timestamp) / 1000000.f, op.file);
			}
		}
		else
		{
			op_map::iterator i = outstanding_ops.find(event_id);
			if (i != outstanding_ops.end())
			{
				fprintf(stderr, "duplicate start event for (%"PRId64"): %"PRId64" at: %f file: %u "
					"(current start is at: %f)\n"
					, event_id, op.offset, double(i->second.timestamp - first_timestamp) / 1000000.f, op.file
					, double(op.timestamp - first_timestamp) / 1000000.f);
			}
			else
			{
				outstanding_ops[event_id] = op;
			}
		}
	}

	fclose(writes_file);
	fclose(reads_file);
	fclose(writes_elev_file);
	fclose(reads_elev_file);
	fclose(log_file);

	FILE* gnuplot = fopen("file_access.gnuplot", "w+");

	char const* gnuplot_file =
		"set term png size 1400,1024\n"
		"set output \"file_access.png\"\n"
		"set xlabel \"time (s)\"\n"
		"set ylabel \"file offset\"\n"
		"set style line 1 lc rgb \"#ff8888\"\n"
		"set style line 2 lc rgb \"#88ff88\"\n"
		"set style arrow 1 nohead ls 1\n"
		"set style arrow 2 nohead ls 2\n"
		"plot \"writes.log\" using 1:4:3:(0) title \"writes\" with vectors arrowstyle 1, " 
			"\"reads.log\" using 1:4:3:(0) title \"reads\" with vectors arrowstyle 2\n"
		"set output \"file_access_physical.png\"\n"
		"set ylabel \"physical disk offset\"\n"
		"plot \"writes.log\" using 1:2:3:(0) title \"writes\" with vectors arrowstyle 1, " 
			"\"reads.log\" using 1:2:3:(0) title \"reads\" with vectors arrowstyle 2\n";

	fwrite(gnuplot_file, strlen(gnuplot_file), 1, gnuplot);
	fclose(gnuplot);

	system("gnuplot file_access.gnuplot");

	assert(outstanding_ops.empty());

	return 0;
}


