/*

Copyright (c) 2026, ghz
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/read_ahead.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/assert.hpp"

#include <algorithm>

namespace libtorrent::aux {

namespace {

// the most windows a piece is split into: one bit each in a std::uint64_t
constexpr int max_windows = 64;

// a run of contiguous reads this long, or the whole piece, is read ahead of
constexpr int min_run = 3 * default_block_size;

std::uint64_t piece_key(storage_index_t const st, piece_index_t const piece)
{
	return (std::uint64_t(static_cast<std::uint32_t>(st)) << 32)
		| std::uint32_t(static_cast<int>(piece));
}

std::uint64_t window_bit(int const window)
{
	TORRENT_ASSERT(window >= 0 && window < max_windows);
	return std::uint64_t(1) << window;
}

// the windows [first, last]
std::uint64_t window_bits(int const first, int const last)
{
	TORRENT_ASSERT(0 <= first && first <= last && last < max_windows);
	std::uint64_t const to_last =
		last == max_windows - 1 ? ~std::uint64_t(0) : window_bit(last + 1) - 1;
	return to_last & ~(window_bit(first) - 1);
}
}

read_ahead::read_ahead(int const ahead, steady_clock::duration const ttl, int const max_pieces)
	: m_ahead(std::clamp(ahead, 0, max_windows))
	, m_ttl(ttl)
	, m_max_pieces(std::size_t(std::max(max_pieces, 1)))
{
	TORRENT_ASSERT(ahead >= 0);
	TORRENT_ASSERT(max_pieces > 0);
}

void read_ahead::on_request(storage_index_t const st,
	piece_index_t const piece,
	int const offset,
	steady_clock::time_point const now)
{
	sweep(now);

	piece_state& p = m_pieces[piece_key(st, piece)];
	// requested recently: its place stays
	if (p.order != 0 && now - p.requested < m_ttl)
		return;
	std::uint32_t const s = static_cast<std::uint32_t>(st);
	erase_request(s, p);
	p.order = ++m_last_order;
	p.requested_offset = std::max(offset, 0);
	p.requested = now;
	m_requested.emplace(std::make_pair(s, p.order), piece);
	trim(now);
}

read_ahead::result read_ahead::on_read(storage_index_t const st,
	piece_index_t const piece,
	int const piece_size,
	int const offset,
	int const length,
	steady_clock::time_point const now)
{
	result ret;
	if (piece_size <= 0 || offset < 0 || offset >= piece_size || length <= 0)
		return ret;

	sweep(now);

	auto const it = m_pieces.find(piece_key(st, piece));
	if (it == m_pieces.end() || it->second.window_size < 0)
	{
		ret.what = action::lookup;
		ret.piece = piece;
		return ret;
	}
	piece_state& p = it->second;
	// not read for a while: what was hinted may have been evicted
	if (now - p.used >= m_ttl)
	{
		p.hinted = 0;
		p.num_runs = 0;
		p.next_run = 0;
	}
	p.used = now;
	if (p.window_size == 0)
		return ret;

	std::int64_t const end = std::min(std::int64_t(piece_size), std::int64_t(offset) + length);
	// a few scattered blocks: no sign the piece is read through
	if (!add_run(p, offset, int(end)))
		return ret;

	std::int64_t const size = p.window_size;
	int const num_windows = int((piece_size + size - 1) / size);
	int const first = int(offset / size);
	int const last = int((end - 1) / size);
	TORRENT_ASSERT(num_windows <= max_windows);

	int const ahead_to = std::min(num_windows - 1, last + m_ahead);

	// the windows in reach past the end of the piece are those of the
	// pieces requested after it: from the window of a piece's first request,
	// at most `budget` of them
	std::uint32_t const s = static_cast<std::uint32_t>(st);
	auto const next = p.order != 0 && last + m_ahead > ahead_to
		? m_requested.upper_bound(std::make_pair(s, p.order))
		: m_requested.end();
	auto const in_reach = [](piece_state const& q, int const budget) {
		std::int64_t const q_size = q.window_size;
		int const q_windows = int((q.piece_size + q_size - 1) / q_size);
		int const q_first = int(std::min(q.requested_offset, q.piece_size - 1) / q_size);
		return std::make_pair(q_first, std::min(q_windows - 1, q_first + budget - 1));
	};
	// they must all be known first
	int budget = last + m_ahead - ahead_to;
	for (auto i = next; budget > 0 && i != m_requested.end() && i->first.first == s; ++i)
	{
		auto const q = m_pieces.find(piece_key(st, i->second));
		TORRENT_ASSERT(q != m_pieces.end());
		if (q->second.window_size < 0)
		{
			ret.what = action::lookup;
			ret.piece = i->second;
			return ret;
		}
		// a piece that isn't hinted ends it
		if (q->second.window_size == 0)
			break;
		auto const [q_first, q_last] = in_reach(q->second, budget);
		budget -= q_last - q_first + 1;
	}

	std::uint64_t const want = window_bits(first, ahead_to) & ~p.hinted;
	p.hinted |= want;
	for (int w = first; w <= ahead_to; ++w)
	{
		if (!(want & window_bit(w)))
			continue;
		// from the start of the window: reads of it that came before this
		// one may not have been submitted yet
		int const start = int(w * size);
		int const len = int(std::min(size, std::int64_t(piece_size) - start));
		ret.hints.push_back({piece, start, len});
	}
	// nothing to hint but what this read reads anyway
	if (!ret.hints.empty() && ret.hints.front().offset >= offset
		&& ret.hints.back().offset + ret.hints.back().length <= end)
	{
		ret.hints.clear();
	}

	budget = last + m_ahead - ahead_to;
	for (auto i = next; budget > 0 && i != m_requested.end() && i->first.first == s; ++i)
	{
		piece_state& q = m_pieces.find(piece_key(st, i->second))->second;
		if (q.window_size == 0)
			break;
		auto const [q_first, q_last] = in_reach(q, budget);
		budget -= q_last - q_first + 1;
		if (now - q.used >= m_ttl)
			q.hinted = 0;
		std::uint64_t const q_want = window_bits(q_first, q_last) & ~q.hinted;
		if (q_want == 0)
			continue;
		q.hinted |= q_want;
		q.used = now;
		std::int64_t const q_size = q.window_size;
		for (int w = q_first; w <= q_last; ++w)
		{
			if (!(q_want & window_bit(w)))
				continue;
			int const start = int(w * q_size);
			int const len = int(std::min(q_size, std::int64_t(q.piece_size) - start));
			ret.hints.push_back({i->second, start, len});
		}
	}

	if (!ret.hints.empty())
		ret.what = action::hint;
	return ret;
}

void read_ahead::add_piece(storage_index_t const st,
	piece_index_t const piece,
	int const piece_size,
	std::int64_t const limit,
	steady_clock::time_point const now)
{
	piece_state& p = m_pieces[piece_key(st, piece)];
	if (p.window_size >= 0)
		return;
	p.used = now;
	p.window_size = 0;
	trim(now);
	if (limit < 0 || piece_size <= 0)
		return;
	p.piece_size = piece_size;

	std::int64_t size = limit > 0 ? limit : default_window;
	size = std::max(size, (std::int64_t(piece_size) + max_windows - 1) / max_windows);
	// whole blocks, as peers request them
	size = (size + default_block_size - 1) / default_block_size * default_block_size;
	p.window_size = int(std::min(size, std::int64_t(piece_size)));
}

void read_ahead::remove_storage(storage_index_t const st)
{
	std::uint32_t const s = static_cast<std::uint32_t>(st);
	m_requested.erase(m_requested.lower_bound(std::make_pair(s, std::uint64_t(0))),
		m_requested.upper_bound(std::make_pair(s, ~std::uint64_t(0))));
	for (auto i = m_pieces.begin(); i != m_pieces.end();)
	{
		if (i->first >> 32 == s)
			i = m_pieces.erase(i);
		else
			++i;
	}
}

bool read_ahead::add_run(piece_state& p, int const start, int const end)
{
	int const enough = std::min(min_run, p.piece_size);
	for (int i = 0; i < p.num_runs; ++i)
	{
		auto& r = p.runs[std::size_t(i)];
		// neither next to it nor in it
		if (start > r.second || end < r.first)
			continue;
		r = {std::min(r.first, start), std::max(r.second, end)};
		return r.second - r.first >= enough;
	}
	p.runs[p.next_run] = {start, end};
	p.next_run = std::uint8_t((p.next_run + 1) % max_runs);
	p.num_runs = std::uint8_t(std::min(p.num_runs + 1, max_runs));
	return end - start >= enough;
}

void read_ahead::sweep(steady_clock::time_point const now)
{
	if (now - m_swept < m_ttl)
		return;
	m_swept = now;
	for (auto i = m_pieces.begin(); i != m_pieces.end();)
	{
		piece_state& p = i->second;
		if (now - p.used >= m_ttl && now - p.requested >= m_ttl)
		{
			erase_request(std::uint32_t(i->first >> 32), p);
			i = m_pieces.erase(i);
		}
		else
		{
			++i;
		}
	}
}

void read_ahead::trim(steady_clock::time_point const now)
{
	if (m_pieces.size() <= m_max_pieces)
		return;
	// the pieces not used in this call, by their last read, hint or request:
	// the oldest go, down to three quarters of max_pieces
	std::vector<std::pair<steady_clock::time_point, std::uint64_t>> by_use;
	by_use.reserve(m_pieces.size());
	for (auto const& [key, p] : m_pieces)
	{
		steady_clock::time_point const last = std::max(p.used, p.requested);
		if (last < now)
			by_use.emplace_back(last, key);
	}
	std::size_t const n = std::min(by_use.size(), m_pieces.size() - m_max_pieces * 3 / 4);
	auto const cut = by_use.begin() + std::ptrdiff_t(n);
	std::nth_element(by_use.begin(), cut, by_use.end());
	for (auto i = by_use.begin(); i != cut; ++i)
	{
		auto const it = m_pieces.find(i->second);
		erase_request(std::uint32_t(i->second >> 32), it->second);
		m_pieces.erase(it);
	}
}

void read_ahead::erase_request(std::uint32_t const st, piece_state& p)
{
	if (p.order == 0)
		return;
	m_requested.erase(std::make_pair(st, p.order));
	p.order = 0;
}
}
