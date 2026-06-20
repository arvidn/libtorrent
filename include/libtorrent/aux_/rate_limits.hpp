/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RATE_LIMITS_HPP_INCLUDED
#define TORRENT_RATE_LIMITS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/bandwidth_manager.hpp"
#include "libtorrent/aux_/bandwidth_queue_entry.hpp"
#include "libtorrent/aux_/bandwidth_socket.hpp"
#include "libtorrent/aux_/peer_class_set.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/span.hpp"
#if TORRENT_USE_INVARIANT_CHECKS
#include "libtorrent/aux_/string_util.hpp" // for url_random
#endif

#include <algorithm> // for std::min
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility> // for std::move
#if TORRENT_USE_ASSERTS
#include <set>
#endif

namespace libtorrent::aux {

	// concrete owner of the per-session bandwidth subsystem (channel
	// collection, manager dispatch, IP overhead application). peer_connection
	// holds a reference to this and calls its methods directly -- no virtual
	// dispatch. exists so that, when peer_connection runs on its own strand,
	// a single mutex held inside this class can encapsulate all bandwidth
	// state behind the language's access control, rather than relying on
	// callers to remember to lock.
	struct TORRENT_EXTRA_EXPORT rate_limits
	{
		// channel indices used throughout the bandwidth subsystem: into
		// peer_class::channel[2], peer_connection::m_channel_state[2] and
		// m_quota[2]. peer_connection re-exports these names as
		// peer_connection::upload_channel etc. for backward compatibility
		// with the ~100 existing call sites.
		enum channels
		{
			upload_channel,
			download_channel,
			num_channels
		};

		rate_limits(bandwidth_manager& download, bandwidth_manager& upload)
			: m_download_rate(download)
			, m_upload_rate(upload)
		{}

		rate_limits(rate_limits const&) = delete;
		rate_limits& operator=(rate_limits const&) = delete;
		rate_limits(rate_limits&&) = delete;
		rate_limits& operator=(rate_limits&&) = delete;

		int request_bandwidth(std::shared_ptr<bandwidth_socket> peer,
			int const channel,
			int const bytes,
			int const priority,
			peer_class_set const& peer_classes,
			peer_class_set const* torrent_classes)
		{
			int const max_supported_channels = bw_request::max_bandwidth_channels;
			int const max_channels = std::min(max_supported_channels,
				peer_classes.num_classes() + (torrent_classes ? torrent_classes->num_classes() : 0)
					+ 2);
			TORRENT_ALLOCA(channels, bandwidth_channel*, max_channels);

			int c = 0;
			c += copy_pertinent_channels(peer_classes, channel, channels.subspan(c));
			if (torrent_classes)
				c += copy_pertinent_channels(*torrent_classes, channel, channels.subspan(c));

#if TORRENT_USE_ASSERTS
			std::set<bandwidth_channel*> unique_classes;
			for (auto* chan : channels.first(c))
			{
				TORRENT_ASSERT(unique_classes.count(chan) == 0);
				unique_classes.insert(chan);
			}
#endif

			bandwidth_manager& manager =
				(channel == download_channel) ? m_download_rate : m_upload_rate;
			return manager.request_bandwidth(std::move(peer), bytes, priority, channels.first(c));
		}

		std::uint8_t apply_ip_overhead(peer_class_set& peer_classes,
			peer_class_set* torrent_classes,
			int const amount_down,
			int const amount_up)
		{
			std::uint8_t ret = use_quota_overhead(peer_classes, amount_down, amount_up);
			if (torrent_classes)
				ret |= use_quota_overhead(*torrent_classes, amount_down, amount_up);
			return ret;
		}

#if TORRENT_USE_ASSERTS
		bool is_queued(bandwidth_socket const* peer, int const channel) const
		{
			bandwidth_manager const& manager =
				(channel == download_channel) ? m_download_rate : m_upload_rate;
			return manager.is_queued(peer);
		}

