/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/settings_pack.hpp"

#include "transfer_sim.hpp"

using lt::settings_pack;

void no_init(lt::session& ses0, lt::session& ses1) {}

record_finished_pieces::record_finished_pieces(std::set<lt::piece_index_t>& p)
	: m_passed(&p)
{}

void record_finished_pieces::operator()(lt::session&, lt::alert const* a) const
{
	if (auto const* pf = lt::alert_cast<lt::piece_finished_alert>(a))
		m_passed->insert(pf->piece_index);
}

expect_seed::expect_seed(bool e) : m_expect(e) {}
void expect_seed::operator()(std::shared_ptr<lt::session> ses[2]) const
{
	TEST_EQUAL(is_seed(*ses[0]), m_expect);
}

int blocks_per_piece(test_transfer_flags_t const flags)
{
	if (flags & tx::small_pieces) return 1;
	if (flags & tx::large_pieces) return 4;
	return 2;
}

int num_pieces(test_transfer_flags_t const flags)
{
	if (flags & tx::multiple_files)
	{
		// since v1 torrents don't pad files by default, there will be fewer
		// pieces on those torrents
		if (flags & tx::v1_only)
			return 31;
		else
			return 33;
	}
	return 11;
}
