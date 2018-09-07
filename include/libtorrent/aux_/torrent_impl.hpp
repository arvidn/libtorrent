/*

Copyright (c) 2003-2017, Arvid Norberg
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

#ifndef TORRENT_TORRENT_IMPL_HPP_INCLUDED
#define TORRENT_TORRENT_IMPL_HPP_INCLUDED

// this is not a normal header, it's just this template, to be able to call this
// function from more than one translation unit. But it's still internal

namespace libtorrent {

	template <typename Fun, typename... Args>
	void torrent::wrap(Fun f, Args&&... a)
#ifndef BOOST_NO_EXCEPTIONS
		try
#endif
	{
		(this->*f)(std::forward<Args>(a)...);
	}
#ifndef BOOST_NO_EXCEPTIONS
	catch (system_error const& e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("EXCEPTION: (%d %s) %s"
			, e.code().value()
			, e.code().message().c_str()
			, e.what());
#endif
		alerts().emplace_alert<torrent_error_alert>(get_handle()
			, e.code(), e.what());
		pause();
	} catch (std::exception const& e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("EXCEPTION: %s", e.what());
#endif
		alerts().emplace_alert<torrent_error_alert>(get_handle()
			, error_code(), e.what());
		pause();
	} catch (...) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("EXCEPTION: unknown");
#endif
		alerts().emplace_alert<torrent_error_alert>(get_handle()
			, error_code(), "unknown error");
		pause();
	}
#endif

}

#endif

