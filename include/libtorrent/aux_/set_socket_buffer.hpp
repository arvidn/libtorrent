/*

Copyright (c) 2018, Arvid Norberg, Magnus Jonsson
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

#ifndef TORRENT_SET_SOCKET_BUFFER_HPP
#define TORRENT_SET_SOCKET_BUFFER_HPP

#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent {
namespace aux {

	template <class Socket>
	void set_socket_buffer_size(Socket& s, session_settings const& sett, error_code& ec)
	{
#ifdef TCP_NOTSENT_LOWAT
		int const not_sent_low_watermark = sett.get_int(settings_pack::send_not_sent_low_watermark);
		if (not_sent_low_watermark)
		{
			error_code ignore;
			s.set_option(tcp_notsent_lowat(not_sent_low_watermark), ignore);
		}
#endif
		int const snd_size = sett.get_int(settings_pack::send_socket_buffer_size);
		if (snd_size)
		{
			typename Socket::send_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != snd_size)
			{
				typename Socket::send_buffer_size option(snd_size);
				s.set_option(option, ec);
				if (ec)
				{
					// restore previous value
					s.set_option(prev_option, ec);
					return;
				}
			}
		}
		int const recv_size = sett.get_int(settings_pack::recv_socket_buffer_size);
		if (recv_size)
		{
			typename Socket::receive_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != recv_size)
			{
				typename Socket::receive_buffer_size option(recv_size);
				s.set_option(option, ec);
				if (ec)
				{
					// restore previous value
					s.set_option(prev_option, ec);
					return;
				}
			}
		}
	}

}}

#endif
