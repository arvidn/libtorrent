/*

Copyright (c) 2011-2018, Arvid Norberg
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

#ifndef TORRENT_NETWORK_THREAD_POOL_HPP_INCLUDED
#define TORRENT_NETWORK_THREAD_POOL_HPP_INCLUDED

#include "libtorrent/thread_pool.hpp"
#include <boost/shared_ptr.hpp>
#include <vector>

namespace libtorrent
{

	class peer_connection;
	class buffer;

	struct socket_job
	{
		socket_job() : type(none), vec(NULL), recv_buf(NULL), buf_size(0) {}
#if __cplusplus >= 201103L
		socket_job(socket_job const&) = default;
		socket_job& operator=(socket_job const&) = default;
#endif

		enum job_type_t
		{
			read_job = 0,
			write_job,
			none
		};

		job_type_t type;

		// used for write jobs
		std::vector<boost::asio::const_buffer> const* vec;
		// used for read jobs
		char* recv_buf;
		int buf_size;
		boost::array<boost::asio::mutable_buffer, 2> read_vec;

		boost::shared_ptr<peer_connection> peer;
		// defined in session_impl.cpp
		~socket_job();
	};

	// defined in session_impl.cpp
	struct network_thread_pool : thread_pool<socket_job>
	{
		void process_job(socket_job const& j, bool post);
	};
}

#endif // TORRENT_NETWORK_THREAD_POOL_HPP_INCLUDED

