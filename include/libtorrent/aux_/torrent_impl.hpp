/*

Copyright (c) 2017, 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TORRENT_IMPL_HPP_INCLUDED
#define TORRENT_TORRENT_IMPL_HPP_INCLUDED

// this is not a normal header, it's just this template, to be able to call this
// function from more than one translation unit. But it's still internal

namespace lt::aux {

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
