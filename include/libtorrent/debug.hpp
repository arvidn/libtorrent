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

#include <string>
#include <fstream>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{

	// DEBUG API

	struct logger
	{
		logger& operator<<(const char* t)
		{ assert(t); log(t); return *this; }
		logger& operator<<(const std::string& t)
		{ log(t.c_str()); return *this; }
		logger& operator<<(int i)
		{
			log(boost::lexical_cast<std::string>(i).c_str());
			return *this; 
		}
		logger& operator<<(unsigned int i)
		{
			log(boost::lexical_cast<std::string>(i).c_str());
			return *this; 
		}
		logger& operator<<(float i)
		{
			log(boost::lexical_cast<std::string>(i).c_str());
			return *this; 
		}

		logger& operator<<(char i)
		{
			char c[2];
			c[0] = i;
			c[1] = 0;
			log(c);
			return *this; 
		}

		virtual void log(const char*) = 0;
		virtual ~logger() {}
	};

	struct null_logger: libtorrent::logger
	{
	public:
		virtual void log(const char* text) {}
	};

	struct cout_logger: libtorrent::logger
	{
	public:
		virtual void log(const char* text) { std::cout << text; }
	};

	struct file_logger: libtorrent::logger
	{
	public:
		file_logger(boost::filesystem::path const& filename)
			: m_file(boost::filesystem::complete("libtorrent_logs" / filename))
		{}
		virtual void log(const char* text) { assert(text); m_file << text; }

		boost::filesystem::ofstream m_file;
	};

}

#endif // TORRENT_DEBUG_HPP_INCLUDED
