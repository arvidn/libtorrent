/*

Copyright (c) 2018, Arvid Norberg, Magnus Jonsson
Copyright (c) 2018, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SET_SOCKET_BUFFER_HPP
#define TORRENT_SET_SOCKET_BUFFER_HPP

#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/error_code.hpp"

namespace lt::aux {

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
}

#endif
