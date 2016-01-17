/*

Copyright (c) 2003-2016, Arvid Norberg
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

#if defined TORRENT_ASIO_DEBUGGING

#include "libtorrent/assert.hpp"
#include "libtorrent/thread.hpp"

#include <map>
#include <cstring>

std::string demangle(char const* name);

namespace libtorrent
{
	struct async_t
	{
		async_t() : refs(0) {}
		std::string stack;
		int refs;
	};

	extern std::map<std::string, async_t> _async_ops;
	extern int _async_ops_nthreads;
	extern mutex _async_ops_mutex;

	inline void add_outstanding_async(char const* name)
	{
		mutex::scoped_lock l(_async_ops_mutex);
		async_t& a = _async_ops[name];
		if (a.stack.empty())
		{
			char stack_text[10000];
			print_backtrace(stack_text, sizeof(stack_text), 9);
			a.stack = stack_text;
		}
		++a.refs;
	}

	inline void complete_async(char const* name)
	{
		mutex::scoped_lock l(_async_ops_mutex);
		async_t& a = _async_ops[name];
		TORRENT_ASSERT(a.refs > 0);
		--a.refs;
	}

	inline void async_inc_threads()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		++_async_ops_nthreads;
	}

	inline void async_dec_threads()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		--_async_ops_nthreads;
	}

	inline int log_async()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		int ret = 0;
		for (std::map<std::string, async_t>::iterator i = _async_ops.begin()
			, end(_async_ops.end()); i != end; ++i)
		{
			if (i->second.refs <= _async_ops_nthreads - 1) continue;
			ret += i->second.refs;
			printf("%s: (%d)\n%s\n", i->first.c_str(), i->second.refs, i->second.stack.c_str());
		}
		return ret;
	}
}

#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING

#include <cstring>
#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/thread.hpp"

#if TORRENT_USE_IOSTREAM
#include <string>
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

		~logger()
		{
			mutex::scoped_lock l(file_mutex);
			log_file.close();
			open_filename.clear();
		}

		logger(std::string const& logpath, std::string const& filename
			, int instance, bool append)
		{
			char log_name[512];
			snprintf(log_name, sizeof(log_name), "libtorrent_logs%d", instance);
			std::string dir(complete(combine_path(combine_path(logpath, log_name), filename)) + ".log");
			error_code ec;
			if (!exists(parent_path(dir)))
				create_directories(parent_path(dir), ec);
			m_filename = dir;

			mutex::scoped_lock l(file_mutex);
			open(!append);
			log_file << "\n\n\n*** starting log ***\n";
		}

		void move_log_file(std::string const& logpath, std::string const& new_name, int instance)
		{
			mutex::scoped_lock l(file_mutex);
			if (open_filename == m_filename)
			{
				log_file.close();
				open_filename.clear();
			}

			char log_name[512];
			snprintf(log_name, sizeof(log_name), "libtorrent_logs%d", instance);
			std::string dir(combine_path(combine_path(complete(logpath), log_name), new_name) + ".log");

			error_code ec;
			create_directories(parent_path(dir), ec);

			if (ec)
				fprintf(stderr, "Failed to create logfile directory %s: %s\n"
					, parent_path(dir).c_str(), ec.message().c_str());
			ec.clear();
			rename(m_filename, dir, ec);
			if (ec)
				fprintf(stderr, "Failed to move logfile %s: %s\n"
					, parent_path(dir).c_str(), ec.message().c_str());

			m_filename = dir;
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