		// returns true if `cid` refers to a live class. used by precondition
		// asserts on the public peer-class API.
		bool exists(peer_class_t const cid) const { return m_classes.at(cid) != nullptr; }
#endif

		bool ignore_unchoke_slots_set(peer_class_set const& set) const
		{
			int const num = set.num_classes();
			for (int i = 0; i < num; ++i)
			{
				peer_class const* pc = m_classes.at(set.class_at(i));
				if (pc == nullptr) continue;
				if (pc->ignore_unchoke_slots) return true;
			}
			return false;
		}

		// reads a class into a peer_class_info. returns the default-constructed
		// (or, under invariant checks, garbage-filled) info if the class is
		// missing, matching the long-standing get_peer_class() behavior.
		peer_class_info info(peer_class_t const cid) const
		{
			peer_class_info ret{};
			peer_class const* pc = m_classes.at(cid);
			if (pc == nullptr)
			{
#if TORRENT_USE_INVARIANT_CHECKS
				// make it obvious that the return value is undefined
				ret.upload_limit = 0xf0f0f0f;
				ret.download_limit = 0xf0f0f0f;
				ret.label.resize(20);
				url_random(span<char>(ret.label));
				ret.ignore_unchoke_slots = false;
				ret.connection_limit_factor = 0xf0f0f0f;
				ret.upload_priority = 0xf0f0f0f;
				ret.download_priority = 0xf0f0f0f;
#endif
				return ret;
			}
			pc->get_info(&ret);
			return ret;
		}

		// returns the throttle on a class' channel, or 0 ("no limit") if the
		// class doesn't exist. matches session_impl::rate_limit semantics.
		int throttle(peer_class_t const cid, int const channel) const
		{
			peer_class const* pc = m_classes.at(cid);
			if (pc == nullptr) return 0;
			return pc->channel[channel].throttle();
		}

		// returns the max of priority[channel] across all classes in `set`,
		// or 0 if `set` is empty. callers apply their own floor (the
		// bandwidth allocator's minimum priority is 1).
		int max_priority(peer_class_set const& set, int const channel) const
		{
			int prio = 0;
			int const n = set.num_classes();
			for (int i = 0; i < n; ++i)
			{
				peer_class const* pc = m_classes.at(set.class_at(i));
				if (pc == nullptr) continue;
				int const p = pc->priority[std::size_t(channel)];
				if (prio < p) prio = p;
			}
			return prio;
		}

#ifndef TORRENT_DISABLE_LOGGING
		// appends the label of each class in `set` to `out`, each followed
		// by a space. used for debug logging only.
		void format_class_labels(peer_class_set const& set, std::string& out) const
		{
			int const n = set.num_classes();
			for (int i = 0; i < n; ++i)
			{
				peer_class const* pc = m_classes.at(set.class_at(i));
				if (pc == nullptr) continue;
				out += pc->label;
				out += ' ';
			}
		}
#endif

		// returns the max connection_limit_factor across all classes in `set`,
		// or 0 if no class contributed a factor (caller treats 0 as "use default").
		int max_connection_limit_factor(peer_class_set const& set) const
		{
			int factor = 0;
			int const n = set.num_classes();
			for (int i = 0; i < n; ++i)
			{
				peer_class const* pc = m_classes.at(set.class_at(i));
				if (pc == nullptr) continue;
				if (factor < pc->connection_limit_factor) factor = pc->connection_limit_factor;
			}
			return factor;
		}

		// creates a new peer_class with `label` and returns its id.
		peer_class_t new_class(std::string label)
		{
			return m_classes.new_peer_class(std::move(label));
		}

		// adds `cid` to `set` and bumps the pool refcount iff it wasn't
		// already a member. silent no-op if `cid` is not a live class --
		// matches the long-standing runtime safety net at the call sites.
		void add_class_to(peer_class_set& set, peer_class_t const cid)
		{
			if (m_classes.at(cid) == nullptr) return;
			if (set.add(cid)) m_classes.incref(cid);
		}

		// removes `cid` from `set` and drops the pool refcount iff it was
		// a member.
		void remove_class_from(peer_class_set& set, peer_class_t const cid)
		{
			if (set.remove(cid)) m_classes.decref(cid);
		}

