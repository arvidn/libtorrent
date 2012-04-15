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

#include "libtorrent/config.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp"

namespace {

	template <class T>
	void insort_replace(std::vector<std::pair<int, T> >& c, std::pair<int, T> const& v)
	{
		typedef std::vector<std::pair<int, T> > container_t;
		typename container_t::iterator i = std::lower_bound(c.begin(), c.end(), v);
		if (i != c.end() && i->first == v.first) i->second = v.second;
		else c.insert(i, v);
	}
}

namespace libtorrent
{
	void settings_pack::set_str(int name, std::string val)
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return;
		std::pair<int, std::string> v(name, val);
		insort_replace(m_strings, v);
	}

	void settings_pack::set_int(int name, int val)
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return;
		std::pair<int, int> v(name, val);
		insort_replace(m_ints, v);
	}

	void settings_pack::set_float(int name, float val)
	{
		TORRENT_ASSERT((name & type_mask) == float_type_base);
		if ((name & type_mask) != float_type_base) return;
		std::pair<int, float> v(name, val);
		insort_replace(m_floats, v);
	}

	void settings_pack::set_bool(int name, bool val)
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return;
		std::pair<int, bool> v(name, val);
		insort_replace(m_bools, v);
	}

}

