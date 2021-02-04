/*

Copyright (c) 2015, 2017, 2019-2020, Arvid Norberg
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

#ifndef TEST_UTILS_HPP
#define TEST_UTILS_HPP

#include <string>

#include "test.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/download_priority.hpp"

#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/path.hpp"
#include <vector>
#include <fstream>

namespace libtorrent
{
	EXPORT std::string time_now_string();
	EXPORT std::string time_to_string(lt::time_point const tp);
}

inline lt::download_priority_t operator "" _pri(unsigned long long const p)
{
	return lt::download_priority_t(static_cast<std::uint8_t>(p));
}

EXPORT lt::aux::vector<lt::sha256_hash> build_tree(int const size);

#ifdef _WIN32
int EXPORT truncate(char const* file, std::int64_t size);
#endif

struct EXPORT ofstream : std::ofstream
{
	ofstream(char const* filename);
};

EXPORT bool exists(std::string const& f);

#endif

