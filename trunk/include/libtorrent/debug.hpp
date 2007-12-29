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
#include <iostream>

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
	
	namespace fs = boost::filesystem;

	struct logger
	{
		logger(fs::path const& logpath, fs::path const& filename, int instance, bool append = true)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				fs::path dir(fs::complete(logpath / ("libtorrent_logs" + boost::lexical_cast<std::string>(instance))));
				if (!fs::exists(dir)) fs::create_directories(dir);
				m_file.open((dir / filename).string().c_str(), std::ios_base::out | (append ? std::ios_base::app : std::ios_base::out));
				*this << "\n\n\n*** starting log ***\n";
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
				std::cerr << "failed to create log '" << filename.string() << "': " << e.what() << std::endl;
			}
#endif
		}

		template <class T>
		logger& operator<<(T const& v)
		{
			m_file << v;
			m_file.flush();
			return *this;
		}

		std::ofstream m_file;
	};

}

#endif // TORRENT_DEBUG_HPP_INCLUDED

