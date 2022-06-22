/*

Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2016, Steven Siloti
Copyright (c) 2017, 2019-2020, Arvid Norberg
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

#ifndef TORRENT_IP_NOTIFIER_HPP_INCLUDED
#define TORRENT_IP_NOTIFIER_HPP_INCLUDED

#include <functional>
#include <memory>

#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"

namespace libtorrent { namespace aux {

	struct TORRENT_EXTRA_EXPORT ip_change_notifier
	{
		// cb will be invoked  when a change is detected in the
		// system's IP addresses
		virtual void async_wait(std::function<void(error_code const&)> cb) = 0;
		virtual void cancel() = 0;

		virtual ~ip_change_notifier() {}
	};

	TORRENT_EXTRA_EXPORT std::unique_ptr<ip_change_notifier> create_ip_notifier(io_context& ios);
}}

#endif
