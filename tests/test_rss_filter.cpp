/*

Copyright (c) 2013, Arvid Norberg
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

#include "rencode.hpp"

#include "test.hpp"
#include "rss_filter.hpp"

using namespace libtorrent;

int main_ret = 0;

int main(int argc, char* argv[])
{
	item_properties p;

	parse_name("Foo Bar 3x7 [HDTV - 2HD]", p);
	printf("s: %d e: %d\n", p.season, p.episode);
	TEST_CHECK(p.season == 3);
	TEST_CHECK(p.episode == 7);
	TEST_CHECK(p.quality == item_properties::hd720);
	TEST_CHECK(p.source == item_properties::tv);

	parse_name("Foo.Bar.S10E23.HDTV", p);
	printf("s: %d e: %d\n", p.season, p.episode);
	TEST_CHECK(p.season == 10);
	TEST_CHECK(p.episode == 23);
	TEST_CHECK(p.quality == item_properties::hd720);
	TEST_CHECK(p.source == item_properties::tv);

	parse_name("Foo_Bar_2013-05-13_[brrip.1080p]", p);
	printf("s: %d e: %d\n", p.season, p.episode);
	TEST_CHECK(p.season == 2013);
	TEST_CHECK(p.episode == 513);
	TEST_CHECK(p.quality == item_properties::hd1080);
	TEST_CHECK(p.source == item_properties::bluray);

	parse_name("Foo_Bar 2013 05 13", p);
	printf("s: %d e: %d\n", p.season, p.episode);
	TEST_CHECK(p.season == 2013);
	TEST_CHECK(p.episode == 513);

	parse_name("Foo_Bar 2013.05.13", p);
	printf("s: %d e: %d\n", p.season, p.episode);
	TEST_CHECK(p.season == 2013);
	TEST_CHECK(p.episode == 513);

	TEST_CHECK(normalize_title("Foo.. Bar.>< [hdtv] __ test") == "foo bar hdtv test");
	TEST_CHECK(normalize_title("Foo_Bar_2013-05-13_[brrip.1080p]") == "foo bar 2013-05-13 brrip 1080p");

	return main_ret;
}
