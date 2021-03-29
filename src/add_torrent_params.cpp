/*

Copyright (c) 2016, 2018-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/add_torrent_params.hpp"

namespace lt {

	add_torrent_params::add_torrent_params() = default;
	add_torrent_params::~add_torrent_params() = default;
	add_torrent_params::add_torrent_params(add_torrent_params&&) noexcept = default;
	add_torrent_params& add_torrent_params::operator=(add_torrent_params&&) & noexcept = default;
	add_torrent_params::add_torrent_params(add_torrent_params const&) = default;
	add_torrent_params& add_torrent_params::operator=(add_torrent_params const&) & = default;

	static_assert(std::is_nothrow_move_constructible<add_torrent_params>::value
		, "should be nothrow move constructible");

	static_assert(std::is_nothrow_move_constructible<std::string>::value
		, "should be nothrow move constructible");

	static_assert(std::is_nothrow_move_assignable<add_torrent_params>::value
		, "should be nothrow move assignable");

	// TODO: it would be nice if this was nothrow default constructible
//	static_assert(std::is_nothrow_default_constructible<add_torrent_params>::value
//		, "should be nothrow default constructible");

namespace aux {

	// returns whether this add_torrent_params object has "resume-data", i.e.
	// information about which pieces we have.
	bool contains_resume_data(add_torrent_params const& atp)
	{
		return !atp.have_pieces.empty()
			|| (atp.flags & torrent_flags::seed_mode);
	}
}

}
