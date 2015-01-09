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

#ifndef TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED

#include "libtorrent/version.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/assert.hpp"

#include <string>

namespace libtorrent
{
	TORRENT_EXTRA_EXPORT void initialize_default_settings(aux::session_settings& s);
}

namespace libtorrent { namespace aux
{

#define SET(type) \
	TORRENT_ASSERT((name & settings_pack::type_mask) == settings_pack:: type ## _type_base); \
	if ((name & settings_pack::type_mask) != settings_pack:: type ## _type_base) return; \
	m_ ## type ## s[name - settings_pack:: type ## _type_base] = value

#define GET(type, default_val) \
	TORRENT_ASSERT((name & settings_pack::type_mask) == settings_pack:: type ## _type_base); \
	if ((name & settings_pack::type_mask) != settings_pack:: type ## _type_base) return default_val; \
	return m_ ## type ## s[name - settings_pack:: type ## _type_base]

	struct TORRENT_EXTRA_EXPORT session_settings
	{
		friend void libtorrent::save_settings_to_dict(
			aux::session_settings const& s, entry::dictionary_type& sett);

		void set_str(int name, std::string const& value) { SET(string); }
		std::string const& get_str(int name) const { GET(string, m_strings[0]); }
		void set_int(int name, int value) { SET(int); }
		int get_int(int name) const { GET(int, 0); }
		void set_bool(int name, bool value) { SET(bool); }
		bool get_bool(int name) const { GET(bool, false); }

		session_settings();

	private:
		std::string m_strings[settings_pack::num_string_settings];
		int m_ints[settings_pack::num_int_settings];
		// TODO: make this a bitfield
		bool m_bools[settings_pack::num_bool_settings];
	};

#undef GET
#undef SET

} }

#endif

