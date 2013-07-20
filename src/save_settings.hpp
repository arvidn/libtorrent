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

#ifndef TORRENT_SAVE_SETTINGS_HPP
#define TORRENT_SAVE_SETTINGS_HPP

#include "libtorrent/session.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/error_code.hpp"

#include <string>
#include <map>

namespace libtorrent
{
	struct save_settings_interface
	{
		virtual void save(error_code& ec) const = 0;
		virtual void load(error_code& ec) = 0;
		virtual void set_int(char const* key, int val) = 0;
		virtual void set_str(char const* key, std::string val) = 0;
		virtual int get_int(char const* key, int def = 0) const = 0;
		virtual std::string get_str(char const* key, char const* def = "") const = 0;
	};

	struct save_settings : save_settings_interface
	{
		save_settings(session& s, std::string const& settings_file);
		~save_settings();

		void save(error_code& ec) const;
		void load(error_code& ec);

		void set_int(char const* key, int val);
		void set_str(char const* key, std::string val);

		int get_int(char const* key, int def) const;
		std::string get_str(char const* key, char const* def = "") const;

	private:

		void load_impl(std::string filename, error_code& ec);

		session& m_ses;
		std::string m_settings_file;
		std::map<std::string, int> m_ints;
		std::map<std::string, std::string> m_strings;
	};

	int load_file(std::string const& filename, std::vector<char>& v, error_code& ec, int limit = 8000000);
	int save_file(std::string const& filename, std::vector<char>& v, error_code& ec);

}

#endif

