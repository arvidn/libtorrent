/*

Copyright (c) 2006, Arvid Norberg & Daniel Wallin
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

#ifndef TORRENT_LOGGING_HPP
#define TORRENT_LOGGING_HPP

#include "libtorrent/config.hpp"

#if TORRENT_USE_IOSTREAM

#include <iostream>
#include <fstream>
#include "libtorrent/time.hpp"

namespace libtorrent { namespace dht
{

class log
{
public:
	log(char const* id, std::ostream& stream)
		: m_id(id)
		, m_enabled(true)
		, m_stream(stream)
	{
	}

	char const* id() const
	{
		return m_id;
	}

	bool enabled() const
	{
		return m_enabled;
	}

	void enable(bool e)
	{
		m_enabled = e;
	}
	
	void flush() { m_stream.flush(); }

	template<class T>
	log& operator<<(T const& x)
	{
		m_stream << x;
		return *this;
	}

private:
	char const* m_id;
	bool m_enabled;
	std::ostream& m_stream;
};

class log_event
{
public:
	log_event(log& log) 
		: log_(log) 
	{
		if (log_.enabled())
			log_ << time_now_string() << " [" << log.id() << "] ";
	}

	~log_event()
	{
		if (log_.enabled())
		{
			log_ << "\n";
			log_.flush();
		}
	}

	template<class T>
	log_event& operator<<(T const& x)
	{
		log_ << x;
		return *this;
	}

	operator bool() const
	{
		return log_.enabled();
	}

private:	
	log& log_;
};

class inverted_log_event : public log_event
{
public:
	inverted_log_event(log& log) : log_event(log) {}

	operator bool() const
	{
		return !log_event::operator bool();
	}
};

} } // namespace libtorrent::dht

#define TORRENT_DECLARE_LOG(name) \
	libtorrent::dht::log& name ## _log()

#define TORRENT_DEFINE_LOG(name) \
	libtorrent::dht::log& name ## _log() \
	{ \
		static std::ofstream log_file("dht.log", std::ios::app); \
		static libtorrent::dht::log instance(#name, log_file); \
		return instance; \
	}

#define TORRENT_LOG(name) \
	if (libtorrent::dht::inverted_log_event event_object__ = name ## _log()); \
	else static_cast<log_event&>(event_object__)

#endif // TORRENT_USE_IOSTREAM

#endif

