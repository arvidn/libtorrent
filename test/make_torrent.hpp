/*

Copyright (c) 2016, Arvid Norberg
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

#include "libtorrent/torrent_info.hpp"
#include <boost/shared_ptr.hpp>
#include <vector>
#include <string>

enum flags_t
{
	private_torrent = 1
};

struct torrent_args
{
	torrent_args() : m_priv(false) {}
	torrent_args& name(char const* n) { m_name = n; return *this; }
	torrent_args& file(char const* f) { m_files.push_back(f); return *this; }
	torrent_args& url_seed(char const* u) { m_url_seed = u; return *this; }
	torrent_args& http_seed(char const* u) { m_http_seed = u; return *this; }
	torrent_args& priv() { m_priv = true; return *this; }

	bool m_priv;
	std::string m_name;
	std::vector<std::string> m_files;
	std::string m_url_seed;
	std::string m_http_seed;
};

boost::shared_ptr<libtorrent::torrent_info> make_test_torrent(torrent_args const& args);

void generate_files(libtorrent::torrent_info const& ti, std::string const& path, bool random = false);

