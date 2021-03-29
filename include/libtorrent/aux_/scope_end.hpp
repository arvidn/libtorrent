/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SCOPE_END_HPP_INCLUDED
#define TORRENT_SCOPE_END_HPP_INCLUDED

#include <utility>

namespace lt::aux {

	template <typename Fun>
	struct scope_end_impl
	{
		explicit scope_end_impl(Fun f) : m_fun(std::move(f)) {}
		~scope_end_impl() { if (m_armed) m_fun(); }

		// movable
		scope_end_impl(scope_end_impl&&) noexcept = default;
		scope_end_impl& operator=(scope_end_impl&&) & noexcept = default;

		// non-copyable
		scope_end_impl(scope_end_impl const&) = delete;
		scope_end_impl& operator=(scope_end_impl const&) = delete;
		void disarm() { m_armed = false; }
	private:
		Fun m_fun;
		bool m_armed = true;
	};

	template <typename Fun>
	scope_end_impl<Fun> scope_end(Fun f) { return scope_end_impl<Fun>(std::move(f)); }
}

#endif

