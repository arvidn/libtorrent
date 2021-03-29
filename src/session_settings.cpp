/*

Copyright (c) 2007, 2015, 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/settings_pack.hpp"

#include <functional>

namespace lt::aux {

	session_settings::session_settings() = default;

	session_settings::session_settings(settings_pack const& p)
	{
		apply_pack_impl(&p, m_store);
	}

	void session_settings::bulk_set(std::function<void(session_settings_single_thread&)> f)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		f(m_store);
	}

	void session_settings::bulk_get(std::function<void(session_settings_single_thread const&)> f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		f(m_store);
	}

	session_settings_single_thread::session_settings_single_thread()
	{
		initialize_default_settings(*this);
	}
}

