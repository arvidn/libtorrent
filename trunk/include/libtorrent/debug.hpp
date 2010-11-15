/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_DEBUG_HPP_INCLUDED
#define TORRENT_DEBUG_HPP_INCLUDED

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING

#include <string>
#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/thread.hpp"

#if TORRENT_USE_IOSTREAM
#include <fstream>
#include <iostream>
#endif

namespace libtorrent
{
	// DEBUG API
	
	struct logger
	{
#if TORRENT_USE_IOSTREAM
		// all log streams share a single file descriptor
		// and re-opens the file for each log line
		// these members are defined in session_impl.cpp
		static std::ofstream log_file;
		static std::string open_filename;
		static mutex file_mutex;
#endif

		logger(std::string const& logpath, std::string const& filename
			, int instance, bool append)
		{
			char log_name[512];
			snprintf(log_name, sizeof(log_name), "libtorrent_logs%d", instance);
			std::string dir(complete(combine_path(logpath, log_name)));
			error_code ec;
			if (!exists(dir)) create_directories(dir, ec);
			m_filename = combine_path(dir, filename);

			mutex::scoped_lock l(file_mutex);
			open(!append);
			log_file << "\n\n\n*** starting log ***\n";
		}

#if TORRENT_USE_IOSTREAM
		void open(bool truncate)
		{
			if (open_filename == m_filename) return;
			log_file.close();
			log_file.clear();
			log_file.open(m_filename.c_str(), truncate ? std::ios_base::trunc : std::ios_base::app);
			open_filename = m_filename;
			if (!log_file.good())
				fprintf(stderr, "Failed to open logfile %s: %s\n", m_filename.c_str(), strerror(errno));
		}
#endif

		template <class T>
		logger& operator<<(T const& v)
		{
#if TORRENT_USE_IOSTREAM
			mutex::scoped_lock l(file_mutex);
			open(false);
			log_file << v;
#endif
			return *this;
		}

		std::string m_filename;
	};

}

#endif // TORRENT_VERBOSE_LOGGING || TORRENT_LOGGING || TORRENT_ERROR_LOGGING
#endif // TORRENT_DEBUG_HPP_INCLUDED