		// drops a reference to the class. no-op if `cid` doesn't refer to a
		// live class -- the caller is responsible for any precondition assert.
		void delete_class(peer_class_t const cid)
		{
			if (m_classes.at(cid) == nullptr) return;
			m_classes.decref(cid);
		}

		// writes all fields of `pci` into the class. silent no-op if `cid`
		// is not live, matching the long-standing set_peer_class() behavior.
		void set_info(peer_class_t const cid, peer_class_info const& pci)
		{
			peer_class* pc = m_classes.at(cid);
			if (pc == nullptr) return;
			pc->set_info(&pci);
		}

		// sets the throttle (bytes/sec) on a class' channel. `limit <= 0` is
		// normalized to 0 ("no limit"). silent no-op if `cid` is not live.
		// the caller is responsible for validating `channel`. returns true
		// iff the stored value actually changed -- lets callers gate
		// save-resume / state-update side effects on a real change.
		bool set_throttle(peer_class_t const cid, int const channel, int limit)
		{
			peer_class* pc = m_classes.at(cid);
			if (pc == nullptr) return false;
			if (limit <= 0)
				limit = 0;
			else
				limit = std::min(limit, std::numeric_limits<int>::max() - 1);
			return pc->channel[channel].throttle(limit);
		}

		// narrow setters used at boot time to configure built-in classes
		// (local, tcp, ...) without a full read-modify-write of peer_class_info.
		void set_ignore_unchoke_slots(peer_class_t const cid, bool const v)
		{
			peer_class* pc = m_classes.at(cid);
			if (pc == nullptr) return;
			pc->ignore_unchoke_slots = v;
		}

		void set_connection_limit_factor(peer_class_t const cid, int const f)
		{
			peer_class* pc = m_classes.at(cid);
			if (pc == nullptr) return;
			pc->connection_limit_factor = f;
		}

#if TORRENT_ABI_VERSION == 1
		// writes priority[channel] directly, without the [1,255] clamp that
		// set_info() applies. only used by the deprecated ABI-1
		// torrent::set_priority.
		void set_priority(peer_class_t const cid, int const channel, int const prio)
		{
			peer_class* pc = m_classes.at(cid);
			if (pc == nullptr) return;
			pc->priority[std::size_t(channel)] = prio;
		}
#endif

	private:
		int copy_pertinent_channels(
			peer_class_set const& set, int const channel, span<bandwidth_channel*> dst)
		{
			int const num_channels = set.num_classes();
			int num_copied = 0;
			for (int i = 0; i < num_channels; ++i)
			{
				if (num_copied >= dst.size()) break;
				peer_class* pc = m_classes.at(set.class_at(i));
				TORRENT_ASSERT(pc);
				if (pc == nullptr) continue;
				bandwidth_channel* chan = &pc->channel[channel];
				if (chan->throttle() == 0) continue;
				dst[num_copied] = chan;
				++num_copied;
			}
			return num_copied;
		}

		std::uint8_t use_quota_overhead(
			peer_class_set& set, int const amount_down, int const amount_up)
		{
			std::uint8_t ret = 0;
			int const num = set.num_classes();
			for (int i = 0; i < num; ++i)
			{
				peer_class* p = m_classes.at(set.class_at(i));
				if (p == nullptr) continue;

				bandwidth_channel* ch = &p->channel[download_channel];
				ch->use_quota(amount_down);
				if (ch->throttle() > 0 && ch->throttle() < amount_down)
					ret |= std::uint8_t(1u) << download_channel;
				ch = &p->channel[upload_channel];
				ch->use_quota(amount_up);
				if (ch->throttle() > 0 && ch->throttle() < amount_up)
					ret |= std::uint8_t(1u) << upload_channel;
			}
			return ret;
		}

		bandwidth_manager& m_download_rate;
		bandwidth_manager& m_upload_rate;
		peer_class_pool m_classes;
	};
}

#endif
