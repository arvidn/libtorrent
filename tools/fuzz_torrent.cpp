/*

Copyright (c) 2015, Arvid Norberg
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

#include <string>
#include <boost/cstdint.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include "libtorrent/bdecode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/error_code.hpp"

using libtorrent::bdecode_node;
using boost::random::mt19937;
using boost::random::uniform_int_distribution;

char const* invalid_utf8_sequences[] =
{
"\x80",
"\xbf",
"\xff",
"\xfe",
"\xff\xff\xfe\xfe",
"\xc0\xaf",
"\xe0\x80\xaf",
"\xf0\x80\x80\xaf",
"\xf8\x80\x80\x80\xaf ",
"\xfc\x80\x80\x80\x80\xaf",
"\xc1\xbf",
"\xe0\x9f\xbf",
"\xf0\x8f\xbf\xbf",
"\xf8\x87\xbf\xbf\xbf",
"\xfc\x83\xbf\xbf\xbf\xbf",
"\xc0\x80",
"\xe0\x80\x80",
"\xf0\x80\x80\x80",
"\xf8\x80\x80\x80\x80",
"\xfc\x80\x80\x80\x80\x80",
"\xed\xa0\x80",
"\xed\xad\xbf",
"\xed\xae\x80",
"\xed\xaf\xbf",
"\xed\xb0\x80",
"\xed\xbe\x80",
"\xed\xbf\xbf",
"\xed\xa0\x80\xed\xb0\x80",
"\xed\xa0\x80\xed\xbf\xbf",
"\xed\xad\xbf\xed\xb0\x80",
"\xed\xad\xbf\xed\xbf\xbf",
"\xed\xae\x80\xed\xb0\x80",
"\xed\xae\x80\xed\xbf\xbf",
"\xed\xaf\xbf\xed\xb0\x80",
"\xed\xaf\xbf\xed\xbf\xbf",
};

boost::int64_t g_seed;

void print_ascii_number(std::string& output, boost::int64_t val)
{
	const bool overflow = g_seed == 1;
	const bool underflow = g_seed == 2;
	const bool negative = g_seed == 3;
	const bool double_negative = g_seed == 4;
	const bool zero = g_seed == 5;
	g_seed -= 5;

	char const* numbers = "0123456789";
	if (zero)
	{
		output += '0';
	}
	else if (underflow)
	{
		output += '-';
		for (int i = 0; i < 100; ++i) output += numbers[rand() % 10];
		return;
	}
	else if (overflow)
	{
		for (int i = 0; i < 100; ++i) output += numbers[rand() % 10];
		return;
	}
	else
	{
		if (negative) output += '-';
		else if (double_negative) output += "--";
		char buf[50];
		snprintf(buf, sizeof(buf), "%" PRId64 "", val);
		output += buf;
	}
}

void print_string(std::string& output, std::string str)
{
	const bool empty_string = g_seed == 1;
	g_seed -= 1;
	if (empty_string)
	{
		print_ascii_number(output, 0);
		output += ':';
		return;
	}

	const bool random_string = g_seed > 0 && g_seed <= 1000;
	const int str_seed = g_seed - 1;
	g_seed -= 1000;
	if (random_string)
	{
		static mt19937 random_engine(str_seed);
		uniform_int_distribution<boost::uint8_t> d(0, 255);
		for (int i = 0; i < str.size(); ++i)
			str[i] = d(random_engine);

		print_ascii_number(output, str.size());
		output += ':';
		output += str;
		return;
	}

	const int num_sequences = (sizeof(invalid_utf8_sequences)/sizeof(char const*));
	const bool invalid_utf8 = g_seed <= num_sequences && g_seed > 0;

	if (invalid_utf8)
		str += invalid_utf8_sequences[g_seed-1];

	g_seed -= num_sequences;

	print_ascii_number(output, str.size());
	output += ':';
	output += str;
}

void print_terminate(std::string& output)
{
	const bool unterminated = g_seed == 1;
	g_seed -= 1;
	if (!unterminated) output += 'e';
}

void print_int(std::string& output, boost::int64_t value)
{
	const bool double_int = g_seed == 1;
	g_seed -= 1;
	if (double_int) output += 'i';
	output += 'i';
	print_ascii_number(output, value);
	print_terminate(output);
}

void print_dict(std::string& output)
{
	const bool double_dict = g_seed == 1;
	g_seed -= 1;
	if (double_dict) output += 'd';
	output += 'd';
}

void print_list(std::string& output)
{
	const bool double_list = g_seed == 1;
	g_seed -= 1;
	if (double_list) output += 'l';
	output += 'l';
}

void render_arbitrary_item(std::string& out)
{
	if (g_seed <= 0) return;

	std::string option;
	print_int(option, 1337);
	if (g_seed <= 0)
	{
		out += option;
		return;
	}

	option.clear();
	print_string(option, "abcdefgh");
	if (g_seed <= 0)
	{
		out += option;
		return;
	}

	option.clear();
	print_dict(option);
	print_string(option, "abcdefgh");
	print_int(option, 1337);
	print_terminate(option);
	if (g_seed <= 0)
	{
		out += option;
		return;
	}

	option.clear();
	print_list(option);
	print_string(option, "abcdefgh");
	print_terminate(option);
	if (g_seed <= 0)
	{
		out += option;
		return;
	}
}

void render_variant(std::string& out, bdecode_node const& e)
{
	switch (e.type())
	{
		case bdecode_node::dict_t:
			print_dict(out);
			for (int i = 0; i < e.dict_size(); ++i)
			{
				std::pair<std::string, bdecode_node> item = e.dict_at(i);
				const bool duplicate = g_seed == 1;
				const bool skipped = g_seed == 2;
				g_seed -= 2;
				if (duplicate)
				{
					print_string(out, item.first);
					render_variant(out, item.second);
				}
				if (!skipped)
				{
					print_string(out, item.first);
					render_variant(out, item.second);
				}

				render_arbitrary_item(out);
			}
			print_terminate(out);
			break;
		case bdecode_node::list_t:
			print_list(out);
			for (int i = 0; i < e.list_size(); ++i)
			{
				const bool duplicate = g_seed == 1;
				const bool skipped = g_seed == 2;
				g_seed -= 2;
				if (duplicate) render_variant(out, e.list_at(i));

				render_arbitrary_item(out);

				if (!skipped) render_variant(out, e.list_at(i));
			}
			print_terminate(out);
			break;
		case bdecode_node::int_t:
			print_int(out, e.int_value());
			break;
		case bdecode_node::string_t:
			print_string(out, e.string_value());
			break;
		default:
			abort();
	}
}

int load_file(std::string const& filename, std::vector<char>& v
	, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::system_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

int main(int argc, char const* argv[])
{
	std::vector<char> buf;
	libtorrent::error_code ec;

	if (argc < 2)
	{
		fprintf(stderr, "usage: fuzz_torrent torrent-file [torrent-file ...]\n");
		return 1;
	}

	--argc;
	++argv;
	for (;argc > 0; --argc, ++argv)
	{
		int ret = load_file(*argv, buf, ec);
		if (ret < 0)
		{
			fprintf(stderr, "ERROR loading file: %s\n%s\n"
				, *argv, ec.message().c_str());
			continue;
		}

		bdecode_node e;
		if (buf.size() == 0 || bdecode(&buf[0], &buf[0] + buf.size(), e, ec) != 0)
		{
			fprintf(stderr, "ERROR parsing file: %s\n%s\n"
				, *argv, ec.message().c_str());
			continue;
		}

		std::string test_buffer;
		int i = 0;
		for (i = 0; i < 10000000; ++i)
		{
			g_seed = i;
			test_buffer.clear();
			render_variant(test_buffer, e);

			libtorrent::error_code ec;
			libtorrent::torrent_info t(test_buffer.c_str(), test_buffer.size(), ec);

			// TODO: add option to save to file unconditionally (to test other clients)
			/*
				{
				fprintf(stderr, "saving %d\n", i);
				char filename[100];
				snprintf(filename, sizeof(filename), "torrents/fuzz-%d.torrent", i);
				FILE* f = fopen(filename, "wb+");
				if (f == 0)
				{
				fprintf(stderr, "ERROR saving file: (%d) %s\n", errno, strerror(errno));
				return 1;
				}
				fwrite(test_buffer.c_str(), test_buffer.size(), 1, f);
				fclose(f);
				}
			 */
			if (g_seed > 0) break;
		}
		fprintf(stderr, "tested %d variants of %s\n", i, *argv);
	}
	return 0;
}

