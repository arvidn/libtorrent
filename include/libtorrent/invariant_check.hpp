// Copyright Daniel Wallin 2004. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef TORRENT_INVARIANT_ACCESS_HPP_INCLUDED
#define TORRENT_INVARIANT_ACCESS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/config.hpp"

#if TORRENT_USE_INVARIANT_CHECKS

namespace libtorrent
{

	class invariant_access
	{
	public:
		template<class T>
		static void check_invariant(T const& self)
		{
			self.check_invariant();
		}
	};

	template<class T>
	void check_invariant(T const& x)
	{
		invariant_access::check_invariant(x);
	}

	struct invariant_checker {};

	template<class T>
	struct invariant_checker_impl : invariant_checker
	{
		invariant_checker_impl(T const& self_)
			: self(self_)
		{
			TORRENT_TRY
			{
				check_invariant(self);
			}
			TORRENT_CATCH_ALL
			{
				TORRENT_ASSERT(false);
			}
		}

		invariant_checker_impl(invariant_checker_impl const& rhs)
			: self(rhs.self) {}

		~invariant_checker_impl()
		{
			TORRENT_TRY
			{
				check_invariant(self);
			}
			TORRENT_CATCH_ALL
			{
				TORRENT_ASSERT(false);
			}
		}

		T const& self;

	private:
		invariant_checker_impl& operator=(invariant_checker_impl const&);
	};

	template<class T>
	invariant_checker_impl<T> make_invariant_checker(T const& x)
	{
		return invariant_checker_impl<T>(x);
	}
}

#define INVARIANT_CHECK \
	invariant_checker const& _invariant_check = make_invariant_checker(*this); \
	(void)_invariant_check
#else
#define INVARIANT_CHECK do {} TORRENT_WHILE_0
#endif

#endif // TORRENT_INVARIANT_ACCESS_HPP_INCLUDED
