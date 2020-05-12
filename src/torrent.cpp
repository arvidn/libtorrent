/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include <cstdarg> // for va_list
#include <ctime>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <cctype>
#include <numeric>
#include <limits> // for numeric_limits
#include <cstdio> // for snprintf
#include <functional>

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/ssl_stream.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/verify_context.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // TORRENT_USE_OPENSSL

#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/http_seed_connection.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/peer_class.hpp" // for peer_class
#include "libtorrent/socket_io.hpp" // for read_*_endpoint
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/request_blocks.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/resolve_links.hpp"
#include "libtorrent/aux_/file_progress.hpp"
#include "libtorrent/aux_/has_block.hpp"
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_ip_address
#include "libtorrent/download_priority.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/range.hpp"
// TODO: factor out cache_status to its own header
#include "libtorrent/disk_io_thread.hpp" // for cache_status
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/generate_peer_id.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/aux_/session_impl.hpp" // for tracker_logger
#endif

#include "libtorrent/aux_/torrent_impl.hpp"

using namespace std::placeholders;

namespace libtorrent {
namespace {

bool is_downloading_state(int const st)
{
	switch (st)
	{
		case torrent_status::checking_files:
		case torrent_status::allocating:
		case torrent_status::checking_resume_data:
			return false;
		case torrent_status::downloading_metadata:
		case torrent_status::downloading:
		case torrent_status::finished:
		case torrent_status::seeding:
			return true;
		default:
			// unexpected state
			TORRENT_ASSERT_FAIL_VAL(st);
			return false;
	}
}
} // anonymous namespace

	constexpr web_seed_flag_t torrent::ephemeral;

	web_seed_t::web_seed_t(web_seed_entry const& wse)
		: web_seed_entry(wse)
	{
		peer_info.web_seed = true;
	}

	web_seed_t::web_seed_t(std::string const& url_, web_seed_entry::type_t type_
		, std::string const& auth_
		, web_seed_entry::headers_t const& extra_headers_)
		: web_seed_entry(url_, type_, auth_, extra_headers_)
	{
		peer_info.web_seed = true;
	}

	torrent_hot_members::torrent_hot_members(aux::session_interface& ses
		, add_torrent_params const& p, bool const session_paused)
		: m_ses(ses)
		, m_complete(0xffffff)
		, m_upload_mode(p.flags & torrent_flags::upload_mode)
		, m_connections_initialized(false)
		, m_abort(false)
		, m_paused(p.flags & torrent_flags::paused)
		, m_session_paused(session_paused)
#ifndef TORRENT_DISABLE_SHARE_MODE
		, m_share_mode(p.flags & torrent_flags::share_mode)
#endif
		, m_have_all(false)
		, m_graceful_pause_mode(false)
		, m_state_subscription(p.flags & torrent_flags::update_subscribe)
		, m_max_connections(0xffffff)
		, m_state(torrent_status::checking_resume_data)
	{}

	torrent::torrent(
		aux::session_interface& ses
		, bool const session_paused
		, add_torrent_params const& p)
		: torrent_hot_members(ses, p, session_paused)
		, m_total_uploaded(p.total_uploaded)
		, m_total_downloaded(p.total_downloaded)
		, m_tracker_timer(ses.get_io_service())
		, m_inactivity_timer(ses.get_io_service())
		, m_trackerid(p.trackerid)
		, m_save_path(complete(p.save_path))
#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		, m_url(p.url)
		, m_uuid(p.uuid)
#endif
		, m_stats_counters(ses.stats_counters())
		, m_storage_constructor(p.storage)
		, m_added_time(p.added_time ? p.added_time : std::time(nullptr))
		, m_completed_time(p.completed_time)
		, m_info_hash(p.info_hash)
		, m_error_file(torrent_status::error_file_none)
		, m_sequence_number(-1)
		, m_peer_id(aux::generate_peer_id(settings()))
		, m_announce_to_trackers(!(p.flags & torrent_flags::paused))
		, m_announce_to_lsd(!(p.flags & torrent_flags::paused))
		, m_has_incoming(false)
		, m_files_checked(false)
		, m_storage_mode(p.storage_mode)
		, m_announcing(false)
		, m_added(false)
		, m_sequential_download(p.flags & torrent_flags::sequential_download)
		, m_auto_sequential(false)
		, m_seed_mode(false)
#ifndef TORRENT_DISABLE_SUPERSEEDING
		, m_super_seeding(p.flags & torrent_flags::super_seeding)
#endif
		, m_stop_when_ready(p.flags & torrent_flags::stop_when_ready)
		, m_need_save_resume_data(p.flags & torrent_flags::need_save_resume)
		, m_enable_dht(!bool(p.flags & torrent_flags::disable_dht))
		, m_enable_lsd(!bool(p.flags & torrent_flags::disable_lsd))
		, m_max_uploads((1 << 24) - 1)
		, m_save_resume_flags()
		, m_num_uploads(0)
		, m_enable_pex(!bool(p.flags & torrent_flags::disable_pex))
		, m_magnet_link(false)
		, m_apply_ip_filter(p.flags & torrent_flags::apply_ip_filter)
		, m_pending_active_change(false)
		, m_connect_boost_counter(static_cast<std::uint8_t>(settings().get_int(settings_pack::torrent_connect_boost)))
		, m_incomplete(0xffffff)
		, m_announce_to_dht(!(p.flags & torrent_flags::paused))
		, m_ssl_torrent(false)
		, m_deleted(false)
		, m_last_download(seconds32(p.last_download))
		, m_last_upload(seconds32(p.last_upload))
		, m_auto_managed(p.flags & torrent_flags::auto_managed)
		, m_current_gauge_state(static_cast<std::uint32_t>(no_gauge_state))
		, m_moving_storage(false)
		, m_inactive(false)
		, m_downloaded(0xffffff)
		, m_progress_ppm(0)
		, m_torrent_initialized(false)
		, m_outstanding_file_priority(false)
		, m_complete_sent(false)
	{
		// we cannot log in the constructor, because it relies on shared_from_this
		// being initialized, which happens after the constructor returns.

		// TODO: 3 we could probably get away with just saving a few fields here
		// TODO: 2 p should probably be moved in here
		m_add_torrent_params.reset(new add_torrent_params(p));

#if TORRENT_USE_UNC_PATHS
		m_save_path = canonicalize_path(m_save_path);
#endif

		if (!m_apply_ip_filter)
		{
			inc_stats_counter(counters::non_filter_torrents);
		}

		if (!p.ti || !p.ti->is_valid())
		{
			// we don't have metadata for this torrent. We'll download
			// it either through the URL passed in, or through a metadata
			// extension. Make sure that when we save resume data for this
			// torrent, we also save the metadata
			m_magnet_link = true;
		}

		if (!m_torrent_file)
			m_torrent_file = (p.ti ? p.ti : std::make_shared<torrent_info>(m_info_hash));

		// in case we added the torrent via magnet link, make sure to preserve any
		// DHT nodes passed in on the URI in the torrent file itself
		if (!m_torrent_file->is_valid())
		{
			for (auto const& n : p.dht_nodes)
				m_torrent_file->add_node(n);
		}

		// --- WEB SEEDS ---

		// if override web seed flag is set, don't load any web seeds from the
		// torrent file.
		std::vector<web_seed_t> ws;
		if (!(p.flags & torrent_flags::override_web_seeds))
		{
			for (auto const& e : m_torrent_file->web_seeds())
				ws.emplace_back(e);
		}

		// add web seeds from add_torrent_params
		bool const multi_file = m_torrent_file->is_valid()
				&& m_torrent_file->num_files() > 1;

		for (auto const& u : p.url_seeds)
		{
			ws.emplace_back(web_seed_t(u, web_seed_entry::url_seed));

			// correct URLs to end with a "/" for multi-file torrents
			if (multi_file)
				ensure_trailing_slash(ws.back().url);
			if (!m_torrent_file->is_valid())
				m_torrent_file->add_url_seed(ws.back().url);
		}

		for (auto const& e : p.http_seeds)
		{
			ws.emplace_back(e, web_seed_entry::http_seed);
			if (!m_torrent_file->is_valid())
				m_torrent_file->add_http_seed(e);
		}

		aux::random_shuffle(ws);
		for (auto& w : ws) m_web_seeds.emplace_back(std::move(w));

		// --- TRACKERS ---

		// if override trackers flag is set, don't load trackers from torrent file
		if (!(p.flags & torrent_flags::override_trackers))
		{
			auto const& trackers = m_torrent_file->trackers();
			m_trackers = {trackers.begin(), trackers.end()};
		}

		int tier = 0;
		auto tier_iter = p.tracker_tiers.begin();
		for (auto const& url : p.trackers)
		{
			announce_entry e(url);
			if (tier_iter != p.tracker_tiers.end())
				tier = *tier_iter++;

			e.fail_limit = 0;
			e.source = announce_entry::source_magnet_link;
			e.tier = std::uint8_t(tier);
			if (!find_tracker(e.url))
			{
				m_trackers.push_back(e);
				// add the tracker to the m_torrent_file here so that the trackers
				// will be preserved via create_torrent() when passing in just the
				// torrent_info object.
				if (!m_torrent_file->is_valid())
					m_torrent_file->add_tracker(e.url, e.tier, announce_entry::tracker_source(e.source));
			}
		}

		std::sort(m_trackers.begin(), m_trackers.end()
			, [] (announce_entry const& lhs, announce_entry const& rhs)
			{ return lhs.tier < rhs.tier; });

		if (settings().get_bool(settings_pack::prefer_udp_trackers))
			prioritize_udp_trackers();

		// --- MERKLE TREE ---

		if (m_torrent_file->is_valid()
			&& m_torrent_file->is_merkle_torrent())
		{
			if (p.merkle_tree.size() == m_torrent_file->merkle_tree().size())
			{
				// TODO: 2 set_merkle_tree should probably take the vector as &&
				std::vector<sha1_hash> tree(p.merkle_tree);
				m_torrent_file->set_merkle_tree(tree);
			}
			else
			{
				// TODO: 0 if this is a merkle torrent and we can't
				// restore the tree, we need to wipe all the
				// bits in the have array, but not necessarily
				// we might want to do a full check to see if we have
				// all the pieces. This is low priority since almost
				// no one uses merkle torrents
				TORRENT_ASSERT_FAIL();
			}
		}

		if (m_torrent_file->is_valid())
		{
			// setting file- or piece priorities for seed mode makes no sense. If a
			// torrent ends up in seed mode by accident, it can be very confusing,
			// so assume the seed mode flag is not intended and don't enable it in
			// that case. Also, if the resume data says we're missing a piece, we
			// can't be in seed-mode.
			m_seed_mode = (p.flags & torrent_flags::seed_mode)
				&& std::find(p.file_priorities.begin(), p.file_priorities.end(), dont_download) == p.file_priorities.end()
				&& std::find(p.piece_priorities.begin(), p.piece_priorities.end(), dont_download) == p.piece_priorities.end()
				&& std::find(p.have_pieces.begin(), p.have_pieces.end(), false) == p.have_pieces.end();

			m_connections_initialized = true;
		}
		else
		{
			if (!p.name.empty()) m_name.reset(new std::string(p.name));
		}

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		if (!m_url.empty() && m_uuid.empty()) m_uuid = m_url;
#endif

		TORRENT_ASSERT(is_single_thread());
		m_file_priority.assign(p.file_priorities.begin(), p.file_priorities.end());

		if (m_seed_mode)
		{
			m_verified.resize(m_torrent_file->num_pieces(), false);
			m_verifying.resize(m_torrent_file->num_pieces(), false);
		}

		m_total_uploaded = p.total_uploaded;
		m_total_downloaded = p.total_downloaded;

		// the number of seconds this torrent has spent in started, finished and
		// seeding state so far, respectively.
		m_active_time = seconds(p.active_time);
		m_finished_time = seconds(p.finished_time);
		m_seeding_time = seconds(p.seeding_time);

		if (m_completed_time != 0 && m_completed_time < m_added_time)
			m_completed_time = m_added_time;

#if TORRENT_ABI_VERSION == 1
		if (!m_name && !m_url.empty()) m_name.reset(new std::string(m_url));
#endif

		if (valid_metadata())
		{
			inc_stats_counter(counters::num_total_pieces_added
				, m_torrent_file->num_pieces());
		}
	}

	void torrent::inc_stats_counter(int c, int value)
	{ m_ses.stats_counters().inc_stats_counter(c, value); }

#if TORRENT_ABI_VERSION == 1
	// deprecated in 1.2
	void torrent::on_torrent_download(error_code const& ec
		, http_parser const& parser, span<char const> data) try
	{
		if (m_abort) return;

		if (ec && ec != boost::asio::error::eof)
		{
			set_error(ec, torrent_status::error_file_url);
			pause();
			return;
		}

		if (parser.status_code() != 200)
		{
			set_error(error_code(parser.status_code(), http_category()), torrent_status::error_file_url);
			pause();
			return;
		}

		error_code e;
		auto tf = std::make_shared<torrent_info>(data, std::ref(e), from_span);
		if (e)
		{
			set_error(e, torrent_status::error_file_url);
			pause();
			return;
		}

		// update our torrent_info object and move the
		// torrent from the old info-hash to the new one
		// as we replace the torrent_info object
		// we're about to erase the session's reference to this
		// torrent, create another reference
		auto me = shared_from_this();

		m_ses.remove_torrent_impl(me, {});

		if (alerts().should_post<torrent_update_alert>())
			alerts().emplace_alert<torrent_update_alert>(get_handle(), info_hash(), tf->info_hash());

		m_torrent_file = tf;
		m_info_hash = tf->info_hash();

		// now, we might already have this torrent in the session.
		std::shared_ptr<torrent> t = m_ses.find_torrent(m_torrent_file->info_hash()).lock();
		if (t)
		{
			if (!m_uuid.empty() && t->uuid().empty())
				t->set_uuid(m_uuid);
			if (!m_url.empty() && t->url().empty())
				t->set_url(m_url);

			// insert this torrent in the uuid index
			if (!m_uuid.empty() || !m_url.empty())
			{
				m_ses.insert_uuid_torrent(m_uuid.empty() ? m_url : m_uuid, t);
			}

			// TODO: if the existing torrent doesn't have metadata, insert
			// the metadata we just downloaded into it.

			set_error(errors::duplicate_torrent, torrent_status::error_file_url);
			abort();
			return;
		}

		m_ses.insert_torrent(m_torrent_file->info_hash(), me, m_uuid);

		// if the user added any trackers while downloading the
		// .torrent file, merge them into the new tracker list
		std::vector<announce_entry> new_trackers = m_torrent_file->trackers();
		for (auto const& tr : m_trackers)
		{
			// if we already have this tracker, ignore it
			if (std::any_of(new_trackers.begin(), new_trackers.end()
				, [&tr] (announce_entry const& ae) { return ae.url == tr.url; }))
				continue;

			// insert the tracker ordered by tier
			new_trackers.insert(std::find_if(new_trackers.begin(), new_trackers.end()
				, [&tr] (announce_entry const& ae) { return ae.tier >= tr.tier; }), tr);
		}
		m_trackers.swap(new_trackers);

		// add the web seeds from the .torrent file
		std::vector<web_seed_entry> const& web_seeds = m_torrent_file->web_seeds();
		std::vector<web_seed_t> ws(web_seeds.begin(), web_seeds.end());
		aux::random_shuffle(ws);
		for (auto& w : ws) m_web_seeds.push_back(std::move(w));

#if !defined TORRENT_DISABLE_ENCRYPTION
		static char const req2[4] = {'r', 'e', 'q', '2'};
		hasher h(req2);
		h.update(m_torrent_file->info_hash());
		m_ses.add_obfuscated_hash(h.final(), shared_from_this());
#endif

		if (m_ses.alerts().should_post<metadata_received_alert>())
		{
			m_ses.alerts().emplace_alert<metadata_received_alert>(
				get_handle());
		}

		state_updated();

		set_state(torrent_status::downloading);

		init();
	}
	catch (...) { handle_exception(); }

#endif // TORRENT_ABI_VERSION

	int torrent::current_stats_state() const
	{
		if (m_abort || !m_added)
			return counters::num_checking_torrents + no_gauge_state;

		if (has_error()) return counters::num_error_torrents;
		if (m_paused || m_graceful_pause_mode)
		{
			if (!is_auto_managed()) return counters::num_stopped_torrents;
			if (is_seed()) return counters::num_queued_seeding_torrents;
			return counters::num_queued_download_torrents;
		}
		if (state() == torrent_status::checking_files
#if TORRENT_ABI_VERSION == 1
			|| state() == torrent_status::queued_for_checking
#endif
			)
			return counters::num_checking_torrents;
		else if (is_seed()) return counters::num_seeding_torrents;
		else if (is_upload_only()) return counters::num_upload_only_torrents;
		return counters::num_downloading_torrents;
	}

	void torrent::update_gauge()
	{
		int const new_gauge_state = current_stats_state() - counters::num_checking_torrents;
		TORRENT_ASSERT(new_gauge_state >= 0);
		TORRENT_ASSERT(new_gauge_state <= no_gauge_state);

		if (new_gauge_state == int(m_current_gauge_state)) return;

		if (m_current_gauge_state != no_gauge_state)
			inc_stats_counter(m_current_gauge_state + counters::num_checking_torrents, -1);
		if (new_gauge_state != no_gauge_state)
			inc_stats_counter(new_gauge_state + counters::num_checking_torrents, 1);

		TORRENT_ASSERT(new_gauge_state >= 0);
		TORRENT_ASSERT(new_gauge_state <= no_gauge_state);
		m_current_gauge_state = static_cast<std::uint32_t>(new_gauge_state);
	}

	void torrent::leave_seed_mode(seed_mode_t const checking)
	{
		if (!m_seed_mode) return;

		if (checking == seed_mode_t::check_files)
		{
			// this means the user promised we had all the
			// files, but it turned out we didn't. This is
			// an error.

			// TODO: 2 post alert

#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** FAILED SEED MODE, rechecking");
#endif
		}

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** LEAVING SEED MODE (%s)"
			, checking == seed_mode_t::skip_checking ? "as seed" : "as non-seed");
#endif
		m_seed_mode = false;
		// seed is false if we turned out not
		// to be a seed after all
		if (checking == seed_mode_t::check_files
			&& state() != torrent_status::checking_resume_data)
		{
			m_have_all = false;
			set_state(torrent_status::downloading);
			force_recheck();
		}
		m_num_verified = 0;
		m_verified.clear();
		m_verifying.clear();

		set_need_save_resume();
	}

	void torrent::verified(piece_index_t const piece)
	{
		TORRENT_ASSERT(!m_verified.get_bit(piece));
		++m_num_verified;
		m_verified.set_bit(piece);
	}

	void torrent::start()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_was_started == false);
#if TORRENT_USE_ASSERTS
		m_was_started = true;
#endif

		// Some of these calls may log to the torrent debug log, which requires a
		// call to get_handle(), which requires the torrent object to be fully
		// constructed, as it relies on get_shared_from_this()
		if (m_add_torrent_params)
		{
#if TORRENT_ABI_VERSION == 1
			if (m_add_torrent_params->internal_resume_data_error
				&& m_ses.alerts().should_post<fastresume_rejected_alert>())
			{
				m_ses.alerts().emplace_alert<fastresume_rejected_alert>(get_handle()
					, m_add_torrent_params->internal_resume_data_error, ""
					, operation_t::unknown);
			}
#endif

			add_torrent_params const& p = *m_add_torrent_params;

			set_max_uploads(p.max_uploads, false);
			set_max_connections(p.max_connections, false);
			set_limit_impl(p.upload_limit, peer_connection::upload_channel, false);
			set_limit_impl(p.download_limit, peer_connection::download_channel, false);

			for (auto const& peer : p.peers)
			{
				add_peer(peer, peer_info::resume_data);
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log() && !p.peers.empty())
			{
				std::string str;
				for (auto const& peer : p.peers)
				{
					error_code ec;
					str += peer.address().to_string(ec);
					str += ' ';
				}
				debug_log("add_torrent add_peer() [ %s] connect-candidates: %d"
					, str.c_str(), m_peer_list
					? m_peer_list->num_connect_candidates() : -1);
			}
#endif
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("creating torrent: %s max-uploads: %d max-connections: %d "
				"upload-limit: %d download-limit: %d flags: %s%s%s%s%s%s%s%s%s%s%s "
				"save-path: %s"
				, torrent_file().name().c_str()
				, int(m_max_uploads)
				, int(m_max_connections)
				, upload_limit()
				, download_limit()
				, m_seed_mode ? "seed-mode " : ""
				, m_upload_mode ? "upload-mode " : ""
#ifndef TORRENT_DISABLE_SHARE_MODE
				, m_share_mode ? "share-mode " : ""
#else
				, ""
#endif
				, m_apply_ip_filter ? "apply-ip-filter " : ""
				, m_paused ? "paused " : ""
				, m_auto_managed ? "auto-managed " : ""
				, m_state_subscription ? "update-subscribe " : ""
#ifndef TORRENT_DISABLE_SUPERSEEDING
				, m_super_seeding ? "super-seeding " : ""
#else
				, ""
#endif
				, m_sequential_download ? "sequential-download " : ""
				, (m_add_torrent_params && m_add_torrent_params->flags & torrent_flags::override_trackers)
					? "override-trackers "  : ""
				, (m_add_torrent_params && m_add_torrent_params->flags & torrent_flags::override_web_seeds)
					? "override-web-seeds " : ""
				, m_save_path.c_str()
				);
		}
#endif

		update_gauge();

		update_want_peers();
		update_want_scrape();
		update_want_tick();
		update_state_list();

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		if (!m_torrent_file->is_valid() && !m_url.empty())
		{
			// we need to download the .torrent file from m_url
			start_download_url();
		}
		else
#endif
		if (m_torrent_file->is_valid())
		{
			init();
		}
		else
		{
			// we need to start announcing since we don't have any
			// metadata. To receive peers to ask for it.
			set_state(torrent_status::downloading_metadata);
			start_announcing();
		}

#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif
	}

#if TORRENT_ABI_VERSION == 1
	// deprecated in 1.2
	void torrent::start_download_url()
	{
		TORRENT_ASSERT(!m_url.empty());
		TORRENT_ASSERT(!m_torrent_file->is_valid());
		std::shared_ptr<http_connection> conn(
			new http_connection(m_ses.get_io_service()
				, m_ses.get_resolver()
				, std::bind(&torrent::on_torrent_download, shared_from_this()
					, _1, _2, _3)
				, true // bottled
				//bottled buffer size
				, settings().get_int(settings_pack::max_http_recv_buffer_size)
				, http_connect_handler()
				, http_filter_handler()
#ifdef TORRENT_USE_OPENSSL
				, m_ssl_ctx.get()
#endif
				));
		aux::proxy_settings ps = m_ses.proxy();
		conn->get(m_url, seconds(30), 0, &ps
			, 5
			, settings().get_bool(settings_pack::anonymous_mode)
				? "" : settings().get_str(settings_pack::user_agent));
		set_state(torrent_status::downloading_metadata);
	}
#endif

	void torrent::set_apply_ip_filter(bool b)
	{
		if (b == m_apply_ip_filter) return;
		if (b)
		{
			inc_stats_counter(counters::non_filter_torrents, -1);
		}
		else
		{
			inc_stats_counter(counters::non_filter_torrents);
		}
		m_apply_ip_filter = b;
		ip_filter_updated();
		state_updated();
	}

	void torrent::set_ip_filter(std::shared_ptr<const ip_filter> ipf)
	{
		m_ip_filter = std::move(ipf);
		if (!m_apply_ip_filter) return;
		ip_filter_updated();
	}

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_enable_dht) return false;
		if (!m_ses.announce_dht()) return false;

		if (!m_ses.dht()) return false;
		if (m_torrent_file->is_valid() && !m_files_checked) return false;
		if (!m_announce_to_dht) return false;
		if (m_paused) return false;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		// if we don't have the metadata, and we're waiting
		// for a web server to serve it to us, no need to announce
		// because the info-hash is just the URL hash
		if (!m_torrent_file->is_valid() && !m_url.empty()) return false;
#endif

		// don't announce private torrents
		if (m_torrent_file->is_valid() && m_torrent_file->priv()) return false;
		if (m_trackers.empty()) return true;
		if (!settings().get_bool(settings_pack::use_dht_as_fallback)) return true;

		return std::none_of(m_trackers.begin(), m_trackers.end()
			, [](announce_entry const& tr) { return bool(tr.verified); });
	}

#endif

	torrent::~torrent()
	{
		// TODO: 3 assert there are no outstanding async operations on this
		// torrent

#if TORRENT_USE_ASSERTS
		for (torrent_list_index_t i{}; i != m_links.end_index(); ++i)
		{
			if (!m_links[i].in_list()) continue;
			m_links[i].unlink(m_ses.torrent_list(i), i);
		}
#endif

		// The invariant can't be maintained here, since the torrent
		// is being destructed, all weak references to it have been
		// reset, which means that all its peers already have an
		// invalidated torrent pointer (so it cannot be verified to be correct)

		// i.e. the invariant can only be maintained if all connections have
		// been closed by the time the torrent is destructed. And they are
		// supposed to be closed. So we can still do the invariant check.

		// however, the torrent object may be destructed from the main
		// thread when shutting down, if the disk cache has references to it.
		// this means that the invariant check that this is called from the
		// network thread cannot be maintained

		TORRENT_ASSERT(m_peer_class == peer_class_t{0});
		TORRENT_ASSERT(m_connections.empty());
		// just in case, make sure the session accounting is kept right
		for (auto p : m_connections)
			m_ses.close_connection(p);
	}

	void torrent::read_piece(piece_index_t const piece)
	{
		error_code ec;
		if (m_abort || m_deleted)
		{
			ec.assign(boost::system::errc::operation_canceled, generic_category());
		}
		else if (!valid_metadata())
		{
			ec.assign(errors::no_metadata, libtorrent_category());
		}
		else if (piece < piece_index_t{0} || piece >= m_torrent_file->end_piece())
		{
			ec.assign(errors::invalid_piece_index, libtorrent_category());
		}

		if (ec)
		{
			m_ses.alerts().emplace_alert<read_piece_alert>(get_handle(), piece, ec);
			return;
		}

		const int piece_size = m_torrent_file->piece_size(piece);
		const int blocks_in_piece = (piece_size + block_size() - 1) / block_size();

		TORRENT_ASSERT(blocks_in_piece > 0);
		TORRENT_ASSERT(piece_size > 0);

		if (blocks_in_piece == 0)
		{
			// this shouldn't actually happen
			boost::shared_array<char> buf;
			m_ses.alerts().emplace_alert<read_piece_alert>(
				get_handle(), piece, buf, 0);
			return;
		}

		std::shared_ptr<read_piece_struct> rp = std::make_shared<read_piece_struct>();
		rp->piece_data.reset(new (std::nothrow) char[std::size_t(piece_size)]);
		if (!rp->piece_data)
		{
			m_ses.alerts().emplace_alert<read_piece_alert>(
				get_handle(), piece, error_code(boost::system::errc::not_enough_memory, generic_category()));
			return;
		}
		rp->blocks_left = blocks_in_piece;
		rp->fail = false;

		peer_request r;
		r.piece = piece;
		r.start = 0;
		for (int i = 0; i < blocks_in_piece; ++i, r.start += block_size())
		{
			r.length = std::min(piece_size - r.start, block_size());
			m_ses.disk_thread().async_read(m_storage, r
				, std::bind(&torrent::on_disk_read_complete
				, shared_from_this(), _1, _2, _3, r, rp));
		}
		m_ses.disk_thread().submit_jobs();
	}

#ifndef TORRENT_DISABLE_SHARE_MODE
	void torrent::send_share_mode()
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const pc : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			if (pc->type() != connection_type::bittorrent) continue;
			auto* p = static_cast<bt_peer_connection*>(pc);
			p->write_share_mode();
		}
#endif
	}
#endif // TORRENT_DISABLE_SHARE_MODE

	void torrent::send_upload_only()
	{
#ifndef TORRENT_DISABLE_EXTENSIONS

#ifndef TORRENT_DISABLE_SHARE_MODE
		if (share_mode()) return;
#endif
#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (super_seeding()) return;
#endif

		// if we send upload-only, the other end is very likely to disconnect
		// us, at least if it's a seed. If we don't want to close redundant
		// connections, don't sent upload-only
		if (!settings().get_bool(settings_pack::close_redundant_connections)) return;

		// if we're super seeding, we don't want to make peers
		// think that we only have a single piece and is upload
		// only, since they might disconnect immediately when
		// they have downloaded a single piece, although we'll
		// make another piece available
		bool const upload_only_enabled = is_upload_only()
#ifndef TORRENT_DISABLE_SUPERSEEDING
			&& !super_seeding()
#endif
			;

		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);

			p->send_not_interested();
			p->send_upload_only(upload_only_enabled);
		}
#endif // TORRENT_DISABLE_EXTENSIONS
	}

	torrent_flags_t torrent::flags() const
	{
		torrent_flags_t ret = torrent_flags_t{};
		if (m_seed_mode)
			ret |= torrent_flags::seed_mode;
		if (m_upload_mode)
			ret |= torrent_flags::upload_mode;
#ifndef TORRENT_DISABLE_SHARE_MODE
		if (m_share_mode)
			ret |= torrent_flags::share_mode;
#endif
		if (m_apply_ip_filter)
			ret |= torrent_flags::apply_ip_filter;
		if (is_torrent_paused())
			ret |= torrent_flags::paused;
		if (m_auto_managed)
			ret |= torrent_flags::auto_managed;
#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (m_super_seeding)
			ret |= torrent_flags::super_seeding;
#endif
		if (m_sequential_download)
			ret |= torrent_flags::sequential_download;
		if (m_stop_when_ready)
			ret |= torrent_flags::stop_when_ready;
		if (!m_enable_dht)
			ret |= torrent_flags::disable_dht;
		if (!m_enable_lsd)
			ret |= torrent_flags::disable_lsd;
		if (!m_enable_pex)
			ret |= torrent_flags::disable_pex;
		return ret;
	}

	void torrent::set_flags(torrent_flags_t const flags
		, torrent_flags_t const mask)
	{
		if ((mask & torrent_flags::seed_mode)
			&& !(flags & torrent_flags::seed_mode))
		{
			leave_seed_mode(seed_mode_t::check_files);
		}
		if (mask & torrent_flags::upload_mode)
			set_upload_mode(bool(flags & torrent_flags::upload_mode));
#ifndef TORRENT_DISABLE_SHARE_MODE
		if (mask & torrent_flags::share_mode)
			set_share_mode(bool(flags & torrent_flags::share_mode));
#endif
		if (mask & torrent_flags::apply_ip_filter)
			set_apply_ip_filter(bool(flags & torrent_flags::apply_ip_filter));
		if (mask & torrent_flags::paused)
		{
			if (flags & torrent_flags::paused)
				pause(torrent_handle::graceful_pause);
			else
				resume();
		}
		if (mask & torrent_flags::auto_managed)
			auto_managed(bool(flags & torrent_flags::auto_managed));
#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (mask & torrent_flags::super_seeding)
			set_super_seeding(bool(flags & torrent_flags::super_seeding));
#endif
		if (mask & torrent_flags::sequential_download)
			set_sequential_download(bool(flags & torrent_flags::sequential_download));
		if (mask & torrent_flags::stop_when_ready)
			stop_when_ready(bool(flags & torrent_flags::stop_when_ready));
		if (mask & torrent_flags::disable_dht)
			m_enable_dht = !bool(flags & torrent_flags::disable_dht);
		if (mask & torrent_flags::disable_lsd)
			m_enable_lsd = !bool(flags & torrent_flags::disable_lsd);
		if (mask & torrent_flags::disable_pex)
			m_enable_pex = !bool(flags & torrent_flags::disable_pex);
	}

#ifndef TORRENT_DISABLE_SHARE_MODE
	void torrent::set_share_mode(bool s)
	{
		if (s == m_share_mode) return;

		m_share_mode = s;
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** set-share-mode: %d", s);
#endif
		if (m_share_mode)
		{
			std::size_t const num_files = valid_metadata()
				? std::size_t(m_torrent_file->num_files())
				: m_file_priority.size();
			// in share mode, all pieces have their priorities initialized to
			// dont_download
			prioritize_files(aux::vector<download_priority_t, file_index_t>(num_files, dont_download));
		}
	}
#endif // TORRENT_DISABLE_SHARE_MODE

	void torrent::set_upload_mode(bool b)
	{
		if (b == m_upload_mode) return;

		m_upload_mode = b;
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** set-upload-mode: %d", b);
#endif

		update_gauge();
		state_updated();
		send_upload_only();

		if (m_upload_mode)
		{
			// clear request queues of all peers
			for (auto p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				// we may want to disconnect other upload-only peers
				if (p->upload_only())
					p->update_interest();
				p->cancel_all_requests();
			}
			// this is used to try leaving upload only mode periodically
			m_upload_mode_time = aux::time_now32();
		}
		else if (m_peer_list)
		{
			// reset last_connected, to force fast reconnect after leaving upload mode
			for (auto pe : *m_peer_list)
			{
				pe->last_connected = 0;
			}

			// send_block_requests on all peers
			for (auto p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				// we may be interested now, or no longer interested
				p->update_interest();
				p->send_block_requests();
			}
		}
	}

	void torrent::need_peer_list()
	{
		if (m_peer_list) return;
		m_peer_list.reset(new peer_list(m_ses.get_peer_allocator()));
	}

	void torrent::handle_exception()
	{
		try
		{
			throw;
		}
		catch (system_error const& err)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("torrent exception: (%d) %s: %s"
					, err.code().value(), err.code().message().c_str()
					, err.what());
			}
#endif
			set_error(err.code(), torrent_status::error_file_exception);
		}
		catch (std::exception const& err)
		{
			TORRENT_UNUSED(err);
			set_error(error_code(), torrent_status::error_file_exception);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("torrent exception: %s", err.what());
			}
#endif
		}
		catch (...)
		{
			set_error(error_code(), torrent_status::error_file_exception);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("torrent exception: unknown");
			}
#endif
		}
	}

	void torrent::handle_disk_error(string_view job_name
		, storage_error const& error
		, peer_connection* c
		, disk_class rw)
	{
		TORRENT_UNUSED(job_name);
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(error);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("disk error: (%d) %s [%*s : %s] in file: %s"
				, error.ec.value(), error.ec.message().c_str()
				, int(job_name.size()), job_name.data()
				, operation_name(error.operation)
				, resolve_filename(error.file()).c_str());
		}
#endif

		if (error.ec == boost::system::errc::not_enough_memory)
		{
			if (alerts().should_post<file_error_alert>())
				alerts().emplace_alert<file_error_alert>(error.ec
					, resolve_filename(error.file()), error.operation, get_handle());
			if (c) c->disconnect(errors::no_memory, error.operation);
			return;
		}

		if (error.ec == boost::asio::error::operation_aborted) return;

		// notify the user of the error
		if (alerts().should_post<file_error_alert>())
			alerts().emplace_alert<file_error_alert>(error.ec
				, resolve_filename(error.file()), error.operation, get_handle());

		// if a write operation failed, and future writes are likely to
		// fail, while reads may succeed, just set the torrent to upload mode
		// if we make an incorrect assumption here, it's not the end of the
		// world, if we ever issue a read request and it fails as well, we
		// won't get in here and we'll actually end up pausing the torrent
		if (rw == disk_class::write
			&& (error.ec == boost::system::errc::read_only_file_system
			|| error.ec == boost::system::errc::permission_denied
			|| error.ec == boost::system::errc::operation_not_permitted
			|| error.ec == boost::system::errc::no_space_on_device
			|| error.ec == boost::system::errc::file_too_large))
		{
			// if we failed to write, stop downloading and just
			// keep seeding.
			// TODO: 1 make this depend on the error and on the filesystem the
			// files are being downloaded to. If the error is no_space_left_on_device
			// and the filesystem doesn't support sparse files, only zero the priorities
			// of the pieces that are at the tails of all files, leaving everything
			// up to the highest written piece in each file
			set_upload_mode(true);
			return;
		}

		// put the torrent in an error-state
		set_error(error.ec, error.file());

		// if the error appears to be more serious than a full disk, just pause the torrent
		pause();
	}

	void torrent::on_piece_fail_sync(piece_index_t, piece_block) try
	{
		if (m_abort) return;

		update_gauge();
		// some peers that previously was no longer interesting may
		// now have become interesting, since we lack this one piece now.
		for (auto i = begin(); i != end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			// no need to do anything with peers that
			// already are interested. Gaining a piece may
			// only make uninteresting peers interesting again.
			if (p->is_interesting()) continue;
			p->update_interest();
			if (!m_abort)
			{
				if (request_a_block(*this, *p))
					inc_stats_counter(counters::hash_fail_piece_picks);
				p->send_block_requests();
			}
		}
	}
	catch (...) { handle_exception(); }

	void torrent::on_disk_read_complete(disk_buffer_holder buffer
		, disk_job_flags_t, storage_error const& se
		, peer_request const&  r, std::shared_ptr<read_piece_struct> rp) try
	{
		// hold a reference until this function returns
		TORRENT_ASSERT(is_single_thread());

		--rp->blocks_left;
		if (se)
		{
			rp->fail = true;
			rp->error = se.ec;
			handle_disk_error("read", se);
		}
		else
		{
			std::memcpy(rp->piece_data.get() + r.start, buffer.get(), aux::numeric_cast<std::size_t>(r.length));
		}

		if (rp->blocks_left == 0)
		{
			int size = m_torrent_file->piece_size(r.piece);
			if (rp->fail)
			{
				m_ses.alerts().emplace_alert<read_piece_alert>(
					get_handle(), r.piece, rp->error);
			}
			else
			{
				m_ses.alerts().emplace_alert<read_piece_alert>(
					get_handle(), r.piece, rp->piece_data, size);
			}
		}
	}
	catch (...) { handle_exception(); }

	storage_mode_t torrent::storage_mode() const
	{ return storage_mode_t(m_storage_mode); }

	storage_interface* torrent::get_storage_impl() const
	{
		return m_ses.disk_thread().get_torrent(m_storage);
	}

	void torrent::need_picker()
	{
		if (m_picker) return;

		TORRENT_ASSERT(valid_metadata());
		TORRENT_ASSERT(m_connections_initialized);

		INVARIANT_CHECK;

		// if we have all pieces we should not have a picker
		// unless we're in suggest mode
		TORRENT_ASSERT(!m_have_all
			|| settings().get_int(settings_pack::suggest_mode)
			== settings_pack::suggest_read_cache);

		int const blocks_per_piece
			= (m_torrent_file->piece_length() + block_size() - 1) / block_size();
		int const blocks_in_last_piece
			= ((m_torrent_file->total_size() % m_torrent_file->piece_length())
			+ block_size() - 1) / block_size();

		std::unique_ptr<piece_picker> pp(new piece_picker(blocks_per_piece
			, blocks_in_last_piece
			, m_torrent_file->num_pieces()));

		if (m_have_all) pp->we_have_all();

		// initialize the file progress too
		if (m_file_progress.empty())
			m_file_progress.init(*pp, m_torrent_file->files());

		m_picker = std::move(pp);

		update_gauge();

		for (auto const p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			if (p->is_disconnecting()) continue;
			peer_has(p->get_bitfield(), p);
		}
	}

	struct piece_refcount
	{
		piece_refcount(piece_picker& p, piece_index_t piece)
			: m_picker(p)
			, m_piece(piece)
		{
			m_picker.inc_refcount(m_piece, nullptr);
		}

		piece_refcount(piece_refcount const&) = delete;
		piece_refcount& operator=(piece_refcount const&) = delete;

		~piece_refcount()
		{
			m_picker.dec_refcount(m_piece, nullptr);
		}

	private:
		piece_picker& m_picker;
		piece_index_t m_piece;
	};

	// TODO: 3 there's some duplication between this function and
	// peer_connection::incoming_piece(). is there a way to merge something?
	void torrent::add_piece(piece_index_t const piece, char const* data
		, add_piece_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		int const piece_size = m_torrent_file->piece_size(piece);
		int const blocks_in_piece = (piece_size + block_size() - 1) / block_size();

		if (m_deleted) return;

		// avoid crash trying to access the picker when there is none
		if (m_have_all && !has_picker()) return;

		need_picker();

		if (picker().have_piece(piece)
			&& !(flags & torrent_handle::overwrite_existing))
			return;

		peer_request p;
		p.piece = piece;
		p.start = 0;
		piece_refcount refcount{picker(), piece};
		for (int i = 0; i < blocks_in_piece; ++i, p.start += block_size())
		{
			piece_block const block(piece, i);
			if (!(flags & torrent_handle::overwrite_existing)
				&& picker().is_finished(block))
				continue;

			p.length = std::min(piece_size - p.start, block_size());

			m_stats_counters.inc_stats_counter(counters::queued_write_bytes, p.length);
			m_ses.disk_thread().async_write(m_storage, p, data + p.start, nullptr
				, std::bind(&torrent::on_disk_write_complete
				, shared_from_this(), _1, p));

			bool const was_finished = picker().is_piece_finished(p.piece);
			bool const multi = picker().num_peers(block) > 1;

			picker().mark_as_downloading(block, nullptr);
			picker().mark_as_writing(block, nullptr);

			if (multi) cancel_block(block);

			// did we just finish the piece?
			// this means all blocks are either written
			// to disk or are in the disk write cache
			if (picker().is_piece_finished(p.piece) && !was_finished)
			{
				verify_piece(p.piece);
			}
		}
	}

	void torrent::on_disk_write_complete(storage_error const& error
		, peer_request const& p) try
	{
		TORRENT_ASSERT(is_single_thread());

		m_stats_counters.inc_stats_counter(counters::queued_write_bytes, -p.length);

//		std::fprintf(stderr, "torrent::on_disk_write_complete ret:%d piece:%d block:%d\n"
//			, j->ret, j->piece, j->offset/0x4000);

		INVARIANT_CHECK;
		if (m_abort) return;
		piece_block const block_finished(p.piece, p.start / block_size());

		if (error)
		{
			handle_disk_error("write", error);
			return;
		}

		if (!has_picker()) return;

		// if we already have this block, just ignore it.
		// this can happen if the same block is passed in through
		// add_piece() multiple times
		if (picker().is_finished(block_finished)) return;

		picker().mark_as_finished(block_finished, nullptr);
		maybe_done_flushing();

		if (alerts().should_post<block_finished_alert>())
		{
			alerts().emplace_alert<block_finished_alert>(get_handle(),
				tcp::endpoint(), peer_id(), block_finished.block_index
				, block_finished.piece_index);
		}
	}
	catch (...) { handle_exception(); }

	bool torrent::add_merkle_nodes(std::map<int, sha1_hash> const& nodes
		, piece_index_t const piece)
	{
		return m_torrent_file->add_merkle_nodes(nodes, piece);
	}

	peer_request torrent::to_req(piece_block const& p) const
	{
		int block_offset = p.block_index * block_size();
		int block = std::min(torrent_file().piece_size(
			p.piece_index) - block_offset, block_size());
		TORRENT_ASSERT(block > 0);
		TORRENT_ASSERT(block <= block_size());

		peer_request r;
		r.piece = p.piece_index;
		r.start = block_offset;
		r.length = block;
		return r;
	}

	std::string torrent::name() const
	{
		if (valid_metadata()) return m_torrent_file->name();
		if (m_name) return *m_name;
		return "";
	}

#ifndef TORRENT_DISABLE_EXTENSIONS

	void torrent::add_extension(std::shared_ptr<torrent_plugin> ext)
	{
		m_extensions.push_back(ext);
	}

	void torrent::remove_extension(std::shared_ptr<torrent_plugin> ext)
	{
		auto const i = std::find(m_extensions.begin(), m_extensions.end(), ext);
		if (i == m_extensions.end()) return;
		m_extensions.erase(i);
	}

	void torrent::add_extension_fun(std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> const& ext
		, void* userdata)
	{
		std::shared_ptr<torrent_plugin> tp(ext(get_handle(), userdata));
		if (!tp) return;

		add_extension(tp);

		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			std::shared_ptr<peer_plugin> pp(tp->new_connection(peer_connection_handle(p->self())));
			if (pp) p->add_extension(std::move(pp));
		}

		// if files are checked for this torrent, call the extension
		// to let it initialize itself
		if (m_connections_initialized)
			tp->on_files_checked();
	}

#endif

#ifdef TORRENT_USE_OPENSSL
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

	bool torrent::verify_peer_cert(bool const preverified, boost::asio::ssl::verify_context& ctx)
	{
		// if the cert wasn't signed by the correct CA, fail the verification
		if (!preverified) return false;

		// we're only interested in checking the certificate at the end of the chain.
		// TODO: is verify_peer_cert called once per certificate in the chain, and
		// this function just tells us which depth we're at right now? If so, the comment
		// makes sense.
		// any certificate that isn't the leaf (i.e. the one presented by the peer)
		// should be accepted automatically, given preverified is true. The leaf certificate
		// need to be verified to make sure its DN matches the info-hash
		int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
		if (depth > 0) return true;

		X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());

		// Go through the alternate names in the certificate looking for matching DNS entries
		auto* gens = static_cast<GENERAL_NAMES*>(
			X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

#ifndef TORRENT_DISABLE_LOGGING
		std::string names;
		bool match = false;
#endif
		for (int i = 0; i < aux::openssl_num_general_names(gens); ++i)
		{
			GENERAL_NAME* gen = aux::openssl_general_name_value(gens, i);
			if (gen->type != GEN_DNS) continue;
			ASN1_IA5STRING* domain = gen->d.dNSName;
			if (domain->type != V_ASN1_IA5STRING || !domain->data || !domain->length) continue;
			auto const* torrent_name = reinterpret_cast<char const*>(domain->data);
			std::size_t const name_length = aux::numeric_cast<std::size_t>(domain->length);

#ifndef TORRENT_DISABLE_LOGGING
			if (i > 1) names += " | n: ";
			names.append(torrent_name, name_length);
#endif
			if (std::strncmp(torrent_name, "*", name_length) == 0
				|| std::strncmp(torrent_name, m_torrent_file->name().c_str(), name_length) == 0)
			{
#ifndef TORRENT_DISABLE_LOGGING
				match = true;
				// if we're logging, keep looping over all names,
				// for completeness of the log
				continue;
#else
				return true;
#endif
			}
		}

		// no match in the alternate names, so try the common names. We should only
		// use the "most specific" common name, which is the last one in the list.
		X509_NAME* name = X509_get_subject_name(cert);
		int i = -1;
		ASN1_STRING* common_name = nullptr;
		while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0)
		{
			X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
			common_name = X509_NAME_ENTRY_get_data(name_entry);
		}
		if (common_name && common_name->data && common_name->length)
		{
			auto const* torrent_name = reinterpret_cast<char const*>(common_name->data);
			std::size_t const name_length = aux::numeric_cast<std::size_t>(common_name->length);

#ifndef TORRENT_DISABLE_LOGGING
			if (!names.empty()) names += " | n: ";
			names.append(torrent_name, name_length);
#endif

			if (std::strncmp(torrent_name, "*", name_length) == 0
				|| std::strncmp(torrent_name, m_torrent_file->name().c_str(), name_length) == 0)
			{
#ifdef TORRENT_DISABLE_LOGGING
				return true;
#else
				match = true;
#endif
			}
		}

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== incoming SSL CONNECTION [ n: %s | match: %s ]"
			, names.c_str(), match?"yes":"no");
		return match;
#else
		return false;
#endif
	}

	void torrent::init_ssl(string_view cert)
	{
		using boost::asio::ssl::context;

		// this is needed for openssl < 1.0 to decrypt keys created by openssl 1.0+
#if !defined(OPENSSL_API_COMPAT) || (OPENSSL_API_COMPAT < 0x10100000L)
		OpenSSL_add_all_algorithms();
#else
		OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
#endif

		// create the SSL context for this torrent. We need to
		// inject the root certificate, and no other, to
		// verify other peers against
#if BOOST_VERSION >= 106400
		std::unique_ptr<context> ctx(new context(context::tls));
#else
		std::unique_ptr<context> ctx(new context(context::tlsv12));
#endif

		if (!ctx)
		{
			error_code ec(int(::ERR_get_error()),
				boost::asio::error::get_ssl_category());
			set_error(ec, torrent_status::error_file_ssl_ctx);
			pause();
			return;
		}

		ctx->set_options(context::default_workarounds
			| boost::asio::ssl::context::no_sslv2
			| boost::asio::ssl::context::single_dh_use);

		error_code ec;
		ctx->set_verify_mode(context::verify_peer
			| context::verify_fail_if_no_peer_cert
			| context::verify_client_once, ec);
		if (ec)
		{
			set_error(ec, torrent_status::error_file_ssl_ctx);
			pause();
			return;
		}

		// the verification function verifies the distinguished name
		// of a peer certificate to make sure it matches the info-hash
		// of the torrent, or that it's a "star-cert"
		ctx->set_verify_callback(std::bind(&torrent::verify_peer_cert, this, _1, _2), ec);
		if (ec)
		{
			set_error(ec, torrent_status::error_file_ssl_ctx);
			pause();
			return;
		}

		SSL_CTX* ssl_ctx = ctx->native_handle();
		// create a new x.509 certificate store
		X509_STORE* cert_store = X509_STORE_new();
		if (!cert_store)
		{
			ec.assign(int(::ERR_get_error()),
				boost::asio::error::get_ssl_category());
			set_error(ec, torrent_status::error_file_ssl_ctx);
			pause();
			return;
		}

		// wrap the PEM certificate in a BIO, for openssl to read
		BIO* bp = BIO_new_mem_buf(
			const_cast<void*>(static_cast<void const*>(cert.data()))
			, int(cert.size()));

		// parse the certificate into OpenSSL's internal
		// representation
		X509* certificate = PEM_read_bio_X509_AUX(bp, nullptr, nullptr, nullptr);

		BIO_free(bp);

		if (!certificate)
		{
			ec.assign(int(::ERR_get_error()),
				boost::asio::error::get_ssl_category());
			X509_STORE_free(cert_store);
			set_error(ec, torrent_status::error_file_ssl_ctx);
			pause();
			return;
		}

		// add cert to cert_store
		X509_STORE_add_cert(cert_store, certificate);

		X509_free(certificate);

		// and lastly, replace the default cert store with ours
		SSL_CTX_set_cert_store(ssl_ctx, cert_store);
#if 0
		char filename[100];
		std::snprintf(filename, sizeof(filename), "/tmp/%u.pem", random());
		FILE* f = fopen(filename, "w+");
		fwrite(cert.c_str(), cert.size(), 1, f);
		fclose(f);
		ctx->load_verify_file(filename);
#endif
		// if all went well, set the torrent ssl context to this one
		m_ssl_ctx = std::move(ctx);
		// tell the client we need a cert for this torrent
		alerts().emplace_alert<torrent_need_cert_alert>(get_handle());
	}
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
#endif // TORRENT_OPENSSL

	void torrent::construct_storage()
	{
		storage_params params{
			m_torrent_file->orig_files(),
			&m_torrent_file->orig_files() != &m_torrent_file->files()
				? &m_torrent_file->files() : nullptr,
			m_save_path,
			static_cast<storage_mode_t>(m_storage_mode),
			m_file_priority,
			m_info_hash
		};

		TORRENT_ASSERT(m_storage_constructor);

		m_storage = m_ses.disk_thread().new_torrent(m_storage_constructor
			, params, shared_from_this());
	}

	peer_connection* torrent::find_lowest_ranking_peer() const
	{
		auto lowest_rank = end();
		for (auto i = begin(); i != end(); ++i)
		{
			// disconnecting peers don't count
			if ((*i)->is_disconnecting()) continue;
			if (lowest_rank == end() || (*lowest_rank)->peer_rank() > (*i)->peer_rank())
				lowest_rank = i;
		}

		if (lowest_rank == end()) return nullptr;
		return *lowest_rank;
	}

	// this may not be called from a constructor because of the call to
	// shared_from_this(). It's either called when we start() the torrent, or at a
	// later time if it's a magnet link, once the metadata is downloaded
	void torrent::init()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("init torrent: %s", torrent_file().name().c_str());
#endif

		TORRENT_ASSERT(valid_metadata());
		TORRENT_ASSERT(m_torrent_file->num_files() > 0);
		TORRENT_ASSERT(m_torrent_file->total_size() >= 0);

		if (int(m_file_priority.size()) > m_torrent_file->num_files())
			m_file_priority.resize(m_torrent_file->num_files());

		auto cert = m_torrent_file->ssl_cert();
		if (!cert.empty())
		{
			m_ssl_torrent = true;
#ifdef TORRENT_USE_OPENSSL
			init_ssl(cert);
#endif
		}

		if (m_torrent_file->num_pieces() > piece_picker::max_pieces)
		{
			set_error(errors::too_many_pieces_in_torrent, torrent_status::error_file_none);
			pause();
			return;
		}

		if (m_torrent_file->num_pieces() == 0)
		{
			set_error(errors::torrent_invalid_length, torrent_status::error_file_none);
			pause();
			return;
		}

		int const blocks_per_piece
			= (m_torrent_file->piece_length() + default_block_size - 1) / default_block_size;
		if (blocks_per_piece > piece_picker::max_blocks_per_piece)
		{
			set_error(errors::invalid_piece_size, torrent_status::error_file_none);
			pause();
			return;
		}

		// --- MAPPED FILES ---
		file_storage const& fs = m_torrent_file->files();
		if (m_add_torrent_params)
		{
			for (auto const& f : m_add_torrent_params->renamed_files)
			{
				if (f.first < file_index_t(0) || f.first >= fs.end_file()) continue;
				m_torrent_file->rename_file(file_index_t(f.first), f.second);
			}
		}

		construct_storage();

#ifndef TORRENT_DISABLE_SHARE_MODE
		if (m_share_mode && valid_metadata())
		{
			// in share mode, all pieces have their priorities initialized to 0
			m_file_priority.clear();
			m_file_priority.resize(m_torrent_file->num_files(), dont_download);
		}
#endif

		// it's important to initialize the peers early, because this is what will
		// fix up their have-bitmasks to have the correct size
		// TODO: 2 add a unit test where we don't have metadata, connect to a peer
		// that sends a bitfield that's too large, then we get the metadata
		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			// copy the peer list since peers may disconnect and invalidate
			// m_connections as we initialize them
			for (auto c : m_connections)
			{
				auto pc = c->self();
				if (pc->is_disconnecting()) continue;
				pc->on_metadata_impl();
				if (pc->is_disconnecting()) continue;
				pc->init();
			}
		}

		// in case file priorities were passed in via the add_torrent_params
		// and also in the case of share mode, we need to update the priorities
		// this has to be applied before piece priority
		if (!m_file_priority.empty()) update_piece_priorities(m_file_priority);

		if (m_add_torrent_params)
		{
			piece_index_t idx(0);
			if (m_add_torrent_params->piece_priorities.size() > std::size_t(m_torrent_file->num_pieces()))
				m_add_torrent_params->piece_priorities.resize(std::size_t(m_torrent_file->num_pieces()));

			for (auto prio : m_add_torrent_params->piece_priorities)
			{
				if (has_picker() || prio != default_priority)
				{
					need_picker();
					m_picker->set_piece_priority(idx, prio);
				}
				++idx;
			}
			update_gauge();
		}


		if (m_seed_mode)
		{
			m_have_all = true;
			update_gauge();
			update_state_list();
			update_want_tick();
		}
		else
		{
			need_picker();

			TORRENT_ASSERT(block_size() > 0);

			for (auto const i : fs.file_range())
			{
				if (!fs.pad_file_at(i) || fs.file_size(i) == 0) continue;

				peer_request pr = m_torrent_file->map_file(i, 0, int(fs.file_size(i)));
				int off = pr.start & (block_size() - 1);
				if (off != 0) { pr.length -= block_size() - off; pr.start += block_size() - off; }
				TORRENT_ASSERT((pr.start & (block_size() - 1)) == 0);

				int block = block_size();
				piece_block pb(pr.piece, pr.start / block);
				for (; pr.length >= block; pr.length -= block, ++pb.block_index)
				{
					if (pb.block_index == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
					m_picker->mark_as_pad(pb);
					++m_padding_blocks;
				}
				// ugly edge case where padfiles are not used they way they're
				// supposed to be. i.e. added back-to back or at the end
				if (pb.block_index == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
				if (pr.length > 0 && ((next(i) != fs.end_file() && fs.pad_file_at(next(i)))
					|| next(i) == fs.end_file()))
				{
					m_picker->mark_as_finished(pb, nullptr);
				}
			}

			if (m_padding_blocks > 0)
			{
				// if we marked an entire piece as finished, we actually
				// need to consider it finished

				std::vector<piece_picker::downloading_piece> dq
					= m_picker->get_download_queue();

				std::vector<piece_index_t> have_pieces;

				for (auto const& p : dq)
				{
					int const num_blocks = m_picker->blocks_in_piece(p.index);
					if (p.finished < num_blocks) continue;
					have_pieces.push_back(p.index);
				}

				for (auto i : have_pieces)
				{
					picker().piece_passed(i);
					TORRENT_ASSERT(picker().have_piece(i));
					we_have(i);
				}
			}
		}

		set_state(torrent_status::checking_resume_data);

		aux::vector<std::string, file_index_t> links;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		if (!m_torrent_file->similar_torrents().empty()
			|| !m_torrent_file->collections().empty())
		{
			resolve_links res(m_torrent_file);

			for (auto const& ih : m_torrent_file->similar_torrents())
			{
				std::shared_ptr<torrent> t = m_ses.find_torrent(ih).lock();
				if (!t) continue;

				// Only attempt to reuse files from torrents that are seeding.
				// TODO: this could be optimized by looking up which files are
				// complete and just look at those
				if (!t->is_seed()) continue;

				res.match(t->get_torrent_copy(), t->save_path());
			}
			for (auto const& c : m_torrent_file->collections())
			{
				std::vector<std::shared_ptr<torrent>> ts = m_ses.find_collection(c);

				for (auto const& t : ts)
				{
					// Only attempt to reuse files from torrents that are seeding.
					// TODO: this could be optimized by looking up which files are
					// complete and just look at those
					if (!t->is_seed()) continue;

					res.match(t->get_torrent_copy(), t->save_path());
				}
			}

			std::vector<resolve_links::link_t> const& l = res.get_links();
			if (!l.empty())
			{
				for (auto const& i : l)
				{
					if (!i.ti) continue;
					links.push_back(combine_path(i.save_path
						, i.ti->files().file_path(i.file_idx)));
				}
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_outstanding_check_files == false);
		m_outstanding_check_files = true;
#endif
		m_ses.disk_thread().async_check_files(
			m_storage, m_add_torrent_params ? m_add_torrent_params.get() : nullptr
			, links, std::bind(&torrent::on_resume_data_checked
			, shared_from_this(), _1, _2));
		// async_check_files will gut links
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("init, async_check_files");
#endif

		update_want_peers();
		update_want_tick();

		// this will remove the piece picker, if we're done with it
		maybe_done_flushing();

		m_torrent_initialized = true;
	}

	bt_peer_connection* torrent::find_introducer(tcp::endpoint const& ep) const
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto pe : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			if (pe->type() != connection_type::bittorrent) continue;
			auto* p = static_cast<bt_peer_connection*>(pe);
			if (!p->supports_holepunch()) continue;
			if (p->was_introduced_by(ep)) return p;
		}
#else
		TORRENT_UNUSED(ep);
#endif
		return nullptr;
	}

	bt_peer_connection* torrent::find_peer(tcp::endpoint const& ep) const
	{
		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			if (p->type() != connection_type::bittorrent) continue;
			if (p->remote() == ep) return static_cast<bt_peer_connection*>(p);
		}
		return nullptr;
	}

	peer_connection* torrent::find_peer(peer_id const& pid)
	{
		for (auto p : m_connections)
		{
			if (p->pid() == pid) return p;
		}
		return nullptr;
	}

	bool torrent::is_self_connection(peer_id const& pid) const
	{
		return m_outgoing_pids.count(pid) > 0;
	}

	void torrent::on_resume_data_checked(status_t const status
		, storage_error const& error) try
	{
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_outstanding_check_files);
		m_outstanding_check_files = false;
#endif

		// when applying some of the resume data to the torrent, we will
		// trigger calls that set m_need_save_resume_data, even though we're
		// just applying the state of the resume data we loaded with. We don't
		// want anything in this function to affect the state of
		// m_need_save_resume_data, so we save it in a local variable and reset
		// it at the end of the function.
		bool const need_save_resume_data = m_need_save_resume_data;

		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;

		if (status == status_t::fatal_disk_error)
		{
			TORRENT_ASSERT(m_outstanding_check_files == false);
			m_add_torrent_params.reset();
			handle_disk_error("check_resume_data", error);
			auto_managed(false);
			pause();
			set_state(torrent_status::checking_files);
			if (should_check_files()) start_checking();
			return;
		}

		state_updated();

		if (m_add_torrent_params)
		{
			// --- PEERS ---

			for (auto const& p : m_add_torrent_params->peers)
			{
				add_peer(p , peer_info::resume_data);
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log() && !m_add_torrent_params->peers.empty())
			{
				error_code ec;
				std::string str;
				for (auto const& peer : m_add_torrent_params->peers)
				{
					str += peer.address().to_string(ec);
					str += ' ';
				}
				debug_log("resume-checked add_peer() [ %s] connect-candidates: %d"
					, str.c_str(), m_peer_list
					? m_peer_list->num_connect_candidates() : -1);
			}
#endif

			for (auto const& p : m_add_torrent_params->banned_peers)
			{
				torrent_peer* peer = add_peer(p, peer_info::resume_data);
				if (peer) ban_peer(peer);
			}

			if (!m_add_torrent_params->peers.empty()
				|| !m_add_torrent_params->banned_peers.empty())
			{
				update_want_peers();
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (m_peer_list && m_peer_list->num_peers() > 0)
				debug_log("resume added peers (total peers: %d)"
					, m_peer_list->num_peers());
#endif
		}

		// only report this error if the user actually provided resume data
		// (i.e. m_add_torrent_params->have_pieces)
		if ((error || status != status_t::no_error)
			&& m_add_torrent_params
			&& !m_add_torrent_params->have_pieces.empty()
			&& m_ses.alerts().should_post<fastresume_rejected_alert>())
		{
			m_ses.alerts().emplace_alert<fastresume_rejected_alert>(get_handle()
				, error.ec
				, resolve_filename(error.file())
				, error.operation);
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			if (status != status_t::no_error || error)
			{
				debug_log("fastresume data rejected: ret: %d (%d) op: %s file: %d %s"
					, static_cast<int>(status), error.ec.value()
					, operation_name(error.operation)
					, static_cast<int>(error.file())
					, error.ec.message().c_str());
			}
			else
			{
				debug_log("fastresume data accepted");
			}
		}
#endif

		bool should_start_full_check = (status != status_t::no_error)
			&& !m_seed_mode;

		// if we got a partial pieces bitfield, it means we were in the middle of
		// checking this torrent. pick it up where we left off
		if (!should_start_full_check
			&& m_add_torrent_params
			&& !m_add_torrent_params->have_pieces.empty()
			&& m_add_torrent_params->have_pieces.size() < m_torrent_file->num_pieces())
		{
			m_checking_piece = m_num_checked_pieces
				= m_add_torrent_params->have_pieces.end_index();
			should_start_full_check = true;
		}

		// if ret != 0, it means we need a full check. We don't necessarily need
		// that when the resume data check fails. For instance, if the resume data
		// is incorrect, but we don't have any files, we skip the check and initialize
		// the storage to not have anything.
		if (m_seed_mode)
		{
			m_have_all = true;
			update_gauge();
			update_state_list();
		}
		else if (status == status_t::no_error)
		{
			// there are either no files for this torrent
			// or the resume_data was accepted

			if (!error && m_add_torrent_params)
			{
				// --- PIECES ---

				int const num_pieces = std::min(m_add_torrent_params->have_pieces.size()
					, torrent_file().num_pieces());
				for (piece_index_t i = piece_index_t(0); i < piece_index_t(num_pieces); ++i)
				{
					if (!m_add_torrent_params->have_pieces[i]) continue;
					need_picker();
					m_picker->we_have(i);
					inc_stats_counter(counters::num_piece_passed);
					update_gauge();
					we_have(i);
				}

				if (m_seed_mode)
				{
					int const num_pieces2 = std::min(m_add_torrent_params->verified_pieces.size()
						, torrent_file().num_pieces());
					for (piece_index_t i = piece_index_t(0);
						i < piece_index_t(num_pieces2); ++i)
					{
						if (!m_add_torrent_params->verified_pieces[i]) continue;
						m_verified.set_bit(i);
					}
				}

				// --- UNFINISHED PIECES ---

				int const num_blocks_per_piece = torrent_file().piece_length() / block_size();

				for (auto const& p : m_add_torrent_params->unfinished_pieces)
				{
					piece_index_t const piece = p.first;
					bitfield const& blocks = p.second;

					if (piece < piece_index_t(0) || piece >= torrent_file().end_piece())
					{
						continue;
					}

					// being in seed mode and missing a piece is not compatible.
					// Leave seed mode if that happens
					if (m_seed_mode) leave_seed_mode(seed_mode_t::skip_checking);

					if (has_picker() && m_picker->have_piece(piece))
					{
						m_picker->we_dont_have(piece);
						update_gauge();
					}

					need_picker();

					const int num_bits = std::min(num_blocks_per_piece, int(blocks.size()));
					for (int k = 0; k < num_bits; ++k)
					{
						if (blocks.get_bit(k))
						{
							m_picker->mark_as_finished(piece_block(piece, k), nullptr);
						}
					}
					if (m_picker->is_piece_finished(piece))
					{
						verify_piece(piece);
					}
				}
			}
		}

		if (should_start_full_check)
		{
			// either the fastresume data was rejected or there are
			// some files
			set_state(torrent_status::checking_files);
			if (should_check_files()) start_checking();

			// start the checking right away (potentially)
			m_ses.trigger_auto_manage();
		}
		else
		{
			files_checked();
		}

		// this will remove the piece picker, if we're done with it
		maybe_done_flushing();
		TORRENT_ASSERT(m_outstanding_check_files == false);
		m_add_torrent_params.reset();

		// restore m_need_save_resume_data to its state when we entered this
		// function.
		m_need_save_resume_data = need_save_resume_data;
	}
	catch (...) { handle_exception(); }

	void torrent::force_recheck()
	{
		INVARIANT_CHECK;

		if (!valid_metadata()) return;

		// if the torrent is already queued to check its files
		// don't do anything
		if (should_check_files()
			|| m_state == torrent_status::checking_resume_data)
			return;

		clear_error();

		disconnect_all(errors::stopping_torrent, operation_t::bittorrent);
		stop_announcing();

		// we're checking everything anyway, no point in assuming we are a seed
		// now.
		leave_seed_mode(seed_mode_t::skip_checking);

		m_ses.disk_thread().async_release_files(m_storage);

		// forget that we have any pieces
		m_have_all = false;

// removing the piece picker will clear the user priorities
// instead, just clear which pieces we have
		if (m_picker)
		{
			int const blocks_per_piece = (m_torrent_file->piece_length() + block_size() - 1) / block_size();
			int const blocks_in_last_piece = ((m_torrent_file->total_size() % m_torrent_file->piece_length())
				+ block_size() - 1) / block_size();
			m_picker->resize(blocks_per_piece, blocks_in_last_piece, m_torrent_file->num_pieces());

			m_file_progress.clear();
			m_file_progress.init(picker(), m_torrent_file->files());
		}


		// assume that we don't have anything
		m_files_checked = false;

		update_gauge();
		update_want_tick();
		set_state(torrent_status::checking_resume_data);

		set_queue_position(last_pos);

		TORRENT_ASSERT(m_outstanding_check_files == false);
		m_add_torrent_params.reset();

		// this will clear the stat cache, to make us actually query the
		// filesystem for files again
		m_ses.disk_thread().async_release_files(m_storage);

		aux::vector<std::string, file_index_t> links;
		m_ses.disk_thread().async_check_files(m_storage, nullptr
			, links, std::bind(&torrent::on_force_recheck
			, shared_from_this(), _1, _2));
	}

	void torrent::on_force_recheck(status_t const status, storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());

		// hold a reference until this function returns
		state_updated();

		if (m_abort) return;

		if (error)
		{
			handle_disk_error("force_recheck", error);
			return;
		}
		if (status == status_t::no_error)
		{
			// if there are no files, just start
			files_checked();
		}
		else
		{
			m_progress_ppm = 0;
			m_checking_piece = piece_index_t(0);
			m_num_checked_pieces = piece_index_t(0);

			set_state(torrent_status::checking_files);
			if (m_auto_managed) pause(torrent_handle::graceful_pause);
			if (should_check_files()) start_checking();
			else m_ses.trigger_auto_manage();
		}
	}
	catch (...) { handle_exception(); }

	void torrent::start_checking()
	{
		TORRENT_ASSERT(should_check_files());

		int num_outstanding = settings().get_int(settings_pack::checking_mem_usage) * block_size()
			/ m_torrent_file->piece_length();
		// if we only keep a single read operation in-flight at a time, we suffer
		// significant performance degradation. Always keep at least 4 jobs
		// outstanding per hasher thread
		int const min_outstanding = 4
			* std::max(1, settings().get_int(settings_pack::aio_threads)
				/ disk_io_thread::hasher_thread_divisor);
		if (num_outstanding < min_outstanding) num_outstanding = min_outstanding;

		// we might already have some outstanding jobs, if we were paused and
		// resumed quickly, before the outstanding jobs completed
		if (m_checking_piece >= m_torrent_file->end_piece())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("start_checking, checking_piece >= num_pieces. %d >= %d"
				, static_cast<int>(m_checking_piece), m_torrent_file->num_pieces());
#endif
			return;
		}

		// subtract the number of pieces we already have outstanding
		num_outstanding -= (static_cast<int>(m_checking_piece)
			- static_cast<int>(m_num_checked_pieces));
		if (num_outstanding < 0) num_outstanding = 0;

		for (int i = 0; i < num_outstanding; ++i)
		{
			m_ses.disk_thread().async_hash(m_storage, m_checking_piece
				, disk_interface::sequential_access | disk_interface::volatile_read
				, std::bind(&torrent::on_piece_hashed
					, shared_from_this(), _1, _2, _3));
			++m_checking_piece;
			if (m_checking_piece >= m_torrent_file->end_piece()) break;
		}
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("start_checking, m_checking_piece: %d"
			, static_cast<int>(m_checking_piece));
#endif
	}

	// This is only used for checking of torrents. i.e. force-recheck or initial checking
	// of existing files
	void torrent::on_piece_hashed(piece_index_t const piece
		, sha1_hash const& piece_hash, storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_abort) return;
		if (m_deleted) return;

		state_updated();

		++m_num_checked_pieces;

		if (error)
		{
			if (error.ec == boost::system::errc::no_such_file_or_directory
				|| error.ec == boost::asio::error::eof
#ifdef TORRENT_WINDOWS
				|| error.ec == error_code(ERROR_HANDLE_EOF, system_category())
#endif
				)
			{
				TORRENT_ASSERT(error.file() >= file_index_t(0));

				// skip this file by updating m_checking_piece to the first piece following it
				file_storage const& st = m_torrent_file->files();
				std::int64_t file_size = st.file_size(error.file());
				piece_index_t last = st.map_file(error.file(), file_size, 0).piece;
				if (m_checking_piece < last)
				{
					int diff = static_cast<int>(last) - static_cast<int>(m_checking_piece);
					m_num_checked_pieces = piece_index_t(static_cast<int>(m_num_checked_pieces) + diff);
					m_checking_piece = last;
				}
			}
			else
			{
				m_checking_piece = piece_index_t{0};
				m_num_checked_pieces = piece_index_t{0};
				if (m_ses.alerts().should_post<file_error_alert>())
					m_ses.alerts().emplace_alert<file_error_alert>(error.ec,
						resolve_filename(error.file()), error.operation, get_handle());

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("on_piece_hashed, fatal disk error: (%d) %s", error.ec.value()
						, error.ec.message().c_str());
				}
#endif
				auto_managed(false);
				pause();
				set_error(error.ec, error.file());

				// recalculate auto-managed torrents sooner
				// in order to start checking the next torrent
				m_ses.trigger_auto_manage();
				return;
			}
		}

		m_progress_ppm = std::uint32_t(std::int64_t(static_cast<int>(m_num_checked_pieces))
			* 1000000 / torrent_file().num_pieces());

		if (settings().get_bool(settings_pack::disable_hash_checks)
			|| piece_hash == m_torrent_file->hash_for_piece(piece))
		{
			if (has_picker() || !m_have_all)
			{
				need_picker();
				m_picker->we_have(piece);
				update_gauge();
			}
			we_have(piece);
		}
		else
		{
			// if the hash failed, remove it from the cache
			if (m_storage)
				m_ses.disk_thread().clear_piece(m_storage, piece);
		}

		if (m_num_checked_pieces < m_torrent_file->end_piece())
		{
			// we're not done yet, issue another job
			if (m_checking_piece >= m_torrent_file->end_piece())
			{
				// actually, we already have outstanding jobs for
				// the remaining pieces. We just need to wait for them
				// to finish
				return;
			}

			// we paused the checking
			if (!should_check_files())
			{
#ifndef TORRENT_DISABLE_LOGGING
				debug_log("on_piece_hashed, checking paused");
#endif
				if (m_checking_piece == m_num_checked_pieces)
				{
					// we are paused, and we just completed the last outstanding job.
					// now we can be considered paused
					if (alerts().should_post<torrent_paused_alert>())
						alerts().emplace_alert<torrent_paused_alert>(get_handle());
				}
				return;
			}

			m_ses.disk_thread().async_hash(m_storage, m_checking_piece
				, disk_interface::sequential_access | disk_interface::volatile_read
				, std::bind(&torrent::on_piece_hashed
					, shared_from_this(), _1, _2, _3));
			++m_checking_piece;
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("on_piece_hashed, m_checking_piece: %d"
				, static_cast<int>(m_checking_piece));
#endif
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("on_piece_hashed, completed");
#endif
		if (m_auto_managed)
		{
			// if we're auto managed. assume we need to be paused until the auto
			// managed logic runs again (which is triggered further down)
			// setting flags to 0 prevents the disk cache from being evicted as a
			// result of this
			set_paused(true, {});
		}

		// we're done checking! (this should cause a call to trigger_auto_manage)
		files_checked();

		// reset the checking state
		m_checking_piece = piece_index_t(0);
		m_num_checked_pieces = piece_index_t(0);
	}
	catch (...) { handle_exception(); }

#if TORRENT_ABI_VERSION == 1
	void torrent::use_interface(std::string net_interfaces)
	{
		std::shared_ptr<settings_pack> p = std::make_shared<settings_pack>();
		p->set_str(settings_pack::outgoing_interfaces, std::move(net_interfaces));
		m_ses.apply_settings_pack(p);
	}
#endif

	void torrent::on_tracker_announce(error_code const& ec) try
	{
		COMPLETE_ASYNC("tracker::on_tracker_announce");
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_waiting_tracker > 0);
		--m_waiting_tracker;
		if (ec) return;
		if (m_abort) return;
		announce_with_tracker();
	}
	catch (...) { handle_exception(); }

	void torrent::lsd_announce()
	{
		if (m_abort) return;
		if (!m_enable_lsd) return;

		// if the files haven't been checked yet, we're
		// not ready for peers. Except, if we don't have metadata,
		// we need peers to download from
		if (!m_files_checked && valid_metadata()) return;

		if (!m_announce_to_lsd) return;

		// private torrents are never announced on LSD
		if (m_torrent_file->is_valid() && m_torrent_file->priv()) return;

		// i2p torrents are also never announced on LSD
		// unless we allow mixed swarms
		if (m_torrent_file->is_valid()
			&& (torrent_file().is_i2p() && !settings().get_bool(settings_pack::allow_i2p_mixed)))
			return;

		if (is_paused()) return;

		if (!m_ses.has_lsd()) return;

		// TODO: this pattern is repeated in a few places. Factor this into
		// a function and generalize the concept of a torrent having a
		// dedicated listen port
#ifdef TORRENT_USE_OPENSSL
		int port = is_ssl_torrent() ? m_ses.ssl_listen_port() : m_ses.listen_port();
#else
		int port = m_ses.listen_port();
#endif

		// announce with the local discovery service
		m_ses.announce_lsd(m_torrent_file->info_hash(), port);
	}

#ifndef TORRENT_DISABLE_DHT

	void torrent::dht_announce()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ses.dht())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("DHT: no dht initialized");
#endif
			return;
		}
		if (!should_announce_dht())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				if (!m_ses.announce_dht())
					debug_log("DHT: no listen sockets");

				if (m_torrent_file->is_valid() && !m_files_checked)
					debug_log("DHT: files not checked, skipping DHT announce");

				if (!m_announce_to_dht)
					debug_log("DHT: queueing disabled DHT announce");

				if (m_paused)
					debug_log("DHT: torrent paused, no DHT announce");

				if (!m_enable_dht)
					debug_log("DHT: torrent has DHT disabled flag");

#if TORRENT_ABI_VERSION == 1
				// deprecated in 1.2
				if (!m_torrent_file->is_valid() && !m_url.empty())
					debug_log("DHT: no info-hash, waiting for \"%s\"", m_url.c_str());
#endif

				if (m_torrent_file->is_valid() && m_torrent_file->priv())
					debug_log("DHT: private torrent, no DHT announce");

				if (settings().get_bool(settings_pack::use_dht_as_fallback))
				{
					int const verified_trackers = static_cast<int>(std::count_if(
						m_trackers.begin(), m_trackers.end()
						, [](announce_entry const& t) { return t.verified; }));

					if (verified_trackers > 0)
						debug_log("DHT: only using DHT as fallback, and there are %d working trackers", verified_trackers);
				}
			}
#endif
			return;
		}

		TORRENT_ASSERT(!m_paused);

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("START DHT announce");
		m_dht_start_time = aux::time_now();
#endif

		// if we're a seed, we tell the DHT for better scrape stats
		dht::announce_flags_t flags = is_seed() ? dht::announce::seed : dht::announce_flags_t{};

		// If this is an SSL torrent the announce needs to specify an SSL
		// listen port. DHT nodes only operate on non-SSL ports so SSL
		// torrents cannot use implied_port.
		// if we allow incoming uTP connections, set the implied_port
		// argument in the announce, this will make the DHT node use
		// our source port in the packet as our listen port, which is
		// likely more accurate when behind a NAT
		if (is_ssl_torrent())
		{
			flags |= dht::announce::ssl_torrent;
		}
		else if (settings().get_bool(settings_pack::enable_incoming_utp))
		{
			flags |= dht::announce::implied_port;
		}

		std::weak_ptr<torrent> self(shared_from_this());
		m_ses.dht()->announce(m_torrent_file->info_hash(), 0, flags
			, std::bind(&torrent::on_dht_announce_response_disp, self, _1));
	}

	void torrent::on_dht_announce_response_disp(std::weak_ptr<torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		std::shared_ptr<torrent> tor = t.lock();
		if (!tor) return;
		tor->on_dht_announce_response(peers);
	}

	void torrent::on_dht_announce_response(std::vector<tcp::endpoint> const& peers) try
	{
		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("END DHT announce (%d ms) (%d peers)"
			, int(total_milliseconds(clock_type::now() - m_dht_start_time))
			, int(peers.size()));
#endif

		if (m_abort) return;
		if (peers.empty()) return;

		if (m_ses.alerts().should_post<dht_reply_alert>())
		{
			m_ses.alerts().emplace_alert<dht_reply_alert>(
				get_handle(), int(peers.size()));
		}

		if (torrent_file().priv() || (torrent_file().is_i2p()
			&& !settings().get_bool(settings_pack::allow_i2p_mixed))) return;

		for (auto& p : peers)
			add_peer(p, peer_info::dht);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log() && !peers.empty())
		{
			error_code ec;
			std::string str;
			for (auto const& peer : peers)
			{
				str += peer.address().to_string(ec);
				str += ' ';
			}
			debug_log("DHT add_peer() [ %s] connect-candidates: %d"
				, str.c_str(), m_peer_list
				? m_peer_list->num_connect_candidates() : -1);
		}
#endif

		do_connect_boost();

		update_want_peers();
	}
	catch (...) { handle_exception(); }

#endif

	namespace
	{
		struct announce_state
		{
			explicit announce_state(aux::listen_socket_handle const& s)
				: socket(s) {}

			aux::listen_socket_handle socket;

			// the tier is kept as INT_MAX until we find the first
			// tracker that works, then it's set to that tracker's
			// tier.
			int tier = INT_MAX;

			// have we sent an announce in this tier yet?
			bool sent_announce = false;

			// have we finished sending announces on this listen socket?
			bool done = false;
		};
	}

	void torrent::announce_with_tracker(std::uint8_t e)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(e == tracker_request::stopped || state() != torrent_status::checking_files);
		INVARIANT_CHECK;

		if (m_trackers.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** announce: no trackers");
#endif
			return;
		}

		if (m_abort) e = tracker_request::stopped;

		// having stop_tracker_timeout <= 0 means that there is
		// no need to send any request to trackers or trigger any
		// related logic when the event is stopped
		if (e == tracker_request::stopped
			&& settings().get_int(settings_pack::stop_tracker_timeout) <= 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** announce: event == stopped && stop_tracker_timeout <= 0");
#endif
			return;
		}

		// if we're not announcing to trackers, only allow
		// stopping
		if (e != tracker_request::stopped && !m_announce_to_trackers)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** announce: event != stopped && !m_announce_to_trackers");
#endif
			return;
		}

		// if we're not allowing peers, there's no point in announcing
		if (e != tracker_request::stopped && m_paused)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** announce: event != stopped && m_paused");
#endif
			return;
		}

		TORRENT_ASSERT(!m_paused || e == tracker_request::stopped);

		if (e == tracker_request::none && is_finished() && !is_seed())
			e = tracker_request::paused;

		tracker_request req;
		if (settings().get_bool(settings_pack::apply_ip_filter_to_trackers)
			&& m_apply_ip_filter)
		{
			req.filter = m_ip_filter;
		}

		req.private_torrent = m_torrent_file->priv();

		req.info_hash = m_torrent_file->info_hash();
		req.pid = m_peer_id;
		req.downloaded = m_stat.total_payload_download() - m_total_failed_bytes;
		req.uploaded = m_stat.total_payload_upload();
		req.corrupt = m_total_failed_bytes;
		req.left = value_or(bytes_left(), 16*1024);
#ifdef TORRENT_USE_OPENSSL
		// if this torrent contains an SSL certificate, make sure
		// any SSL tracker presents a certificate signed by it
		req.ssl_ctx = m_ssl_ctx.get();
#endif

		req.redundant = m_total_redundant_bytes;
		// exclude redundant bytes if we should
		if (!settings().get_bool(settings_pack::report_true_downloaded))
		{
			req.downloaded -= m_total_redundant_bytes;

			// if the torrent is complete we know that all incoming pieces will be
			// marked redundant so add them to the redundant count
			// this is mainly needed to cover the case where a torrent has just completed
			// but still has partially downloaded pieces
			// if the incoming pieces are not accounted for it could cause the downloaded
			// amount to exceed the total size of the torrent which upsets some trackers
			if (is_seed())
			{
				for (auto c : m_connections)
				{
					TORRENT_INCREMENT(m_iterating_connections);
					auto const pbp = c->downloading_piece_progress();
					if (pbp.bytes_downloaded > 0)
					{
						req.downloaded -= pbp.bytes_downloaded;
						req.redundant += pbp.bytes_downloaded;
					}
				}
			}
		}
		if (req.downloaded < 0) req.downloaded = 0;

		req.event = e;

		// since sending our IPv4/v6 address to the tracker may be sensitive. Only
		// do that if we're not in anonymous mode and if it's a private torrent
		if (!settings().get_bool(settings_pack::anonymous_mode)
			&& m_torrent_file
			&& m_torrent_file->priv())
		{
			m_ses.for_each_listen_socket([&](aux::listen_socket_handle const& s)
			{
				if (s.is_ssl() != is_ssl_torrent()) return;
				tcp::endpoint const ep = s.get_local_endpoint();
				if (is_any(ep.address())) return;
				if (is_v6(ep))
				{
					if (!is_local(ep.address()) && !is_loopback(ep.address()))
						req.ipv6.push_back(ep.address().to_v6());
				}
				else
				{
					if (!is_local(ep.address()) && !is_loopback(ep.address()))
						req.ipv4.push_back(ep.address().to_v4());
				}
			});
		}

		// if we are aborting. we don't want any new peers
		req.num_want = (req.event == tracker_request::stopped)
			? 0 : settings().get_int(settings_pack::num_want);

		time_point32 const now = aux::time_now32();

		// each listen socket gets it's own announce state
		// so that each one should get at least one announce
		std::vector<announce_state> listen_socket_states;

#ifndef TORRENT_DISABLE_LOGGING
		int idx = -1;
		if (should_log())
		{
			debug_log("*** announce: "
				"[ announce_to_all_tiers: %d announce_to_all_trackers: %d num_trackers: %d ]"
				, settings().get_bool(settings_pack::announce_to_all_tiers)
				, settings().get_bool(settings_pack::announce_to_all_trackers)
				, int(m_trackers.size()));
		}
#endif
		for (auto& ae : m_trackers)
		{
#ifndef TORRENT_DISABLE_LOGGING
			++idx;
#endif
			// update the endpoint list by adding entries for new listen sockets
			// and removing entries for non-existent ones
			std::size_t valid_endpoints = 0;
			m_ses.for_each_listen_socket([&](aux::listen_socket_handle const& s) {
				if (s.is_ssl() != is_ssl_torrent())
					return;
				for (auto& aep : ae.endpoints)
				{
					if (aep.socket != s) continue;
					std::swap(ae.endpoints[valid_endpoints], aep);
					valid_endpoints++;
					return;
				}

				ae.endpoints.emplace_back(s, bool(m_complete_sent));
				std::swap(ae.endpoints[valid_endpoints], ae.endpoints.back());
				valid_endpoints++;
			});

			TORRENT_ASSERT(valid_endpoints <= ae.endpoints.size());
			ae.endpoints.erase(ae.endpoints.begin() + int(valid_endpoints), ae.endpoints.end());

			// if trackerid is not specified for tracker use default one, probably set explicitly
			req.trackerid = ae.trackerid.empty() ? m_trackerid : ae.trackerid;
			req.url = ae.url;

			for (auto& aep : ae.endpoints)
			{
				// do not add code which continues to the next endpoint here!
				// listen_socket_states needs to be populated even if none of the endpoints
				// will be announcing for this tracker
				// otherwise the early bail out when neither announce_to_all_trackers
				// nor announce_to_all_tiers is set may be triggered prematurely

				auto aep_state_iter = std::find_if(listen_socket_states.begin(), listen_socket_states.end()
					, [&](announce_state const& s) { return s.socket == aep.socket; });
				if (aep_state_iter == listen_socket_states.end())
				{
					listen_socket_states.emplace_back(aep.socket);
					aep_state_iter = listen_socket_states.end() - 1;
				}
				announce_state& state = *aep_state_iter;

				if (state.done) continue;

				// if we haven't sent an event=start to the tracker, there's no
				// point in sending an event=stopped
				if (!aep.enabled || (!aep.start_sent && req.event == tracker_request::stopped))
					continue;

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("*** tracker: (%d) [ep: %s ] \"%s\" [ i->tier: %d tier: %d"
						" working: %d limit: %d can: %d sent: %d ]"
						, idx, print_endpoint(aep.local_endpoint).c_str()
						, ae.url.c_str(), ae.tier, state.tier, aep.is_working()
						, ae.fail_limit, aep.can_announce(now, is_seed(), ae.fail_limit), state.sent_announce);
				}
#endif

				if (settings().get_bool(settings_pack::announce_to_all_tiers)
					&& !settings().get_bool(settings_pack::announce_to_all_trackers)
					&& state.sent_announce
					&& ae.tier <= state.tier
					&& state.tier != INT_MAX)
					continue;

				if (ae.tier > state.tier && state.sent_announce
					&& !settings().get_bool(settings_pack::announce_to_all_tiers)) continue;
				if (aep.is_working()) { state.tier = ae.tier; state.sent_announce = false; }
				if (!aep.can_announce(now, is_seed(), ae.fail_limit))
				{
					// this counts
					if (aep.is_working())
					{
						state.sent_announce = true;
						if (!settings().get_bool(settings_pack::announce_to_all_trackers)
							&& !settings().get_bool(settings_pack::announce_to_all_tiers))
						{
							state.done = true;
						}
					}
					continue;
				}

				req.event = e;
				if (req.event == tracker_request::none)
				{
					if (!aep.start_sent) req.event = tracker_request::started;
					else if (!m_complete_sent
						&& !aep.complete_sent
						&& is_seed())
					{
						req.event = tracker_request::completed;
					}
				}

				req.triggered_manually = aep.triggered_manually;
				aep.triggered_manually = false;

#if TORRENT_ABI_VERSION == 1
				req.auth = tracker_login();
#endif
				req.key = tracker_key();

#if TORRENT_USE_I2P
				if (is_i2p())
				{
					req.kind |= tracker_request::i2p;
				}
#endif

				req.outgoing_socket = aep.socket;

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("==> TRACKER REQUEST \"%s\" event: %s abort: %d ssl: %p "
						"port: %d ssl-port: %d fails: %d upd: %d ep: %s"
						, req.url.c_str()
						, (req.event == tracker_request::stopped ? "stopped"
							: req.event == tracker_request::started ? "started" : "")
						, m_abort
#ifdef TORRENT_USE_OPENSSL
						, static_cast<void*>(req.ssl_ctx)
#else
						, static_cast<void*>(nullptr)
#endif
						, m_ses.listen_port()
						, m_ses.ssl_listen_port()
						, aep.fails
						, aep.updating
						, print_endpoint(aep.local_endpoint).c_str());
				}

				// if we're not logging session logs, don't bother creating an
				// observer object just for logging
				if (m_abort && m_ses.should_log())
				{
					auto tl = std::make_shared<aux::tracker_logger>(m_ses);
					m_ses.queue_tracker_request(tracker_request(req), tl);
				}
				else
#endif
				{
					m_ses.queue_tracker_request(tracker_request(req), shared_from_this());
				}

				aep.updating = true;
				aep.next_announce = now;
				aep.min_announce = now;

				if (m_ses.alerts().should_post<tracker_announce_alert>())
				{
					m_ses.alerts().emplace_alert<tracker_announce_alert>(
						get_handle(), aep.local_endpoint, req.url, req.event);
				}

				state.sent_announce = true;
				if (aep.is_working()
					&& !settings().get_bool(settings_pack::announce_to_all_trackers)
					&& !settings().get_bool(settings_pack::announce_to_all_tiers))
				{
					state.done = true;
				}
			}

			if (std::all_of(listen_socket_states.begin(), listen_socket_states.end()
				, [](announce_state const& s) { return s.done; }))
				break;
		}
		update_tracker_timer(now);
	}

	void torrent::scrape_tracker(int idx, bool const user_triggered)
	{
		TORRENT_ASSERT(is_single_thread());
#if TORRENT_ABI_VERSION == 1
		m_last_scrape = aux::time_now32();
#endif

		if (m_trackers.empty()) return;

		if (idx < 0 || idx >= int(m_trackers.size())) idx = m_last_working_tracker;
		if (idx < 0) idx = 0;

		tracker_request req;
		if (settings().get_bool(settings_pack::apply_ip_filter_to_trackers)
			&& m_apply_ip_filter)
			req.filter = m_ip_filter;

		req.info_hash = m_torrent_file->info_hash();
		req.kind |= tracker_request::scrape_request;
		req.url = m_trackers[idx].url;
		req.private_torrent = m_torrent_file->priv();
#if TORRENT_ABI_VERSION == 1
		req.auth = tracker_login();
#endif
		req.key = tracker_key();
		req.triggered_manually = user_triggered;
		m_ses.queue_tracker_request(std::move(req), shared_from_this());
	}

	void torrent::tracker_warning(tracker_request const& req, std::string const& msg)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		announce_entry* ae = find_tracker(req.url);
		tcp::endpoint local_endpoint;
		if (ae)
		{
			for (auto& aep : ae->endpoints)
			{
				if (aep.socket != req.outgoing_socket) continue;
				local_endpoint = aep.local_endpoint;
				aep.message = msg;
				break;
			}
		}

		if (m_ses.alerts().should_post<tracker_warning_alert>())
			m_ses.alerts().emplace_alert<tracker_warning_alert>(get_handle()
				, local_endpoint, req.url, msg);
	}

	void torrent::tracker_scrape_response(tracker_request const& req
		, int const complete, int const incomplete, int const downloaded, int /* downloaders */)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;
		TORRENT_ASSERT(0 != (req.kind & tracker_request::scrape_request));

		announce_entry* ae = find_tracker(req.url);
		tcp::endpoint local_endpoint;
		if (ae)
		{
			announce_endpoint* aep = ae->find_endpoint(req.outgoing_socket);
			if (aep)
			{
				local_endpoint = aep->local_endpoint;
				if (incomplete >= 0) aep->scrape_incomplete = incomplete;
				if (complete >= 0) aep->scrape_complete = complete;
				if (downloaded >= 0) aep->scrape_downloaded = downloaded;

				update_scrape_state();
			}
		}

		// if this was triggered manually we need to post this unconditionally,
		// since the client expects a response from its action, regardless of
		// whether all tracker events have been enabled by the alert mask
		if (m_ses.alerts().should_post<scrape_reply_alert>()
			|| req.triggered_manually)
		{
			m_ses.alerts().emplace_alert<scrape_reply_alert>(
				get_handle(), local_endpoint, incomplete, complete, req.url);
		}
	}

	void torrent::update_scrape_state()
	{
		// loop over all trackers and find the largest numbers for each scrape field
		// then update the torrent-wide understanding of number of downloaders and seeds
		int complete = -1;
		int incomplete = -1;
		int downloaded = -1;
		for (auto const& t : m_trackers)
		{
			for (auto const& aep : t.endpoints)
			{
				complete = std::max(aep.scrape_complete, complete);
				incomplete = std::max(aep.scrape_incomplete, incomplete);
				downloaded = std::max(aep.scrape_downloaded, downloaded);
			}
		}

		if ((complete >= 0 && int(m_complete) != complete)
			|| (incomplete >= 0 && int(m_incomplete) != incomplete)
			|| (downloaded >= 0 && int(m_downloaded) != downloaded))
			state_updated();

		if (int(m_complete) != complete
			|| int(m_incomplete) != incomplete
			|| int(m_downloaded) != downloaded)
		{
			m_complete = std::uint32_t(complete);
			m_incomplete = std::uint32_t(incomplete);
			m_downloaded = std::uint32_t(downloaded);

			update_auto_sequential();

			// these numbers are cached in the resume data
			set_need_save_resume();
		}
	}

	void torrent::tracker_response(
		tracker_request const& r
		, address const& tracker_ip // this is the IP we connected to
		, std::list<address> const& tracker_ips // these are all the IPs it resolved to
		, struct tracker_response const& resp)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;
		TORRENT_ASSERT(0 == (r.kind & tracker_request::scrape_request));

		// if the tracker told us what our external IP address is, record it with
		// out external IP counter (and pass along the IP of the tracker to know
		// who to attribute this vote to)
		if (resp.external_ip != address() && !is_any(tracker_ip))
			m_ses.set_external_address(r.outgoing_socket.get_local_endpoint()
				, resp.external_ip
				, aux::session_interface::source_tracker, tracker_ip);

		time_point32 const now = aux::time_now32();

		auto const interval = std::max(resp.interval, seconds32(
			settings().get_int(settings_pack::min_announce_interval)));

		announce_entry* ae = find_tracker(r.url);
		tcp::endpoint local_endpoint;
		if (ae)
		{
#if TORRENT_ABI_VERSION == 1
			if (!ae->complete_sent && r.event == tracker_request::completed)
				ae->complete_sent = true;
#endif
			announce_endpoint* aep = ae->find_endpoint(r.outgoing_socket);
			if (aep)
			{
				local_endpoint = aep->local_endpoint;
				if (resp.incomplete >= 0) aep->scrape_incomplete = resp.incomplete;
				if (resp.complete >= 0) aep->scrape_complete = resp.complete;
				if (resp.downloaded >= 0) aep->scrape_downloaded = resp.downloaded;
				if (!aep->start_sent && r.event == tracker_request::started)
					aep->start_sent = true;
				if (!aep->complete_sent && r.event == tracker_request::completed)
				{
					aep->complete_sent = true;
					// we successfully reported event=completed to one tracker. Don't
					// send it to any other ones from now on (there may be other
					// announces outstanding right now though)
					m_complete_sent = true;
				}
				ae->verified = true;
				aep->next_announce = now + interval;
				aep->min_announce = now + resp.min_interval;
				aep->updating = false;
				aep->fails = 0;
				aep->last_error.clear();
				aep->message = !resp.warning_message.empty() ? resp.warning_message : std::string();
				int tracker_index = int(ae - m_trackers.data());
				m_last_working_tracker = std::int8_t(tracker_index);

				if ((!resp.trackerid.empty()) && (ae->trackerid != resp.trackerid))
				{
					ae->trackerid = resp.trackerid;
					if (m_ses.alerts().should_post<trackerid_alert>())
						m_ses.alerts().emplace_alert<trackerid_alert>(get_handle()
							, aep->local_endpoint, r.url, resp.trackerid);
				}

				update_scrape_state();
			}
		}
		update_tracker_timer(now);

#if TORRENT_ABI_VERSION == 1
		if (resp.complete >= 0 && resp.incomplete >= 0)
			m_last_scrape = aux::time_now32();
#endif

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			std::string resolved_to;
			for (auto const& i : tracker_ips)
			{
				resolved_to += i.to_string();
				resolved_to += ", ";
			}
			debug_log("TRACKER RESPONSE [ interval: %d | min-interval: %d | "
				"external ip: %s | resolved to: %s | we connected to: %s ]"
				, interval.count()
				, resp.min_interval.count()
				, print_address(resp.external_ip).c_str()
				, resolved_to.c_str()
				, print_address(tracker_ip).c_str());
		}
#else
		TORRENT_UNUSED(tracker_ips);
#endif

		// for each of the peers we got from the tracker
		for (auto const& i : resp.peers)
		{
			// don't make connections to ourself
			if (i.pid == m_peer_id)
				continue;

#if TORRENT_USE_I2P
			if (r.i2pconn && string_ends_with(i.hostname, ".i2p"))
			{
				// this is an i2p name, we need to use the SAM connection
				// to do the name lookup
				if (string_ends_with(i.hostname, ".b32.i2p"))
				{
					ADD_OUTSTANDING_ASYNC("torrent::on_i2p_resolve");
					r.i2pconn->async_name_lookup(i.hostname.c_str()
						, std::bind(&torrent::on_i2p_resolve
						, shared_from_this(), _1, _2));
				}
				else
				{
					torrent_state st = get_peer_list_state();
					need_peer_list();
					if (m_peer_list->add_i2p_peer(i.hostname.c_str (), peer_info::tracker, {}, &st))
						state_updated();
					peers_erased(st.erased);
				}
			}
			else
#endif
			{
				ADD_OUTSTANDING_ASYNC("torrent::on_peer_name_lookup");
				m_ses.get_resolver().async_resolve(i.hostname, resolver_interface::abort_on_shutdown
					, std::bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i.port));
			}
		}

		// there are 2 reasons to allow local IPs to be returned from a
		// non-local tracker
		// 1. retrackers are popular in russia, where an ISP runs a tracker within
		//    the AS (but not on the local network) giving out peers only from the
		//    local network
		// 2. it might make sense to have a tracker extension in the future where
		//    trackers records a peer's internal and external IP, and match up
		//    peers on the same local network

		bool need_update = false;
		for (auto const& i : resp.peers4)
		{
			tcp::endpoint const a(address_v4(i.ip), i.port);
			need_update |= bool(add_peer(a, peer_info::tracker) != nullptr);
		}

		for (auto const& i : resp.peers6)
		{
			tcp::endpoint const a(address_v6(i.ip), i.port);
			need_update |= bool(add_peer(a, peer_info::tracker) != nullptr);
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log() && (!resp.peers4.empty() || !resp.peers6.empty()))
		{
			error_code ec;
			std::string str;
			for (auto const& peer : resp.peers4)
			{
				str += address_v4(peer.ip).to_string(ec);
				str += ' ';
			}
			for (auto const& peer : resp.peers6)
			{
				str += address_v6(peer.ip).to_string(ec);
				str += ' ';
			}
			debug_log("tracker add_peer() [ %s] connect-candidates: %d"
				, str.c_str(), m_peer_list
				? m_peer_list->num_connect_candidates() : -1);
		}
#endif
		if (need_update) state_updated();

		update_want_peers();

		// post unconditionally if the announce was triggered manually
		if (m_ses.alerts().should_post<tracker_reply_alert>()
			|| r.triggered_manually)
		{
			m_ses.alerts().emplace_alert<tracker_reply_alert>(
				get_handle(), local_endpoint, int(resp.peers.size() + resp.peers4.size())
				+ int(resp.peers6.size())
				, r.url);
		}

		do_connect_boost();

		state_updated();
	}

	void torrent::update_auto_sequential()
	{
		if (!settings().get_bool(settings_pack::auto_sequential))
		{
			m_auto_sequential = false;
			return;
		}

		if (num_peers() - m_num_connecting < 10)
		{
			// there are too few peers. Be conservative and don't assume it's
			// well seeded until we can connect to more peers
			m_auto_sequential = false;
			return;
		}

		// if there are at least 10 seeds, and there are 10 times more
		// seeds than downloaders, enter sequential download mode
		// (for performance)
		int const downloaders = num_downloaders();
		int const seeds = num_seeds();
		m_auto_sequential = downloaders * 10 <= seeds
			&& seeds > 9;
	}

	void torrent::do_connect_boost()
	{
		if (m_connect_boost_counter == 0) return;

		// this is the first tracker response for this torrent
		// instead of waiting one second for session_impl::on_tick()
		// to be called, connect to a few peers immediately
		int conns = std::min(int(m_connect_boost_counter)
			, settings().get_int(settings_pack::connections_limit) - m_ses.num_connections());

		if (conns == 0) return;

		// if we don't know of any peers
		if (!m_peer_list) return;

		while (want_peers() && conns > 0)
		{
			TORRENT_ASSERT(m_connect_boost_counter > 0);
			--conns;
			--m_connect_boost_counter;
			torrent_state st = get_peer_list_state();
			torrent_peer* p = m_peer_list->connect_one_peer(m_ses.session_time(), &st);
			peers_erased(st.erased);
			inc_stats_counter(counters::connection_attempt_loops, st.loop_counter);
			if (p == nullptr)
			{
				update_want_peers();
				continue;
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				external_ip const& external = m_ses.external_address();
				debug_log(" *** FOUND CONNECTION CANDIDATE ["
					" ip: %s rank: %u external: %s t: %d ]"
					, print_endpoint(p->ip()).c_str()
					, p->rank(external, m_ses.listen_port())
					, print_address(external.external_address(p->address())).c_str()
					, int(m_ses.session_time() - p->last_connected));
			}
#endif

			if (!connect_to_peer(p))
			{
				m_peer_list->inc_failcount(p);
				update_want_peers();
			}
			else
			{
				// increase m_ses.m_boost_connections for each connection
				// attempt. This will be deducted from the connect speed
				// the next time session_impl::on_tick() is triggered
				m_ses.inc_boost_connections();
				update_want_peers();
			}
		}

		if (want_peers()) m_ses.prioritize_connections(shared_from_this());
	}

	// this is the entry point for the client to force a re-announce. It's
	// considered a client-initiated announce (as opposed to the regular ones,
	// issued by libtorrent)
	void torrent::force_tracker_request(time_point const t, int const tracker_idx
		, reannounce_flags_t const flags)
	{
		TORRENT_ASSERT_PRECOND((tracker_idx >= 0
			&& tracker_idx < int(m_trackers.size()))
			|| tracker_idx == -1);

		if (is_paused()) return;
		if (tracker_idx == -1)
		{
			for (auto& e : m_trackers)
			{
				for (auto& aep : e.endpoints)
				{
					aep.next_announce = (flags & torrent_handle::ignore_min_interval)
						? time_point_cast<seconds32>(t) + seconds32(1)
						: std::max(time_point_cast<seconds32>(t), aep.min_announce) + seconds32(1);
					aep.min_announce = aep.next_announce;
					aep.triggered_manually = true;
				}
			}
		}
		else
		{
			if (tracker_idx < 0 || tracker_idx >= int(m_trackers.size()))
				return;
			announce_entry& e = m_trackers[tracker_idx];
			for (auto& aep : e.endpoints)
			{
				aep.next_announce = (flags & torrent_handle::ignore_min_interval)
					? time_point_cast<seconds32>(t) + seconds32(1)
					: std::max(time_point_cast<seconds32>(t), aep.min_announce) + seconds32(1);
				aep.min_announce = aep.next_announce;
				aep.triggered_manually = true;
			}
		}
		update_tracker_timer(aux::time_now32());
	}

#if TORRENT_ABI_VERSION == 1
	void torrent::set_tracker_login(std::string const& name
		, std::string const& pw)
	{
		m_username = name;
		m_password = pw;
	}
#endif

#if TORRENT_USE_I2P
	void torrent::on_i2p_resolve(error_code const& ec, char const* dest) try
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		COMPLETE_ASYNC("torrent::on_i2p_resolve");
#ifndef TORRENT_DISABLE_LOGGING
		if (ec && should_log())
			debug_log("i2p_resolve error: %s", ec.message().c_str());
#endif
		if (ec || m_abort || m_ses.is_aborted()) return;

		need_peer_list();
		torrent_state st = get_peer_list_state();
		if (m_peer_list->add_i2p_peer(dest, peer_info::tracker, {}, &st))
			state_updated();
		peers_erased(st.erased);
	}
	catch (...) { handle_exception(); }
#endif

	void torrent::on_peer_name_lookup(error_code const& e
		, std::vector<address> const& host_list, int const port) try
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		COMPLETE_ASYNC("torrent::on_peer_name_lookup");

#ifndef TORRENT_DISABLE_LOGGING
		if (e && should_log())
			debug_log("peer name lookup error: %s", e.message().c_str());
#endif

		if (e || m_abort || host_list.empty() || m_ses.is_aborted()) return;

		// TODO: add one peer per IP the hostname resolves to
		tcp::endpoint host(host_list.front(), std::uint16_t(port));

		if (m_ip_filter && m_ip_filter->access(host.address()) & ip_filter::blocked)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				error_code ec;
				debug_log("blocked ip from tracker: %s", host.address().to_string(ec).c_str());
			}
#endif
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, host, peer_blocked_alert::ip_filter);
			return;
		}

		if (add_peer(host, peer_info::tracker))
		{
			state_updated();

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				error_code ec;
				debug_log("name-lookup add_peer() [ %s ] connect-candidates: %d"
					, host.address().to_string(ec).c_str()
					, m_peer_list ? m_peer_list->num_connect_candidates() : -1);
			}
#endif
		}
		update_want_peers();
	}
	catch (...) { handle_exception(); }

	boost::optional<std::int64_t> torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return {};
		TORRENT_ASSERT(m_torrent_file->num_pieces() > 0);
		if (m_seed_mode) return std::int64_t(0);
		if (!has_picker()) return is_seed() ? std::int64_t(0) : m_torrent_file->total_size();

		std::int64_t left
			= m_torrent_file->total_size()
			- std::int64_t(m_picker->num_passed()) * m_torrent_file->piece_length();

		// if we have the last piece, we may have subtracted too much, as it can
		// be smaller than the normal piece size.
		// we have to correct it
		piece_index_t const last_piece = prev(m_torrent_file->end_piece());
		if (m_picker->has_piece_passed(last_piece))
		{
			left += m_torrent_file->piece_length() - m_torrent_file->piece_size(last_piece);
		}

		return left;
	}

	// we assume the last block is never a pad block. Should be a fairly
	// safe assumption, and you just get a few kiB off if it is
	std::int64_t calc_bytes(file_storage const& fs, piece_count const& pc)
	{
		// it's an impossible combination to have 0 pieces, but still have one of them be the last piece
		TORRENT_ASSERT(!(pc.num_pieces == 0 && pc.last_piece == true));

		// if we have 0 pieces, we can't have any pad blocks either
		TORRENT_ASSERT(!(pc.num_pieces == 0 && pc.pad_blocks > 0));

		// if we have all pieces, we must also have the last one
		TORRENT_ASSERT(!(pc.num_pieces == fs.num_pieces() && pc.last_piece == false));
		int const block_size = std::min(default_block_size, fs.piece_length());

		// every block should not be a pad block
		TORRENT_ASSERT(pc.pad_blocks <= std::int64_t(pc.num_pieces) * fs.piece_length() / block_size);

		return std::int64_t(pc.num_pieces) * fs.piece_length()
			- (pc.last_piece ? fs.piece_length() - fs.piece_size(fs.last_piece()) : 0)
			- std::int64_t(pc.pad_blocks) * block_size;
	}

	// fills in total_wanted, total_wanted_done and total_done
// TODO: 3 this could probably be pulled out into a free function
	void torrent::bytes_done(torrent_status& st, status_flags_t const flags) const
	{
		INVARIANT_CHECK;

		st.total_done = 0;
		st.total_wanted_done = 0;
		st.total_wanted = m_torrent_file->total_size();

		TORRENT_ASSERT(st.total_wanted >= m_padding_blocks * default_block_size);
		TORRENT_ASSERT(st.total_wanted >= 0);

		TORRENT_ASSERT(!valid_metadata() || m_torrent_file->num_pieces() > 0);
		if (!valid_metadata()) return;

		TORRENT_ASSERT(st.total_wanted >= std::int64_t(m_torrent_file->piece_length())
			* (m_torrent_file->num_pieces() - 1));

		// if any piece hash fails, we'll be taken out of seed mode
		// and m_seed_mode will be false
		if (m_seed_mode || is_seed())
		{
			st.total_done = m_torrent_file->total_size()
				- m_padding_blocks * default_block_size;
			st.total_wanted_done = st.total_done;
			st.total_wanted = st.total_done;
			return;
		}
		else if (!has_picker())
		{
			st.total_done = 0;
			st.total_wanted_done = 0;
			st.total_wanted = m_torrent_file->total_size()
				- m_padding_blocks * default_block_size;
			return;
		}

		TORRENT_ASSERT(has_picker());

		file_storage const& files = m_torrent_file->files();

		st.total_wanted = calc_bytes(files, m_picker->want());
		st.total_wanted_done = calc_bytes(files, m_picker->have_want());
		st.total_done = calc_bytes(files, m_picker->have());
		st.total = calc_bytes(files, m_picker->all_pieces());

		TORRENT_ASSERT(st.total_done <= calc_bytes(files, m_picker->all_pieces()));
		TORRENT_ASSERT(st.total_wanted <= calc_bytes(files, m_picker->all_pieces()));

		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_wanted >= 0);
		TORRENT_ASSERT(st.total_wanted >= st.total_wanted_done);
		TORRENT_ASSERT(st.total_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		// this is expensive, we might not want to do it all the time
		if (!(flags & torrent_handle::query_accurate_download_counters)) return;

		// to get higher accuracy of the download progress, include
		// blocks from currently downloading pieces as well
		std::vector<piece_picker::downloading_piece> const dl_queue
			= m_picker->get_download_queue();

		// look at all unfinished pieces and add the completed
		// blocks to our 'done' counter
		for (auto i = dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			piece_index_t const index = i->index;

			// completed pieces are already accounted for
			if (m_picker->have_piece(index)) continue;

			TORRENT_ASSERT(i->finished + i->writing <= m_picker->blocks_in_piece(index));
			TORRENT_ASSERT(i->finished + i->writing >= m_picker->pad_blocks_in_piece(index));

			int const blocks = i->finished + i->writing - m_picker->pad_blocks_in_piece(index);
			TORRENT_ASSERT(blocks >= 0);

			auto const additional_bytes = std::int64_t(blocks) * block_size();
			st.total_done += additional_bytes;
			if (m_picker->piece_priority(index) > dont_download)
				st.total_wanted_done += additional_bytes;
		}
	}

	void torrent::on_piece_verified(piece_index_t const piece
		, sha1_hash const& piece_hash, storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;
		if (m_deleted) return;

		bool const passed = settings().get_bool(settings_pack::disable_hash_checks)
			|| (!error && sha1_hash(piece_hash) == m_torrent_file->hash_for_piece(piece));

		bool const disk_error = !passed && error;

		if (disk_error) handle_disk_error("piece_verified", error);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("*** PIECE_FINISHED [ p: %d | chk: %s | size: %d ]"
				, static_cast<int>(piece), passed ? "passed" : disk_error ? "disk failed" : "failed"
				, m_torrent_file->piece_size(piece));
		}
#endif
		TORRENT_ASSERT(valid_metadata());

		// if we're a seed we don't have a picker
		// and we also don't have to do anything because
		// we already have this piece
		if (!has_picker() && m_have_all) return;

		need_picker();

		TORRENT_ASSERT(!m_picker->have_piece(piece));

		state_updated();

		// even though the piece passed the hash-check
		// it might still have failed being written to disk
		// if so, piece_picker::write_failed() has been
		// called, and the piece is no longer finished.
		// in this case, we have to ignore the fact that
		// it passed the check
		if (!m_picker->is_piece_finished(piece)) return;

		if (disk_error)
		{
			update_gauge();
		}
		else if (passed)
		{
			// the following call may cause picker to become invalid
			// in case we just became a seed
			piece_passed(piece);
			// if we're in seed mode, we just acquired this piece
			// mark it as verified
			if (m_seed_mode) verified(piece);
		}
		else
		{
			// piece_failed() will restore the piece
			piece_failed(piece);
		}
	}
	catch (...) { handle_exception(); }

	void torrent::add_suggest_piece(piece_index_t const index)
	{
		TORRENT_ASSERT(settings().get_int(settings_pack::suggest_mode)
			== settings_pack::suggest_read_cache);

		// when we care about suggest mode, we keep the piece picker
		// around to track piece availability
		need_picker();
		int const peers = std::max(num_peers(), 1);
		int const availability = m_picker->get_availability(index) * 100 / peers;

		m_suggest_pieces.add_piece(index, availability
			, settings().get_int(settings_pack::max_suggest_pieces));
	}

	// this is called once we have completely downloaded piece
	// 'index', its hash has been verified. It's also called
	// during initial file check when we find a piece whose hash
	// is correct
	void torrent::we_have(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!has_picker() || m_picker->has_piece_passed(index));

		inc_stats_counter(counters::num_have_pieces);

		// at this point, we have the piece for sure. It has been
		// successfully written to disk. We may announce it to peers
		// (unless it has already been announced through predictive_piece_announce
		// feature).
		bool announce_piece = true;
#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
		auto const it = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (it != m_predictive_pieces.end() && *it == index)
		{
			// this means we've already announced the piece
			announce_piece = false;
			m_predictive_pieces.erase(it);
		}
#endif

		// make a copy of the peer list since peers
		// may disconnect while looping
		for (auto c : m_connections)
		{
			auto p = c->self();

			// received_piece will check to see if we're still interested
			// in this peer, and if neither of us is interested in the other,
			// disconnect it.
			p->received_piece(index);
			if (p->is_disconnecting()) continue;

			// if we're not announcing the piece, it means we
			// already have, and that we might have received
			// a request for it, and not sending it because
			// we were waiting to receive the piece, now that
			// we have received it, try to send stuff (fill_send_buffer)
			if (announce_piece) p->announce_piece(index);
			else p->fill_send_buffer();
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			ext->on_piece_pass(index);
		}
#endif

		// since this piece just passed, we might have
		// become uninterested in some peers where this
		// was the last piece we were interested in
		// update_interest may disconnect the peer and
		// invalidate the iterator
		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			// if we're not interested already, no need to check
			if (!p->is_interesting()) continue;
			// if the peer doesn't have the piece we just got, it
			// shouldn't affect our interest
			if (!p->has_piece(index)) continue;
			p->update_interest();
		}

		set_need_save_resume();
		state_updated();

		if (m_ses.alerts().should_post<piece_finished_alert>())
			m_ses.alerts().emplace_alert<piece_finished_alert>(get_handle(), index);

		// update m_file_progress (if we have one)
		m_file_progress.update(m_torrent_file->files(), index
			, [this](file_index_t const file_index)
			{
				if (m_ses.alerts().should_post<file_completed_alert>())
				{
					// this file just completed, post alert
					m_ses.alerts().emplace_alert<file_completed_alert>(
						get_handle(), file_index);
				}
			});

#ifndef TORRENT_DISABLE_STREAMING
		remove_time_critical_piece(index, true);
#endif

		if (is_downloading_state(m_state))
		{
			if (m_state != torrent_status::finished
				&& m_state != torrent_status::seeding
				&& is_finished())
			{
				// torrent finished
				// i.e. all the pieces we're interested in have
				// been downloaded. Release the files (they will open
				// in read only mode if needed)
				finished();
				// if we just became a seed, picker is now invalid, since it
				// is deallocated by the torrent once it starts seeding
			}

			m_last_download = aux::time_now32();

#ifndef TORRENT_DISABLE_SHARE_MODE
			if (m_share_mode)
				recalc_share_mode();
#endif
		}
	}

	// this is called when the piece hash is checked as correct. Note
	// that the piece picker and the torrent won't necessarily consider
	// us to have this piece yet, since it might not have been flushed
	// to disk yet. Only if we have predictive_piece_announce on will
	// we announce this piece to peers at this point.
	void torrent::piece_passed(piece_index_t const index)
	{
//		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_picker->has_piece_passed(index));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
			debug_log("PIECE_PASSED (%d)", num_passed());
#endif

//		std::fprintf(stderr, "torrent::piece_passed piece:%d\n", index);

		TORRENT_ASSERT(index >= piece_index_t(0));
		TORRENT_ASSERT(index < m_torrent_file->end_piece());

		set_need_save_resume();

		inc_stats_counter(counters::num_piece_passed);

#ifndef TORRENT_DISABLE_STREAMING
		remove_time_critical_piece(index, true);
#endif

		if (settings().get_int(settings_pack::suggest_mode)
			== settings_pack::suggest_read_cache)
		{
			// we just got a new piece. Chances are that it's actually the
			// rarest piece (since we're likely to download pieces rarest first)
			// if it's rarer than any other piece that we currently suggest, insert
			// it in the suggest set and pop the last one out
			add_suggest_piece(index);
		}

		std::vector<torrent_peer*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<torrent_peer*> peers;

		// these torrent_peer pointers are owned by m_peer_list and they may be
		// invalidated if a peer disconnects. We cannot keep them across any
		// significant operations, but we should use them right away
		// ignore nullptrs
		std::remove_copy(downloaders.begin(), downloaders.end()
			, std::inserter(peers, peers.begin()), static_cast<torrent_peer*>(nullptr));

		for (auto p : peers)
		{
			TORRENT_ASSERT(p != nullptr);
			if (p == nullptr) continue;
			TORRENT_ASSERT(p->in_use);
			p->on_parole = false;
			int trust_points = p->trust_points;
			++trust_points;
			if (trust_points > 8) trust_points = 8;
			p->trust_points = trust_points;
			if (p->connection)
			{
				auto* peer = static_cast<peer_connection*>(p->connection);
				TORRENT_ASSERT(peer->m_in_use == 1337);
				peer->received_valid_data(index);
			}
		}
		// announcing a piece may invalidate the torrent_peer pointers
		// so we can't use them anymore

		downloaders.clear();
		peers.clear();

		// make the disk cache flush the piece to disk
		if (m_storage)
			m_ses.disk_thread().async_flush_piece(m_storage, index);
		m_picker->piece_passed(index);
		update_gauge();
		we_have(index);
		update_want_tick();
	}

#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
	// we believe we will complete this piece very soon
	// announce it to peers ahead of time to eliminate the
	// round-trip times involved in announcing it, requesting it
	// and sending it
	// TODO: 2 use chrono type for time duration
	void torrent::predicted_have_piece(piece_index_t const index, int const milliseconds)
	{
		auto const i = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (i != m_predictive_pieces.end() && *i == index) return;

		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
#ifndef TORRENT_DISABLE_LOGGING
			p->peer_log(peer_log_alert::outgoing, "PREDICTIVE_HAVE", "piece: %d expected in %d ms"
				, static_cast<int>(index), milliseconds);
#else
			TORRENT_UNUSED(milliseconds);
#endif
			p->announce_piece(index);
		}

		m_predictive_pieces.insert(i, index);
	}
#endif

	void torrent::piece_failed(piece_index_t const index)
	{
		// if the last piece fails the peer connection will still
		// think that it has received all of it until this function
		// resets the download queue. So, we cannot do the
		// invariant check here since it assumes:
		// (total_done == m_torrent_file->total_size()) => is_seed()
		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= piece_index_t(0));
		TORRENT_ASSERT(index < m_torrent_file->end_piece());

		inc_stats_counter(counters::num_piece_failed);

#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
		auto const it = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (it != m_predictive_pieces.end() && *it == index)
		{
			for (auto p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				// send reject messages for
				// potential outstanding requests to this piece
				p->reject_piece(index);
				// let peers that support the dont-have message
				// know that we don't actually have this piece
				p->write_dont_have(index);
			}
			m_predictive_pieces.erase(it);
		}
#endif
		// increase the total amount of failed bytes
		add_failed_bytes(m_torrent_file->piece_size(index));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			ext->on_piece_failed(index);
		}
#endif

		std::vector<torrent_peer*> downloaders;
		if (m_picker)
			m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<torrent_peer*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#if TORRENT_USE_ASSERTS
		for (auto const& p : downloaders)
		{
			if (p && p->connection)
			{
				auto* peer = static_cast<peer_connection*>(p->connection);
				peer->piece_failed = true;
			}
		}
#endif

		// did we receive this piece from a single peer?
		bool const single_peer = peers.size() == 1;

		for (auto p : peers)
		{
			if (p == nullptr) continue;
			TORRENT_ASSERT(p->in_use);
			bool allow_disconnect = true;
			if (p->connection)
			{
				auto* peer = static_cast<peer_connection*>(p->connection);
				TORRENT_ASSERT(peer->m_in_use == 1337);

				// the peer implementation can ask not to be disconnected.
				// this is used for web seeds for instance, to instead of
				// disconnecting, mark the file as not being had.
				allow_disconnect = peer->received_invalid_data(index, single_peer);
			}

			if (settings().get_bool(settings_pack::use_parole_mode))
				p->on_parole = true;

			int hashfails = p->hashfails;
			int trust_points = p->trust_points;

			// we decrease more than we increase, to keep the
			// allowed failed/passed ratio low.
			trust_points -= 2;
			++hashfails;
			if (trust_points < -7) trust_points = -7;
			p->trust_points = trust_points;
			if (hashfails > 255) hashfails = 255;
			p->hashfails = std::uint8_t(hashfails);

			// either, we have received too many failed hashes
			// or this was the only peer that sent us this piece.
			// if we have failed more than 3 pieces from this peer,
			// don't trust it regardless.
			if (p->trust_points <= -7
				|| (single_peer && allow_disconnect))
			{
				// we don't trust this peer anymore
				// ban it.
				if (m_ses.alerts().should_post<peer_ban_alert>())
				{
					peer_id const pid = p->connection
						? p->connection->pid() : peer_id();
					m_ses.alerts().emplace_alert<peer_ban_alert>(
						get_handle(), p->ip(), pid);
				}

				// mark the peer as banned
				ban_peer(p);
				update_want_peers();
				inc_stats_counter(counters::banned_for_hash_failure);

				if (p->connection)
				{
					auto* peer = static_cast<peer_connection*>(p->connection);
#ifndef TORRENT_DISABLE_LOGGING
					if (should_log())
					{
						debug_log("*** BANNING PEER: \"%s\" Too many corrupt pieces"
							, print_endpoint(p->ip()).c_str());
					}
					peer->peer_log(peer_log_alert::info, "BANNING_PEER", "Too many corrupt pieces");
#endif
					peer->disconnect(errors::too_many_corrupt_pieces, operation_t::bittorrent);
				}
			}
		}

		// If m_storage isn't set here, it means we're shutting down
		if (m_storage)
		{
			// it doesn't make much sense to fail to hash a piece
			// without having a storage associated with the torrent.
			// restoring the piece in the piece picker without calling
			// clear piece on the disk thread will make them out of
			// sync, and if we try to write more blocks to this piece
			// the disk thread will barf, because it hasn't been cleared
			TORRENT_ASSERT(m_storage);

			// don't allow picking any blocks from this piece
			// until we're done synchronizing with the disk threads.
			m_picker->lock_piece(index);

			// don't do this until after the plugins have had a chance
			// to read back the blocks that failed, for blame purposes
			// this way they have a chance to hit the cache
			m_ses.disk_thread().async_clear_piece(m_storage, index
				, std::bind(&torrent::on_piece_sync, shared_from_this(), _1));
		}
		else
		{
			TORRENT_ASSERT(m_abort);
			// it doesn't really matter what we do
			// here, since we're about to destruct the
			// torrent anyway.
			on_piece_sync(index);
		}

#if TORRENT_USE_ASSERTS
		for (auto const& p : downloaders)
		{
			if (p && p->connection)
			{
				auto* peer = static_cast<peer_connection*>(p->connection);
				peer->piece_failed = false;
			}
		}
#endif
	}

	void torrent::peer_is_interesting(peer_connection& c)
	{
		INVARIANT_CHECK;

		// no peer should be interesting if we're finished
		TORRENT_ASSERT(!is_finished());

		if (c.in_handshake()) return;
		c.send_interested();
		if (c.has_peer_choked()
			&& c.allowed_fast().empty())
			return;

		if (request_a_block(*this, c))
			inc_stats_counter(counters::interesting_piece_picks);
		c.send_block_requests();
	}

	void torrent::on_piece_sync(piece_index_t const piece) try
	{
		// the user may have called force_recheck, which clears
		// the piece picker
		if (!has_picker()) return;

		// unlock the piece and restore it, as if no block was
		// ever downloaded for it.
		m_picker->restore_piece(piece);

		if (m_ses.alerts().should_post<hash_failed_alert>())
			m_ses.alerts().emplace_alert<hash_failed_alert>(get_handle(), piece);

		// we have to let the piece_picker know that
		// this piece failed the check as it can restore it
		// and mark it as being interesting for download
		TORRENT_ASSERT(!m_picker->have_piece(piece));

		// loop over all peers and re-request potential duplicate
		// blocks to this piece
		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			for (auto const& b : p->download_queue())
			{
				if (b.timed_out || b.not_wanted) continue;
				if (b.block.piece_index != piece) continue;
				m_picker->mark_as_downloading(b.block, p->peer_info_struct()
					, p->picker_options());
			}
			for (auto const& b : p->request_queue())
			{
				if (b.block.piece_index != piece) continue;
				m_picker->mark_as_downloading(b.block, p->peer_info_struct()
					, p->picker_options());
			}
		}
	}
	catch (...) { handle_exception(); }

	void torrent::peer_has(piece_index_t const index, peer_connection const* peer)
	{
		if (has_picker())
		{
			torrent_peer* pp = peer->peer_info_struct();
			m_picker->inc_refcount(index, pp);
		}
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
	}

	// when we get a bitfield message, this is called for that piece
	void torrent::peer_has(typed_bitfield<piece_index_t> const& bits
		, peer_connection const* peer)
	{
		if (has_picker())
		{
			TORRENT_ASSERT(bits.size() == torrent_file().num_pieces());
			torrent_peer* pp = peer->peer_info_struct();
			m_picker->inc_refcount(bits, pp);
		}
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
	}

	void torrent::peer_has_all(peer_connection const* peer)
	{
		if (has_picker())
		{
			torrent_peer* pp = peer->peer_info_struct();
			m_picker->inc_refcount_all(pp);
		}
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
	}

	void torrent::peer_lost(typed_bitfield<piece_index_t> const& bits
		, peer_connection const* peer)
	{
		if (has_picker())
		{
			TORRENT_ASSERT(bits.size() == torrent_file().num_pieces());
			torrent_peer* pp = peer->peer_info_struct();
			m_picker->dec_refcount(bits, pp);
		}
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
	}

	void torrent::peer_lost(piece_index_t const index, peer_connection const* peer)
	{
		if (m_picker)
		{
			torrent_peer* pp = peer->peer_info_struct();
			m_picker->dec_refcount(index, pp);
		}
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
	}

	void torrent::abort()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;

		m_abort = true;
		update_want_peers();
		update_want_tick();
		update_want_scrape();
		update_gauge();
		stop_announcing();

		// remove from download queue
		m_ses.set_queue_position(this, queue_position_t{-1});

		if (m_peer_class > peer_class_t{0})
		{
			remove_class(m_ses.peer_classes(), m_peer_class);
			m_ses.peer_classes().decref(m_peer_class);
			m_peer_class = peer_class_t{0};
		}

		error_code ec;
		m_inactivity_timer.cancel(ec);

#ifndef TORRENT_DISABLE_LOGGING
		log_to_all_peers("aborting");
#endif

		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all(errors::torrent_aborted, operation_t::bittorrent);

		// make sure to destruct the peers immediately
		on_remove_peers();
		TORRENT_ASSERT(m_connections.empty());

		// post a message to the main thread to destruct
		// the torrent object from there
		if (m_storage)
		{
			try {
				m_ses.disk_thread().async_stop_torrent(m_storage
					, std::bind(&torrent::on_torrent_aborted, shared_from_this()));
			}
			catch (std::exception const& e)
			{
				TORRENT_UNUSED(e);
				m_storage.reset();
#ifndef TORRENT_DISABLE_LOGGING
				debug_log("Failed to flush disk cache: %s", e.what());
#endif
				// clients may rely on this alert to be posted, so it's probably a
				// good idea to post it here, even though we failed
				// TODO: 3 should this alert have an error code in it?
				if (alerts().should_post<cache_flushed_alert>())
					alerts().emplace_alert<cache_flushed_alert>(get_handle());
			}
		}
		else
		{
			if (alerts().should_post<cache_flushed_alert>())
				alerts().emplace_alert<cache_flushed_alert>(get_handle());
		}

		// TODO: 2 abort lookups this torrent has made via the
		// session host resolver interface

		if (!m_apply_ip_filter)
		{
			inc_stats_counter(counters::non_filter_torrents, -1);
			m_apply_ip_filter = true;
		}

		m_paused = false;
		m_auto_managed = false;
		update_state_list();
		for (torrent_list_index_t i{}; i != m_links.end_index(); ++i)
		{
			if (!m_links[i].in_list()) continue;
			m_links[i].unlink(m_ses.torrent_list(i), i);
		}
		// don't re-add this torrent to the state-update list
		m_state_subscription = false;
	}

	// this is called when we're destructing non-gracefully. i.e. we're _just_
	// destructing everything.
	void torrent::panic()
	{
		m_storage.reset();
		// if there are any other peers allocated still, we need to clear them
		// now. They can't be cleared later because the allocator will already
		// have been destructed
		if (m_peer_list) m_peer_list->clear();
		m_connections.clear();
		m_outgoing_pids.clear();
		m_peers_to_disconnect.clear();
		m_num_uploads = 0;
		m_num_connecting = 0;
		m_num_connecting_seeds = 0;
	}

#ifndef TORRENT_DISABLE_SUPERSEEDING
	void torrent::set_super_seeding(bool on)
	{
		if (on == m_super_seeding) return;

		m_super_seeding = on;
		set_need_save_resume();
		state_updated();

		if (m_super_seeding) return;

		// disable super seeding for all peers
		for (auto pc : *this)
		{
			pc->superseed_piece(piece_index_t(-1), piece_index_t(-1));
		}
	}

	// TODO: 3 this should return optional<>. piece index -1 should not be
	// allowed
	piece_index_t torrent::get_piece_to_super_seed(typed_bitfield<piece_index_t> const& bits)
	{
		// return a piece with low availability that is not in
		// the bitfield and that is not currently being super
		// seeded by any peer
		TORRENT_ASSERT(m_super_seeding);

		// do a linear search from the first piece
		int min_availability = 9999;
		std::vector<piece_index_t> avail_vec;
		for (auto const i : m_torrent_file->piece_range())
		{
			if (bits[i]) continue;

			int availability = 0;
			for (auto pc : *this)
			{
				if (pc->super_seeded_piece(i))
				{
					// avoid super-seeding the same piece to more than one
					// peer if we can avoid it. Do this by artificially
					// increase the availability
					availability = 999;
					break;
				}
				if (pc->has_piece(i)) ++availability;
			}
			if (availability > min_availability) continue;
			if (availability == min_availability)
			{
				avail_vec.push_back(i);
				continue;
			}
			TORRENT_ASSERT(availability < min_availability);
			min_availability = availability;
			avail_vec.clear();
			avail_vec.push_back(i);
		}

		if (avail_vec.empty()) return piece_index_t(-1);
		return avail_vec[random(std::uint32_t(avail_vec.size() - 1))];
	}
#endif

	void torrent::on_files_deleted(storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());

		if (error)
		{
			if (alerts().should_post<torrent_delete_failed_alert>())
				alerts().emplace_alert<torrent_delete_failed_alert>(get_handle()
					, error.ec, m_torrent_file->info_hash());
		}
		else
		{
			alerts().emplace_alert<torrent_deleted_alert>(get_handle(), m_torrent_file->info_hash());
		}
	}
	catch (...) { handle_exception(); }

	void torrent::on_file_renamed(std::string const& filename
		, file_index_t const file_idx
		, storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());

		if (error)
		{
			if (alerts().should_post<file_rename_failed_alert>())
				alerts().emplace_alert<file_rename_failed_alert>(get_handle()
					, file_idx, error.ec);
		}
		else
		{
			if (alerts().should_post<file_renamed_alert>())
				alerts().emplace_alert<file_renamed_alert>(get_handle()
					, filename, file_idx);
			m_torrent_file->rename_file(file_idx, filename);
		}
	}
	catch (...) { handle_exception(); }

	void torrent::on_torrent_paused() try
	{
		TORRENT_ASSERT(is_single_thread());

		if (alerts().should_post<torrent_paused_alert>())
			alerts().emplace_alert<torrent_paused_alert>(get_handle());
	}
	catch (...) { handle_exception(); }

#if TORRENT_ABI_VERSION == 1
	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}
#endif

	std::uint32_t torrent::tracker_key() const
	{
		uintptr_t const self = reinterpret_cast<uintptr_t>(this);
		uintptr_t const ses = reinterpret_cast<uintptr_t>(&m_ses);
		std::uint32_t const storage = m_storage
			? static_cast<std::uint32_t>(static_cast<storage_index_t>(m_storage))
			: 0;
		sha1_hash const h = hasher(reinterpret_cast<char const*>(&self), sizeof(self))
			.update(reinterpret_cast<char const*>(&storage), sizeof(storage))
			.update(reinterpret_cast<char const*>(&ses), sizeof(ses))
			.final();
		unsigned char const* ptr = &h[0];
		return detail::read_uint32(ptr);
	}

#ifndef TORRENT_DISABLE_STREAMING
	void torrent::cancel_non_critical()
	{
		std::set<piece_index_t> time_critical;
		for (auto const& p : m_time_critical_pieces)
			time_critical.insert(p.piece);

		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			// for each peer, go through its download and request queue
			// and cancel everything, except pieces that are time critical

			// make a copy of the download queue since we may be cancelling entries
			// from it from within the loop
			std::vector<pending_block> dq = p->download_queue();
			for (auto const& k : dq)
			{
				if (time_critical.count(k.block.piece_index)) continue;
				if (k.not_wanted || k.timed_out) continue;
				p->cancel_request(k.block, true);
			}

			// make a copy of the download queue since we may be cancelling entries
			// from it from within the loop
			std::vector<pending_block> rq = p->request_queue();
			for (auto const& k : rq)
			{
				if (time_critical.count(k.block.piece_index)) continue;
				p->cancel_request(k.block, true);
			}
		}
	}

	void torrent::set_piece_deadline(piece_index_t const piece, int const t
		, deadline_flags_t const flags)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT_PRECOND(piece >= piece_index_t(0));
		TORRENT_ASSERT_PRECOND(valid_metadata());
		TORRENT_ASSERT_PRECOND(valid_metadata() && piece < m_torrent_file->end_piece());

		if (m_abort || !valid_metadata()
			|| piece < piece_index_t(0)
			|| piece >= m_torrent_file->end_piece())
		{
			// failed
			if (flags & torrent_handle::alert_when_available)
			{
				m_ses.alerts().emplace_alert<read_piece_alert>(
					get_handle(), piece, error_code(boost::system::errc::operation_canceled, generic_category()));
			}
			return;
		}

		time_point const deadline = aux::time_now() + milliseconds(t);

		// if we already have the piece, no need to set the deadline.
		// however, if the user asked to get the piece data back, we still
		// need to read it and post it back to the user
		if (is_seed() || (has_picker() && m_picker->has_piece_passed(piece)))
		{
			if (flags & torrent_handle::alert_when_available)
				read_piece(piece);
			return;
		}

		// if this is the first time critical piece we add. in order to make it
		// react quickly, cancel all the currently outstanding requests
		if (m_time_critical_pieces.empty())
		{
			// defer this by posting it to the end of the message queue.
			// this gives the client a chance to specify multiple time-critical
			// pieces before libtorrent cancels requests
			auto self = shared_from_this();
			m_ses.get_io_service().post([self] { self->wrap(&torrent::cancel_non_critical); });
		}

		for (auto i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			if (i->piece != piece) continue;
			i->deadline = deadline;
			i->flags = flags;

			// resort i since deadline might have changed
			while (std::next(i) != m_time_critical_pieces.end() && i->deadline > std::next(i)->deadline)
			{
				std::iter_swap(i, std::next(i));
				++i;
			}
			while (i != m_time_critical_pieces.begin() && i->deadline < std::prev(i)->deadline)
			{
				std::iter_swap(i, std::prev(i));
				--i;
			}
			// just in case this piece had priority 0
			download_priority_t prev_prio = m_picker->piece_priority(piece);
			m_picker->set_piece_priority(piece, top_priority);
			if (prev_prio == dont_download) update_gauge();
			return;
		}

		need_picker();

		time_critical_piece p;
		p.first_requested = min_time();
		p.last_requested = min_time();
		p.flags = flags;
		p.deadline = deadline;
		p.peers = 0;
		p.piece = piece;
		auto const critical_piece_it = std::upper_bound(m_time_critical_pieces.begin()
			, m_time_critical_pieces.end(), p);
		m_time_critical_pieces.insert(critical_piece_it, p);

		// just in case this piece had priority 0
		download_priority_t prev_prio = m_picker->piece_priority(piece);
		m_picker->set_piece_priority(piece, top_priority);
		if (prev_prio == dont_download) update_gauge();

		piece_picker::downloading_piece pi;
		m_picker->piece_info(piece, pi);
		if (pi.requested == 0) return;
		// this means we have outstanding requests (or queued
		// up requests that haven't been sent yet). Promote them
		// to deadline pieces immediately
		std::vector<torrent_peer*> downloaders;
		m_picker->get_downloaders(downloaders, piece);

		int block = 0;
		for (auto i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i, ++block)
		{
			torrent_peer* tp = *i;
			if (tp == nullptr || tp->connection == nullptr) continue;
			auto* peer = static_cast<peer_connection*>(tp->connection);
			peer->make_time_critical(piece_block(piece, block));
		}
	}

	void torrent::reset_piece_deadline(piece_index_t piece)
	{
		remove_time_critical_piece(piece);
	}

	void torrent::remove_time_critical_piece(piece_index_t const piece, bool const finished)
	{
		for (auto i = m_time_critical_pieces.begin(), end(m_time_critical_pieces.end());
			i != end; ++i)
		{
			if (i->piece != piece) continue;
			if (finished)
			{
				if (i->flags & torrent_handle::alert_when_available)
				{
					read_piece(i->piece);
				}

				// if first_requested is min_time(), it wasn't requested as a critical piece
				// and we shouldn't adjust any average download times
				if (i->first_requested != min_time())
				{
					// update the average download time and average
					// download time deviation
					int const dl_time = aux::numeric_cast<int>(total_milliseconds(aux::time_now() - i->first_requested));

					if (m_average_piece_time == 0)
					{
						m_average_piece_time = dl_time;
					}
					else
					{
						int diff = std::abs(dl_time - m_average_piece_time);
						if (m_piece_time_deviation == 0) m_piece_time_deviation = diff;
						else m_piece_time_deviation = (m_piece_time_deviation * 9 + diff) / 10;

						m_average_piece_time = (m_average_piece_time * 9 + dl_time) / 10;
					}
				}
			}
			else if (i->flags & torrent_handle::alert_when_available)
			{
				// post an empty read_piece_alert to indicate it failed
				alerts().emplace_alert<read_piece_alert>(
					get_handle(), piece, error_code(boost::system::errc::operation_canceled, generic_category()));
			}
			if (has_picker()) m_picker->set_piece_priority(piece, low_priority);
			m_time_critical_pieces.erase(i);
			return;
		}
	}

	void torrent::clear_time_critical()
	{
		for (auto i = m_time_critical_pieces.begin(); i != m_time_critical_pieces.end();)
		{
			if (i->flags & torrent_handle::alert_when_available)
			{
				// post an empty read_piece_alert to indicate it failed
				m_ses.alerts().emplace_alert<read_piece_alert>(
					get_handle(), i->piece, error_code(boost::system::errc::operation_canceled, generic_category()));
			}
			if (has_picker()) m_picker->set_piece_priority(i->piece, low_priority);
			i = m_time_critical_pieces.erase(i);
		}
	}

	// remove time critical pieces where priority is 0
	void torrent::remove_time_critical_pieces(aux::vector<download_priority_t, piece_index_t> const& priority)
	{
		for (auto i = m_time_critical_pieces.begin(); i != m_time_critical_pieces.end();)
		{
			if (priority[i->piece] == dont_download)
			{
				if (i->flags & torrent_handle::alert_when_available)
				{
					// post an empty read_piece_alert to indicate it failed
					alerts().emplace_alert<read_piece_alert>(
						get_handle(), i->piece, error_code(boost::system::errc::operation_canceled, generic_category()));
				}
				i = m_time_critical_pieces.erase(i);
				continue;
			}
			++i;
		}
	}
#endif // TORRENT_DISABLE_STREAMING

	void torrent::piece_availability(aux::vector<int, piece_index_t>& avail) const
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (!has_picker())
		{
			avail.clear();
			return;
		}

		m_picker->get_availability(avail);
	}

	void torrent::set_piece_priority(piece_index_t const index
		, download_priority_t const priority)
	{
//		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		if (!valid_metadata())
		{
			debug_log("*** SET_PIECE_PRIORITY [ idx: %d prio: %d ignored. "
				"no metadata yet ]", static_cast<int>(index)
				, static_cast<std::uint8_t>(priority));
		}
#endif
		if (!valid_metadata() || is_seed()) return;

		// this call is only valid on torrents with metadata
		if (index < piece_index_t(0) || index >= m_torrent_file->end_piece())
		{
			return;
		}

		need_picker();

		bool const was_finished = is_finished();
		bool const filter_updated = m_picker->set_piece_priority(index, priority);

		update_gauge();

		if (filter_updated)
		{
			update_peer_interest(was_finished);
#ifndef TORRENT_DISABLE_STREAMING
			if (priority == dont_download) remove_time_critical_piece(index);
#endif // TORRENT_DISABLE_STREAMING
		}

	}

	download_priority_t torrent::piece_priority(piece_index_t const index) const
	{
//		INVARIANT_CHECK;

		if (!has_picker()) return default_priority;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (index < piece_index_t(0) || index >= m_torrent_file->end_piece())
		{
			TORRENT_ASSERT_FAIL();
			return dont_download;
		}

		return m_picker->piece_priority(index);
	}

	void torrent::prioritize_piece_list(std::vector<std::pair<piece_index_t
		, download_priority_t>> const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		need_picker();

		bool filter_updated = false;
		bool const was_finished = is_finished();
		for (auto const& p : pieces)
		{
			static_assert(std::is_unsigned<decltype(p.second)::underlying_type>::value
				, "we need assert p.second >= dont_download");
			TORRENT_ASSERT(p.second <= top_priority);
			TORRENT_ASSERT(p.first >= piece_index_t(0));
			TORRENT_ASSERT(p.first < m_torrent_file->end_piece());

			if (p.first < piece_index_t(0)
				|| p.first >= m_torrent_file->end_piece()
				|| p.second > top_priority)
			{
				static_assert(std::is_unsigned<decltype(p.second)::underlying_type>::value
					, "we need additional condition: p.second < dont_download");
				continue;
			}

			filter_updated |= m_picker->set_piece_priority(p.first, p.second);
		}
		update_gauge();
		if (filter_updated)
		{
			// we need to save this new state
			set_need_save_resume();

			update_peer_interest(was_finished);
		}

		state_updated();
	}

	void torrent::prioritize_pieces(aux::vector<download_priority_t, piece_index_t> const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		if (!valid_metadata())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** PRIORITIZE_PIECES [ ignored. no metadata yet ]");
#endif
			return;
		}

		need_picker();

		piece_index_t index(0);
		bool filter_updated = false;
		bool const was_finished = is_finished();
		for (auto prio : pieces)
		{
			static_assert(std::is_unsigned<decltype(prio)::underlying_type>::value
				, "we need assert prio >= dont_download");
			TORRENT_ASSERT(prio <= top_priority);
			filter_updated |= m_picker->set_piece_priority(index, prio);
			++index;
		}
		update_gauge();
		update_want_tick();

		if (filter_updated)
		{
			// we need to save this new state
			set_need_save_resume();

			update_peer_interest(was_finished);
#ifndef TORRENT_DISABLE_STREAMING
			remove_time_critical_pieces(pieces);
#endif
		}

		state_updated();
		update_state_list();
	}

	void torrent::piece_priorities(aux::vector<download_priority_t, piece_index_t>* pieces) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata())
		{
			pieces->clear();
			return;
		}

		if (!has_picker())
		{
			pieces->clear();
			pieces->resize(m_torrent_file->num_pieces(), default_priority);
			return;
		}

		TORRENT_ASSERT(m_picker);
		m_picker->piece_priorities(*pieces);
	}

	namespace
	{
		aux::vector<download_priority_t, file_index_t> fix_priorities(
			aux::vector<download_priority_t, file_index_t> input
			, file_storage const* fs)
		{
			if (fs) input.resize(fs->num_files(), default_priority);

			for (file_index_t i : input.range())
			{
				// initialize pad files to priority 0
				if (input[i] > dont_download && fs && fs->pad_file_at(i))
					input[i] = dont_download;
				else if (input[i] > top_priority)
					input[i] = top_priority;
			}

			return input;
		}
	}

	void torrent::on_file_priority(storage_error const& err
		, aux::vector<download_priority_t, file_index_t> prios)
	{
		m_outstanding_file_priority = false;
		COMPLETE_ASYNC("file_priority");
		if (m_file_priority != prios)
		{
			m_file_priority = std::move(prios);
#ifndef TORRENT_DISABLE_SHARE_MODE
			if (m_share_mode)
				recalc_share_mode();
#endif
		}

		if (err)
		{
			// in this case, some file priorities failed to get set
			if (alerts().should_post<file_error_alert>())
				alerts().emplace_alert<file_error_alert>(err.ec
					, resolve_filename(err.file()), err.operation, get_handle());

			set_error(err.ec, err.file());
			pause();
		}
		else if (!m_deferred_file_priorities.empty() && !m_abort)
		{
			auto new_priority = m_file_priority;
			// resize the vector if we have to. The last item in the map has the
			// highest file index.
			auto const max_idx = std::prev(m_deferred_file_priorities.end())->first;
			if (new_priority.end_index() <= max_idx)
			{
				// any unallocated slot is assumed to have the default priority
				new_priority.resize(static_cast<int>(max_idx) + 1, default_priority);
			}
			for (auto const& p : m_deferred_file_priorities)
			{
				file_index_t const index = p.first;
				download_priority_t const prio = p.second;
				new_priority[index] = prio;
			}
			m_deferred_file_priorities.clear();
			prioritize_files(std::move(new_priority));
		}
	}

	void torrent::prioritize_files(aux::vector<download_priority_t, file_index_t> files)
	{
		INVARIANT_CHECK;

		auto new_priority = fix_priorities(std::move(files)
			, valid_metadata() ? &m_torrent_file->files() : nullptr);

		// storage may be NULL during shutdown
		if (m_storage)
		{
			// the update of m_file_priority is deferred until the disk job comes
			// back, but to preserve sanity and consistency, the piece priorities are
			// updated immediately. If, on the off-chance, there's a disk failure, the
			// piece priorities still stay the same, but the file priorities are
			// possibly not fully updated.
			update_piece_priorities(new_priority);

			m_outstanding_file_priority = true;
			ADD_OUTSTANDING_ASYNC("file_priority");
			m_ses.disk_thread().async_set_file_priority(m_storage
				, std::move(new_priority), std::bind(&torrent::on_file_priority, shared_from_this(), _1, _2));
		}
		else
		{
			m_file_priority = std::move(new_priority);
		}
	}

	void torrent::set_file_priority(file_index_t const index
		, download_priority_t prio)
	{
		INVARIANT_CHECK;

		// setting file priority on a torrent that doesn't have metadata yet is
		// similar to having passed in file priorities through add_torrent_params.
		// we store the priorities in m_file_priority until we get the metadata
		if (index < file_index_t(0)
			|| (valid_metadata() && index >= m_torrent_file->files().end_file()))
		{
			return;
		}

		prio = aux::clamp(prio, dont_download, top_priority);

		if (m_outstanding_file_priority)
		{
			m_deferred_file_priorities[index] = prio;
			return;
		}

		auto new_priority = m_file_priority;
		if (new_priority.end_index() <= index)
		{
			// any unallocated slot is assumed to have the default priority
			new_priority.resize(static_cast<int>(index) + 1, default_priority);
		}

		new_priority[index] = prio;

		// storage may be nullptr during shutdown
		if (m_storage)
		{
			// the update of m_file_priority is deferred until the disk job comes
			// back, but to preserve sanity and consistency, the piece priorities are
			// updated immediately. If, on the off-chance, there's a disk failure, the
			// piece priorities still stay the same, but the file priorities are
			// possibly not fully updated.
			update_piece_priorities(new_priority);
			m_outstanding_file_priority = true;
			ADD_OUTSTANDING_ASYNC("file_priority");
			m_ses.disk_thread().async_set_file_priority(m_storage
				, std::move(new_priority), std::bind(&torrent::on_file_priority, shared_from_this(), _1, _2));
		}
		else
		{
			m_file_priority = std::move(new_priority);
		}
	}

	download_priority_t torrent::file_priority(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0));
		if (index < file_index_t(0)) return dont_download;

		// if we have metadata, perform additional checks
		if (valid_metadata())
		{
			file_storage const& fs = m_torrent_file->files();
			TORRENT_ASSERT_PRECOND(index < fs.end_file());
			if (index >= fs.end_file()) return dont_download;

			// pad files always have priority 0
			if (fs.pad_file_at(index)) return dont_download;
		}

		// any unallocated slot is assumed to have the default priority
		if (m_file_priority.end_index() <= index) return default_priority;

		return m_file_priority[index];
	}

	void torrent::file_priorities(aux::vector<download_priority_t, file_index_t>* files) const
	{
		INVARIANT_CHECK;

		files->assign(m_file_priority.begin(), m_file_priority.end());

		if (!valid_metadata())
		{
			return;
		}

		files->resize(m_torrent_file->num_files(), default_priority);
	}

	void torrent::update_piece_priorities(
		aux::vector<download_priority_t, file_index_t> const& file_prios)
	{
		INVARIANT_CHECK;

		if (m_torrent_file->num_pieces() == 0) return;

		bool need_update = false;
		std::int64_t position = 0;
		// initialize the piece priorities to 0, then only allow
		// setting higher priorities
		aux::vector<download_priority_t, piece_index_t> pieces(aux::numeric_cast<std::size_t>(
			m_torrent_file->num_pieces()), dont_download);
		file_storage const& fs = m_torrent_file->files();
		for (auto const i : fs.file_range())
		{
			std::int64_t const size = m_torrent_file->files().file_size(i);
			if (size == 0) continue;
			position += size;

			// pad files always have priority 0
			download_priority_t const file_prio
				= fs.pad_file_at(i) ? dont_download
				: i >= file_prios.end_index() ? default_priority
				: file_prios[i];

			if (file_prio == dont_download)
			{
				// the pieces already start out as priority 0, no need to update
				// the pieces vector in this case
				need_update = true;
				continue;
			}

			// mark all pieces of the file with this file's priority
			// but only if the priority is higher than the pieces
			// already set (to avoid problems with overlapping pieces)
			piece_index_t start;
			piece_index_t end;
			std::tie(start, end) = file_piece_range_inclusive(fs, i);

			// if one piece spans several files, we might
			// come here several times with the same start_piece, end_piece
			for (piece_index_t p = start; p < end; ++p)
				pieces[p] = std::max(pieces[p], file_prio);

			need_update = true;
		}
		if (need_update) prioritize_pieces(pieces);
	}

	// this is called when piece priorities have been updated
	// updates the interested flag in peers
	void torrent::update_peer_interest(bool const was_finished)
	{
		for (auto i = begin(); i != end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			p->update_interest();
		}

		if (!is_downloading_state(m_state))
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** UPDATE_PEER_INTEREST [ skipping, state: %d ]"
				, int(m_state));
#endif
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("*** UPDATE_PEER_INTEREST [ finished: %d was_finished %d ]"
				, is_finished(), was_finished);
		}
#endif

		// the torrent just became finished
		if (!was_finished && is_finished())
		{
			finished();
		}
		else if (was_finished && !is_finished())
		{
			// if we used to be finished, but we aren't anymore
			// we may need to connect to peers again
			resume_download();
		}
	}

	void torrent::replace_trackers(std::vector<announce_entry> const& urls)
	{
		m_trackers.clear();
		std::remove_copy_if(urls.begin(), urls.end(), back_inserter(m_trackers)
			, [](announce_entry const& e) { return e.url.empty(); });

		m_last_working_tracker = -1;
		for (auto& t : m_trackers)
		{
			t.endpoints.clear();
			if (t.source == 0) t.source = announce_entry::source_client;
#if TORRENT_ABI_VERSION == 1
			t.complete_sent = m_complete_sent;
#endif
			for (auto& aep : t.endpoints)
				aep.complete_sent = m_complete_sent;
		}

		if (settings().get_bool(settings_pack::prefer_udp_trackers))
			prioritize_udp_trackers();

		if (!m_trackers.empty()) announce_with_tracker();

		set_need_save_resume();
	}

	void torrent::prioritize_udp_trackers()
	{
		// look for udp-trackers
		for (auto i = m_trackers.begin(), end(m_trackers.end()); i != end; ++i)
		{
			if (i->url.substr(0, 6) != "udp://") continue;
			// now, look for trackers with the same hostname
			// that is has higher priority than this one
			// if we find one, swap with the udp-tracker
			error_code ec;
			std::string udp_hostname;
			using std::ignore;
			std::tie(ignore, ignore, udp_hostname, ignore, ignore)
				= parse_url_components(i->url, ec);
			for (auto j = m_trackers.begin(); j != i; ++j)
			{
				std::string hostname;
				std::tie(ignore, ignore, hostname, ignore, ignore)
					= parse_url_components(j->url, ec);
				if (hostname != udp_hostname) continue;
				if (j->url.substr(0, 6) == "udp://") continue;
				using std::swap;
				using std::iter_swap;
				swap(i->tier, j->tier);
				iter_swap(i, j);
				break;
			}
		}
	}

	bool torrent::add_tracker(announce_entry const& url)
	{
		if(auto k = find_tracker(url.url))
		{
			k->source |= url.source;
			return false;
		}
		auto k = std::upper_bound(m_trackers.begin(), m_trackers.end(), url
			, [] (announce_entry const& lhs, announce_entry const& rhs)
			{ return lhs.tier < rhs.tier; });
		if (k - m_trackers.begin() < m_last_working_tracker) ++m_last_working_tracker;
		k = m_trackers.insert(k, url);
		if (k->source == 0) k->source = announce_entry::source_client;
		if (m_announcing && !m_trackers.empty()) announce_with_tracker();
		return true;
	}

	bool torrent::choke_peer(peer_connection& c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!c.is_choked());
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		TORRENT_ASSERT(m_num_uploads > 0);
		if (!c.send_choke()) return false;
		--m_num_uploads;
		state_updated();
		return true;
	}

	bool torrent::unchoke_peer(peer_connection& c, bool optimistic)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_graceful_pause_mode);
		TORRENT_ASSERT(c.is_choked());
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		// when we're unchoking the optimistic slots, we might
		// exceed the limit temporarily while we're iterating
		// over the peers
		if (m_num_uploads >= m_max_uploads && !optimistic) return false;
		if (!c.send_unchoke()) return false;
		++m_num_uploads;
		state_updated();
		return true;
	}

	void torrent::trigger_unchoke() noexcept
	{
		m_ses.trigger_unchoke();
	}

	void torrent::trigger_optimistic_unchoke() noexcept
	{
		m_ses.trigger_optimistic_unchoke();
	}

	void torrent::cancel_block(piece_block block)
	{
		INVARIANT_CHECK;

		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			p->cancel_request(block);
		}
	}

#ifdef TORRENT_USE_OPENSSL
	namespace {
		std::string password_callback(int length, boost::asio::ssl::context::password_purpose p
			, std::string pw)
		{
			TORRENT_UNUSED(length);

			if (p != boost::asio::ssl::context::for_reading) return "";
			return pw;
		}
	}

	// certificate is a filename to a .pem file which is our
	// certificate. The certificate must be signed by the root
	// cert of the torrent file. any peer we connect to or that
	// connect to use must present a valid certificate signed
	// by the torrent root cert as well
	void torrent::set_ssl_cert(std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params
		, std::string const& passphrase)
	{
		if (!m_ssl_ctx)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle()
					, errors::not_an_ssl_torrent, "");
			return;
		}

		using boost::asio::ssl::context;
		error_code ec;
		m_ssl_ctx->set_password_callback(std::bind(&password_callback, _1, _2, passphrase), ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, "");
		}
		m_ssl_ctx->use_certificate_file(certificate, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, certificate);
		}
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
			debug_log("*** use certificate file: %s", ec.message().c_str());
#endif
		m_ssl_ctx->use_private_key_file(private_key, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, private_key);
		}
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
			debug_log("*** use private key file: %s", ec.message().c_str());
#endif
		m_ssl_ctx->use_tmp_dh_file(dh_params, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, dh_params);
		}
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
			debug_log("*** use DH file: %s", ec.message().c_str());
#endif
	}

	void torrent::set_ssl_cert_buffer(std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params)
	{
		if (!m_ssl_ctx) return;

		boost::asio::const_buffer certificate_buf(certificate.c_str(), certificate.size());

		using boost::asio::ssl::context;
		error_code ec;
		m_ssl_ctx->use_certificate(certificate_buf, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, "[certificate]");
		}

		boost::asio::const_buffer private_key_buf(private_key.c_str(), private_key.size());
		m_ssl_ctx->use_private_key(private_key_buf, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, "[private key]");
		}

		boost::asio::const_buffer dh_params_buf(dh_params.c_str(), dh_params.size());
		m_ssl_ctx->use_tmp_dh(dh_params_buf, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().emplace_alert<torrent_error_alert>(get_handle(), ec, "[dh params]");
		}
	}

#endif

	void torrent::on_exception(std::exception const&)
	{
		set_error(errors::no_memory, torrent_status::error_file_none);
	}

	void torrent::on_error(error_code const& ec)
	{
		set_error(ec, torrent_status::error_file_none);
	}

	void torrent::remove_connection(peer_connection const* p)
	{
		TORRENT_ASSERT(m_iterating_connections == 0);
		auto const i = sorted_find(m_connections, p);
		if (i != m_connections.end())
			m_connections.erase(i);
	}

	void torrent::remove_peer(std::shared_ptr<peer_connection> p) noexcept
	{
		TORRENT_ASSERT(p);
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(std::count(m_peers_to_disconnect.begin()
			, m_peers_to_disconnect.end(), p) == 0);

		auto it = m_outgoing_pids.find(p->our_pid());
		if (it != m_outgoing_pids.end())
		{
			m_outgoing_pids.erase(it);
		}

		// only schedule the peer for actual removal if in fact
		// we can be sure peer_connection will be kept alive until
		// the deferred function is called. If a peer_connection
		// has not associated torrent, the session_impl object may
		// remove it at any time, which may be while the non-owning
		// pointer in m_peers_to_disconnect (if added to it) is
		// waiting for the deferred function to be called.
		//
		// one example of this situation is if for example, this
		// function is called from the attach_peer path and fail to
		// do so because of too many connections.
		bool const is_attached = p->associated_torrent().lock().get() == this;
		if (is_attached)
		{
			std::weak_ptr<torrent> weak_t = shared_from_this();
			TORRENT_ASSERT_VAL(m_peers_to_disconnect.capacity() > m_peers_to_disconnect.size()
				, m_peers_to_disconnect.capacity());
			m_peers_to_disconnect.push_back(p);
			m_deferred_disconnect.post(m_ses.get_io_service(), aux::make_handler([=]()
			{
				std::shared_ptr<torrent> t = weak_t.lock();
				if (t) t->on_remove_peers();
			}, m_deferred_handler_storage, *this));
		}
		else
		{
			// if the peer was inserted in m_connections but instructed to
			// be removed from this torrent, just remove it from it, see
			// attach_peer logic.
			remove_connection(p.get());
		}

		torrent_peer* pp = p->peer_info_struct();
		if (ready_for_connections())
		{
			TORRENT_ASSERT(p->associated_torrent().lock().get() == nullptr
				|| p->associated_torrent().lock().get() == this);

			if (has_picker())
			{
				if (p->is_seed())
				{
					m_picker->dec_refcount_all(pp);
				}
				else
				{
					auto const& pieces = p->get_bitfield();
					TORRENT_ASSERT(pieces.count() <= pieces.size());
					m_picker->dec_refcount(pieces, pp);
				}
			}
		}

		if (!p->is_choked() && !p->ignore_unchoke_slots())
		{
			--m_num_uploads;
			trigger_unchoke();
		}

		if (pp)
		{
			if (pp->optimistically_unchoked)
			{
				pp->optimistically_unchoked = false;
				m_stats_counters.inc_stats_counter(
					counters::num_peers_up_unchoked_optimistic, -1);
				trigger_optimistic_unchoke();
			}

			TORRENT_ASSERT(pp->prev_amount_upload == 0);
			TORRENT_ASSERT(pp->prev_amount_download == 0);
			pp->prev_amount_download += aux::numeric_cast<std::uint32_t>(p->statistics().total_payload_download() >> 10);
			pp->prev_amount_upload += aux::numeric_cast<std::uint32_t>(p->statistics().total_payload_upload() >> 10);

			// only decrement the seed count if the peer completed attaching to the torrent
			// otherwise the seed count did not get incremented for this peer
			if (is_attached && pp->seed)
			{
				TORRENT_ASSERT(m_num_seeds > 0);
				--m_num_seeds;
			}

			if (pp->connection && m_peer_list)
			{
				torrent_state st = get_peer_list_state();
				m_peer_list->connection_closed(*p, m_ses.session_time(), &st);
				peers_erased(st.erased);
			}
		}

		p->set_peer_info(nullptr);

		update_want_peers();
		update_want_tick();
	}

	void torrent::on_remove_peers() noexcept
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#if TORRENT_USE_ASSERTS
		auto const num = m_peers_to_disconnect.size();
#endif
		for (auto const& p : m_peers_to_disconnect)
		{
			TORRENT_ASSERT(p);
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

			remove_connection(p.get());
			m_ses.close_connection(p.get());
		}
		TORRENT_ASSERT_VAL(m_peers_to_disconnect.size() == num, m_peers_to_disconnect.size() - num);
		m_peers_to_disconnect.clear();

		if (m_graceful_pause_mode && m_connections.empty())
		{
			// we're in graceful pause mode and this was the last peer we
			// disconnected. This will clear the graceful_pause_mode and post the
			// torrent_paused_alert.
			TORRENT_ASSERT(is_paused());

			// this will post torrent_paused alert
			set_paused(true);
		}

		update_want_peers();
		update_want_tick();
	}

	void torrent::remove_web_seed_iter(std::list<web_seed_t>::iterator web)
	{
		if (web->resolving)
		{
			web->removed = true;
		}
		else
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("removing web seed: \"%s\"", web->url.c_str());
#endif

			auto* peer = static_cast<peer_connection*>(web->peer_info.connection);
			if (peer != nullptr)
			{
				// if we have a connection for this web seed, we also need to
				// disconnect it and clear its reference to the peer_info object
				// that's part of the web_seed_t we're about to remove
				TORRENT_ASSERT(peer->m_in_use == 1337);
				peer->disconnect(boost::asio::error::operation_aborted, operation_t::bittorrent);
				peer->set_peer_info(nullptr);
			}
			if (has_picker()) picker().clear_peer(&web->peer_info);

			m_web_seeds.erase(web);
		}

		update_want_tick();
	}

	void torrent::connect_to_url_seed(std::list<web_seed_t>::iterator web)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(!web->resolving);
		if (web->resolving) return;

		if (num_peers() >= int(m_max_connections)
			|| m_ses.num_connections() >= settings().get_int(settings_pack::connections_limit))
			return;

		std::string protocol;
		std::string auth;
		std::string hostname;
		int port;
		std::string path;
		error_code ec;
		std::tie(protocol, auth, hostname, port, path)
			= parse_url_components(web->url, ec);
		if (port == -1)
		{
			port = protocol == "http" ? 80 : 443;
		}

		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
				debug_log("failed to parse web seed url: %s", ec.message().c_str());
#endif
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle()
					, web->url, ec);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

		if (web->peer_info.banned)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("banned web seed: %s", web->url.c_str());
#endif
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url
					, libtorrent::errors::peer_banned);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

#ifdef TORRENT_USE_OPENSSL
		if (protocol != "http" && protocol != "https")
#else
		if (protocol != "http")
#endif
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url, errors::unsupported_url_protocol);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

		if (hostname.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url
					, errors::invalid_hostname);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

		if (port == 0)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url
					, errors::invalid_port);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

		if (m_ses.get_port_filter().access(std::uint16_t(port)) & port_filter::blocked)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle()
					, web->url, errors::port_blocked);
			}
			// never try it again
			remove_web_seed_iter(web);
			return;
		}

		if (!web->endpoints.empty())
		{
			connect_web_seed(web, web->endpoints.front());
			return;
		}

		aux::proxy_settings const& ps = m_ses.proxy();
		if ((ps.type == settings_pack::http
			|| ps.type == settings_pack::http_pw)
			&& ps.proxy_peer_connections)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("resolving proxy for web seed: %s", web->url.c_str());
#endif

			auto self = shared_from_this();
			std::uint16_t const proxy_port = ps.port;

			// use proxy
			web->resolving = true;
			m_ses.get_resolver().async_resolve(ps.hostname, resolver_interface::abort_on_shutdown
				, [self, web, proxy_port](error_code const& e, std::vector<address> const& addrs)
				{
					self->wrap(&torrent::on_proxy_name_lookup, e, addrs, web, proxy_port);
				});
		}
		else if (ps.proxy_hostnames
			&& (ps.type == settings_pack::socks5
				|| ps.type == settings_pack::socks5_pw)
			&& ps.proxy_peer_connections)
		{
			connect_web_seed(web, {address(), std::uint16_t(port)});
		}
		else
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("resolving web seed: \"%s\" %s", hostname.c_str(), web->url.c_str());
#endif

			auto self = shared_from_this();
			web->resolving = true;

			m_ses.get_resolver().async_resolve(hostname, resolver_interface::abort_on_shutdown
				, [self, web, port](error_code const& e, std::vector<address> const& addrs)
				{
					self->wrap(&torrent::on_name_lookup, e, addrs, port, web);
				});
		}
	}

	void torrent::on_proxy_name_lookup(error_code const& e
		, std::vector<address> const& addrs
		, std::list<web_seed_t>::iterator web, int port) try
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		TORRENT_ASSERT(web->resolving);
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("completed resolve proxy hostname for: %s", web->url.c_str());
		if (e && should_log())
			debug_log("proxy name lookup error: %s", e.message().c_str());
#endif
		web->resolving = false;

		if (web->removed)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("removed web seed");
#endif
			remove_web_seed_iter(web);
			return;
		}

		if (m_abort) return;

		if (e || addrs.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle()
					, web->url, e);
			}

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_web_seed_iter(web);
			return;
		}

		if (m_ses.is_aborted()) return;

		if (num_peers() >= int(m_max_connections)
			|| m_ses.num_connections() >= settings().get_int(settings_pack::connections_limit))
			return;

		tcp::endpoint a(addrs[0], std::uint16_t(port));

		std::string hostname;
		error_code ec;
		std::string protocol;
		std::tie(protocol, std::ignore, hostname, port, std::ignore)
			= parse_url_components(web->url, ec);
		if (port == -1) port = protocol == "http" ? 80 : 443;

		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle()
					, web->url, ec);
			}
			remove_web_seed_iter(web);
			return;
		}

		if (m_ip_filter && m_ip_filter->access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, a, peer_blocked_alert::ip_filter);
			return;
		}

		auto self = shared_from_this();
		web->resolving = true;
		m_ses.get_resolver().async_resolve(hostname, resolver_interface::abort_on_shutdown
			, [self, web, port](error_code const& err, std::vector<address> const& addr)
			{
				self->wrap(&torrent::on_name_lookup, err, addr, port, web);
			});
	}
	catch (...) { handle_exception(); }

	void torrent::on_name_lookup(error_code const& e
		, std::vector<address> const& addrs
		, int const port
		, std::list<web_seed_t>::iterator web) try
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		TORRENT_ASSERT(web->resolving);
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("completed resolve: %s", web->url.c_str());
#endif
		web->resolving = false;
		if (web->removed)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("removed web seed");
#endif
			remove_web_seed_iter(web);
			return;
		}

		if (m_abort) return;

		if (e || addrs.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url, e);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("*** HOSTNAME LOOKUP FAILED: %s: (%d) %s"
					, web->url.c_str(), e.value(), e.message().c_str());
			}
#endif

			// unavailable, retry in `settings_pack::web_seed_name_lookup_retry` seconds
			web->retry = aux::time_now32()
			+ seconds32(settings().get_int(settings_pack::web_seed_name_lookup_retry));
			return;
		}

		for (auto const& addr : addrs)
		{
			// fill in the peer struct's address field
			web->endpoints.emplace_back(addr, std::uint16_t(port));

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
				debug_log("  -> %s", print_endpoint(tcp::endpoint(addr, std::uint16_t(port))).c_str());
#endif
		}

		if (num_peers() >= int(m_max_connections)
			|| m_ses.num_connections() >= settings().get_int(settings_pack::connections_limit))
			return;

		connect_web_seed(web, web->endpoints.front());
	}
	catch (...) { handle_exception(); }

	void torrent::connect_web_seed(std::list<web_seed_t>::iterator web, tcp::endpoint a)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());
		if (m_abort) return;

		if (m_ip_filter && m_ip_filter->access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, a, peer_blocked_alert::ip_filter);
			return;
		}

		TORRENT_ASSERT(!web->resolving);
		TORRENT_ASSERT(web->peer_info.connection == nullptr);

		if (is_v4(a))
		{
			web->peer_info.addr = a.address().to_v4();
			web->peer_info.port = a.port();
		}

		if (is_paused()) return;
		if (m_ses.is_aborted()) return;
		if (is_upload_only()) return;

		// this web seed may have redirected all files to other URLs, leaving it
		// having no file left, and there's no longer any point in connecting to
		// it.
		if (!web->have_files.empty()
			&& web->have_files.none_set()) return;

		std::shared_ptr<aux::socket_type> s
			= std::make_shared<aux::socket_type>(m_ses.get_io_service());
		if (!s) return;

		void* userdata = nullptr;
#ifdef TORRENT_USE_OPENSSL
		const bool ssl = string_begins_no_case("https://", web->url.c_str());
		if (ssl)
		{
			userdata = m_ssl_ctx.get();
			if (!userdata) userdata = m_ses.ssl_ctx();
		}
#endif
		bool ret = instantiate_connection(m_ses.get_io_service(), m_ses.proxy()
			, *s, userdata, nullptr, true, false);
		(void)ret;
		TORRENT_ASSERT(ret);

		if (s->get<http_stream>())
		{
			// the web seed connection will talk immediately to
			// the proxy, without requiring CONNECT support
			s->get<http_stream>()->set_no_connect(true);
		}

		std::string hostname;
		error_code ec;
		using std::ignore;
		std::tie(ignore, ignore, hostname, ignore, ignore)
			= parse_url_components(web->url, ec);
		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url, ec);
			return;
		}

		bool const is_ip = is_ip_address(hostname);
		if (is_ip) a.address(make_address(hostname, ec));
		bool const proxy_hostnames = settings().get_bool(settings_pack::proxy_hostnames)
			&& !is_ip;

		if (proxy_hostnames
			&& (s->get<socks5_stream>()
#ifdef TORRENT_USE_OPENSSL
				|| s->get<ssl_stream<socks5_stream>>()
#endif
				))
		{
			// we're using a socks proxy and we're resolving
			// hostnames through it
			socks5_stream* str =
#ifdef TORRENT_USE_OPENSSL
				ssl ? &s->get<ssl_stream<socks5_stream>>()->next_layer() :
#endif
				s->get<socks5_stream>();
			TORRENT_ASSERT_VAL(str, s->type_name());

			str->set_dst_name(hostname);
		}

		setup_ssl_hostname(*s, hostname, ec);
		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().emplace_alert<url_seed_alert>(get_handle(), web->url, ec);
			return;
		}

		peer_connection_args pack{
			&m_ses
			, &settings()
			, &m_ses.stats_counters()
			, &m_ses.disk_thread()
			, &m_ses.get_io_service()
			, shared_from_this()
			, s
			, a
			, &web->peer_info
			, aux::generate_peer_id(settings())
		};

		std::shared_ptr<peer_connection> c;
		if (web->type == web_seed_entry::url_seed)
		{
			c = std::make_shared<web_peer_connection>(std::move(pack), *web);
		}
		else if (web->type == web_seed_entry::http_seed)
		{
			c = std::make_shared<http_seed_connection>(std::move(pack), *web);
		}
		if (!c) return;

#if TORRENT_USE_ASSERTS
		c->m_in_constructor = false;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_extensions)
		{
			std::shared_ptr<peer_plugin>
				pp(ext->new_connection(peer_connection_handle(c->self())));
			if (pp) c->add_extension(pp);
		}
#endif

		TORRENT_ASSERT(!c->m_in_constructor);
		// add the newly connected peer to this torrent's peer list
		TORRENT_ASSERT(m_iterating_connections == 0);

		// we don't want to have to allocate memory to disconnect this peer, so
		// make sure there's enough memory allocated in the deferred_disconnect
		// list up-front
		m_peers_to_disconnect.reserve(m_connections.size() + 1);

		sorted_insert(m_connections, c.get());
		update_want_peers();
		update_want_tick();
		m_ses.insert_peer(c);

		if (web->peer_info.seed)
		{
			TORRENT_ASSERT(m_num_seeds < 0xffff);
			++m_num_seeds;
		}

		TORRENT_ASSERT(!web->peer_info.connection);
		web->peer_info.connection = c.get();
#if TORRENT_USE_ASSERTS
		web->peer_info.in_use = true;
#endif

		c->add_stat(std::int64_t(web->peer_info.prev_amount_download) << 10
			, std::int64_t(web->peer_info.prev_amount_upload) << 10);
		web->peer_info.prev_amount_download = 0;
		web->peer_info.prev_amount_upload = 0;
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("web seed connection started: [%s] %s"
				, print_endpoint(a).c_str(), web->url.c_str());
		}
#endif

		c->start();

		if (c->is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("START queue peer [%p] (%d)", static_cast<void*>(c.get())
			, num_peers());
#endif
	}

	std::shared_ptr<const torrent_info> torrent::get_torrent_copy()
	{
		if (!m_torrent_file->is_valid()) return {};
		return m_torrent_file;
	}

	void torrent::enable_all_trackers()
	{
		for (announce_entry& ae : m_trackers)
			for (announce_endpoint& aep : ae.endpoints)
				aep.enabled = true;
	}

	void torrent::write_resume_data(add_torrent_params& ret) const
	{
		ret.version = LIBTORRENT_VERSION_NUM;
		ret.storage_mode = storage_mode();
		ret.total_uploaded = m_total_uploaded;
		ret.total_downloaded = m_total_downloaded;

		// cast to seconds in case that internal values doesn't have ratio<1>
		ret.active_time = static_cast<int>(total_seconds(active_time()));
		ret.finished_time = static_cast<int>(total_seconds(finished_time()));
		ret.seeding_time = static_cast<int>(total_seconds(seeding_time()));
		ret.last_seen_complete = m_last_seen_complete;
		ret.last_upload = std::time_t(total_seconds(m_last_upload.time_since_epoch()));
		ret.last_download = std::time_t(total_seconds(m_last_download.time_since_epoch()));

		ret.num_complete = m_complete;
		ret.num_incomplete = m_incomplete;
		ret.num_downloaded = m_downloaded;

		ret.flags = flags();

		ret.added_time = m_added_time;
		ret.completed_time = m_completed_time;

		ret.save_path = m_save_path;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		ret.url = m_url;
		ret.uuid = m_uuid;
#endif

		ret.info_hash = torrent_file().info_hash();

		if (valid_metadata())
		{
			if (m_magnet_link || (m_save_resume_flags & torrent_handle::save_info_dict))
			{
				ret.ti = m_torrent_file;
			}
		}

		if (m_torrent_file->is_merkle_torrent())
		{
			// we need to save the whole merkle hash tree
			// in order to resume
			ret.merkle_tree = m_torrent_file->merkle_tree();
		}

		// if this torrent is a seed, we won't have a piece picker
		// if we don't have anything, we may also not have a picker
		// in either case; there will be no half-finished pieces.
		if (has_picker())
		{
			int const num_blocks_per_piece = torrent_file().piece_length() / block_size();

			std::vector<piece_picker::downloading_piece> const q
				= m_picker->get_download_queue();

			// info for each unfinished piece
			for (auto const& dp : q)
			{
				if (dp.finished == 0) continue;

				bitfield bitmask;
				bitmask.resize(num_blocks_per_piece, false);

				auto const info = m_picker->blocks_for_piece(dp);
				for (int i = 0; i < int(info.size()); ++i)
				{
					if (info[i].state == piece_picker::block_info::state_finished)
						bitmask.set_bit(i);
				}
				ret.unfinished_pieces.emplace(dp.index, std::move(bitmask));
			}
		}

		// save trackers
		for (auto const& tr : m_trackers)
		{
			ret.trackers.push_back(tr.url);
			ret.tracker_tiers.push_back(tr.tier);
		}

		// save web seeds
		for (auto const& ws : m_web_seeds)
		{
			if (ws.removed || ws.ephemeral) continue;
			if (ws.type == web_seed_entry::url_seed)
				ret.url_seeds.push_back(ws.url);
			else if (ws.type == web_seed_entry::http_seed)
				ret.http_seeds.push_back(ws.url);
		}

		// write have bitmask
		// the pieces string has one byte per piece. Each
		// byte is a bitmask representing different properties
		// for the piece
		// bit 0: set if we have the piece
		// bit 1: set if we have verified the piece (in seed mode)
		bool const is_checking = state() == torrent_status::checking_files;

		// if we are checking, only save the have_pieces bitfield up to the piece
		// we have actually checked. This allows us to resume the checking when we
		// load this torrent up again. If we have not completed checking nor is
		// currently checking, don't save any pieces from the have_pieces
		// bitfield.
		piece_index_t const max_piece
			= is_checking ? m_num_checked_pieces
			: m_files_checked ? m_torrent_file->end_piece()
			: piece_index_t(0);

		TORRENT_ASSERT(ret.have_pieces.empty());
		if (max_piece > piece_index_t(0))
		{
			if (is_seed())
			{
				ret.have_pieces.resize(static_cast<int>(max_piece), true);
			}
			else if (has_picker())
			{
				ret.have_pieces.resize(static_cast<int>(max_piece), false);
				for (auto const i : ret.have_pieces.range())
					if (m_picker->have_piece(i)) ret.have_pieces.set_bit(i);
			}

			if (m_seed_mode)
				ret.verified_pieces = m_verified;
		}

		// write renamed files
		if (&m_torrent_file->files() != &m_torrent_file->orig_files()
			&& m_torrent_file->files().num_files() == m_torrent_file->orig_files().num_files())
		{
			file_storage const& fs = m_torrent_file->files();
			file_storage const& orig_fs = m_torrent_file->orig_files();
			for (auto const i : fs.file_range())
			{
				if (fs.file_path(i) != orig_fs.file_path(i))
					ret.renamed_files[i] = fs.file_path(i);
			}
		}

		// write local peers
		std::vector<torrent_peer const*> deferred_peers;
		if (m_peer_list)
		{
			for (auto p : *m_peer_list)
			{
#if TORRENT_USE_I2P
				if (p->is_i2p_addr) continue;
#endif
				if (p->banned)
				{
					ret.banned_peers.push_back(p->ip());
					continue;
				}

				// we cannot save remote connection
				// since we don't know their listen port
				// unless they gave us their listen port
				// through the extension handshake
				// so, if the peer is not connectable (i.e. we
				// don't know its listen port) or if it has
				// been banned, don't save it.
				if (!p->connectable) continue;

				// don't save peers that don't work
				if (int(p->failcount) > 0) continue;

				// don't save peers that appear to send corrupt data
				if (int(p->trust_points) < 0) continue;

				if (p->last_connected == 0)
				{
					// we haven't connected to this peer. It might still
					// be useful to save it, but only save it if we
					// don't have enough peers that we actually did connect to
					if (int(deferred_peers.size()) < 100)
						deferred_peers.push_back(p);
					continue;
				}

				ret.peers.push_back(p->ip());
			}
		}

		// if we didn't save 100 peers, fill in with second choice peers
		if (int(ret.peers.size()) < 100)
		{
			aux::random_shuffle(deferred_peers);
			for (auto const p : deferred_peers)
			{
				ret.peers.push_back(p->ip());
				if (int(ret.peers.size()) >= 100) break;
			}
		}

		ret.upload_limit = upload_limit();
		ret.download_limit = download_limit();
		ret.max_connections = max_connections();
		ret.max_uploads = max_uploads();

		// piece priorities and file priorities are mutually exclusive. If there
		// are file priorities set, don't save piece priorities.
		// when in seed mode (i.e. the client promises that we have all files)
		// it does not make sense to save file priorities.
		if (!m_file_priority.empty() && !m_seed_mode)
		{
			// write file priorities
			ret.file_priorities = m_file_priority;
		}

		if (has_picker())
		{
			// write piece priorities
			// but only if they are not set to the default
			bool default_prio = true;
			for (auto const i : m_torrent_file->piece_range())
			{
				if (m_picker->piece_priority(i) == default_priority) continue;
				default_prio = false;
				break;
			}

			if (!default_prio)
			{
				ret.piece_priorities.clear();
				ret.piece_priorities.reserve(static_cast<std::size_t>(m_torrent_file->num_pieces()));

				for (auto const i : m_torrent_file->piece_range())
					ret.piece_priorities.push_back(m_picker->piece_priority(i));
			}
		}
	}

#if TORRENT_ABI_VERSION == 1
	void torrent::get_full_peer_list(std::vector<peer_list_entry>* v) const
	{
		v->clear();
		if (!m_peer_list) return;

		v->reserve(aux::numeric_cast<std::size_t>(m_peer_list->num_peers()));
		for (auto p : *m_peer_list)
		{
			peer_list_entry e;
			e.ip = p->ip();
			e.flags = p->banned ? peer_list_entry::banned : 0;
			e.failcount = p->failcount;
			e.source = p->source;
			v->push_back(e);
		}
	}
#endif

	void torrent::get_peer_info(std::vector<peer_info>* v)
	{
		v->clear();
		for (auto const peer : *this)
		{
			TORRENT_ASSERT(peer->m_in_use == 1337);

			// incoming peers that haven't finished the handshake should
			// not be included in this list
			if (peer->associated_torrent().expired()) continue;

			v->emplace_back();
			peer_info& p = v->back();

			peer->get_peer_info(p);
		}
	}

	void torrent::get_download_queue(std::vector<partial_piece_info>* queue) const
	{
		TORRENT_ASSERT(is_single_thread());
		queue->clear();
		std::vector<block_info>& blk = m_ses.block_info_storage();
		blk.clear();

		if (!valid_metadata() || !has_picker()) return;
		piece_picker const& p = picker();
		std::vector<piece_picker::downloading_piece> q
			= p.get_download_queue();
		if (q.empty()) return;

		const int blocks_per_piece = m_picker->blocks_in_piece(piece_index_t(0));
		blk.resize(q.size() * aux::numeric_cast<std::size_t>(blocks_per_piece));

		int counter = 0;
		for (auto i = q.begin(); i != q.end(); ++i, ++counter)
		{
			partial_piece_info pi;
			pi.blocks_in_piece = p.blocks_in_piece(i->index);
			pi.finished = int(i->finished);
			pi.writing = int(i->writing);
			pi.requested = int(i->requested);
#if TORRENT_ABI_VERSION == 1
			pi.piece_state = partial_piece_info::none;
#endif
			TORRENT_ASSERT(counter * blocks_per_piece + pi.blocks_in_piece <= int(blk.size()));
			pi.blocks = &blk[std::size_t(counter * blocks_per_piece)];
			int const piece_size = torrent_file().piece_size(i->index);
			int idx = -1;
			for (auto const& info : m_picker->blocks_for_piece(*i))
			{
				++idx;
				block_info& bi = pi.blocks[idx];
				bi.state = info.state;
				bi.block_size = idx < pi.blocks_in_piece - 1
					? aux::numeric_cast<std::uint32_t>(block_size())
					: aux::numeric_cast<std::uint32_t>(piece_size - (idx * block_size()));
				bool const complete = bi.state == block_info::writing
					|| bi.state == block_info::finished;
				if (info.peer == nullptr)
				{
					bi.set_peer(tcp::endpoint());
					bi.bytes_progress = complete ? bi.block_size : 0;
				}
				else
				{
					torrent_peer* tp = info.peer;
					TORRENT_ASSERT(tp->in_use);
					if (tp->connection)
					{
						auto* peer = static_cast<peer_connection*>(tp->connection);
						TORRENT_ASSERT(peer->m_in_use);
						bi.set_peer(peer->remote());
						if (bi.state == block_info::requested)
						{
							auto pbp = peer->downloading_piece_progress();
							if (pbp.piece_index == i->index && pbp.block_index == idx)
							{
								bi.bytes_progress = aux::numeric_cast<std::uint32_t>(pbp.bytes_downloaded);
								TORRENT_ASSERT(bi.bytes_progress <= bi.block_size);
							}
							else
							{
								bi.bytes_progress = 0;
							}
						}
						else
						{
							bi.bytes_progress = complete ? bi.block_size : 0;
						}
					}
					else
					{
						bi.set_peer(tp->ip());
						bi.bytes_progress = complete ? bi.block_size : 0;
					}
				}

				pi.blocks[idx].num_peers = info.num_peers;
			}
			pi.piece_index = i->index;
			queue->push_back(pi);
		}

	}

	bool torrent::connect_to_peer(torrent_peer* peerinfo, bool const ignore_limit)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
		TORRENT_UNUSED(ignore_limit);

		TORRENT_ASSERT(peerinfo);
		TORRENT_ASSERT(peerinfo->connection == nullptr);

		if (m_abort) return false;

		peerinfo->last_connected = m_ses.session_time();
#if TORRENT_USE_ASSERTS
		if (!settings().get_bool(settings_pack::allow_multiple_connections_per_ip))
		{
			// this asserts that we don't have duplicates in the peer_list's peer list
			peer_iterator i_ = std::find_if(m_connections.begin(), m_connections.end()
				, [peerinfo] (peer_connection const* p)
				{ return !p->is_disconnecting() && p->remote() == peerinfo->ip(); });
#if TORRENT_USE_I2P
			TORRENT_ASSERT(i_ == m_connections.end()
				|| (*i_)->type() != connection_type::bittorrent
				|| peerinfo->is_i2p_addr);
#else
			TORRENT_ASSERT(i_ == m_connections.end()
				|| (*i_)->type() != connection_type::bittorrent);
#endif
		}
#endif // TORRENT_USE_ASSERTS

		TORRENT_ASSERT(want_peers() || ignore_limit);
		TORRENT_ASSERT(m_ses.num_connections()
			< settings().get_int(settings_pack::connections_limit) || ignore_limit);

		tcp::endpoint a(peerinfo->ip());
		TORRENT_ASSERT(!m_apply_ip_filter
			|| !m_ip_filter
			|| (m_ip_filter->access(peerinfo->address()) & ip_filter::blocked) == 0);

		std::shared_ptr<aux::socket_type> s = std::make_shared<aux::socket_type>(m_ses.get_io_service());

#if TORRENT_USE_I2P
		bool const i2p = peerinfo->is_i2p_addr;
		if (i2p)
		{
			if (m_ses.i2p_proxy().hostname.empty())
			{
				// we have an i2p torrent, but we're not connected to an i2p
				// SAM proxy.
				if (alerts().should_post<i2p_alert>())
					alerts().emplace_alert<i2p_alert>(errors::no_i2p_router);
				return false;
			}

			// It's not entirely obvious why this peer connection is not marked as
			// one. The main feature of a peer connection is that whether or not we
			// proxy it is configurable. When we use i2p, we want to always prox
			// everything via i2p.
			bool const ret = instantiate_connection(m_ses.get_io_service()
				, m_ses.i2p_proxy(), *s, nullptr, nullptr, false, false);
			(void)ret;
			TORRENT_ASSERT(ret);
			s->get<i2p_stream>()->set_destination(static_cast<i2p_peer*>(peerinfo)->dest());
			s->get<i2p_stream>()->set_command(i2p_stream::cmd_connect);
			s->get<i2p_stream>()->set_session_id(m_ses.i2p_session());
		}
		else
#endif
		{
			// this is where we determine if we open a regular TCP connection
			// or a uTP connection. If the utp_socket_manager pointer is not passed in
			// we'll instantiate a TCP connection
			utp_socket_manager* sm = nullptr;

			if (settings().get_bool(settings_pack::enable_outgoing_utp)
				&& (!settings().get_bool(settings_pack::enable_outgoing_tcp)
					|| peerinfo->supports_utp
					|| peerinfo->confirmed_supports_utp))
			{
				sm = m_ses.utp_socket_manager();
			}

			// don't make a TCP connection if it's disabled
			if (sm == nullptr && !settings().get_bool(settings_pack::enable_outgoing_tcp))
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("discarding peer \"%s\": TCP connections disabled "
						"[ supports-utp: %d ]", peerinfo->to_string().c_str()
						, peerinfo->supports_utp);
				}
#endif
				return false;
			}

			void* userdata = nullptr;
#ifdef TORRENT_USE_OPENSSL
			if (is_ssl_torrent())
			{
				userdata = m_ssl_ctx.get();
				// if we're creating a uTP socket, since this is SSL now, make sure
				// to pass in the corresponding utp socket manager
				if (sm) sm = m_ses.ssl_utp_socket_manager();
			}
#endif

			bool ret = instantiate_connection(m_ses.get_io_service()
				, m_ses.proxy(), *s, userdata, sm, true, false);
			(void)ret;
			TORRENT_ASSERT(ret);

#if defined TORRENT_USE_OPENSSL
			if (is_ssl_torrent())
			{
				// for ssl sockets, set the hostname
				std::string host_name = aux::to_hex(m_torrent_file->info_hash());

#define CASE(t) case aux::socket_type_int_impl<ssl_stream<t>>::value: \
	s->get<ssl_stream<t>>()->set_host_name(host_name); break;

				switch (s->type())
				{
					CASE(tcp::socket)
					CASE(socks5_stream)
					CASE(http_stream)
					CASE(utp_stream)
					default: break;
				}
			}
#undef CASE
#endif
		}

		peer_id const our_pid = aux::generate_peer_id(settings());
		peer_connection_args pack{
			&m_ses
			, &settings()
			, &m_ses.stats_counters()
			, &m_ses.disk_thread()
			, &m_ses.get_io_service()
			, shared_from_this()
			, s
			, a
			, peerinfo
			, our_pid
		};

		auto c = std::make_shared<bt_peer_connection>(std::move(pack));

#if TORRENT_USE_ASSERTS
		c->m_in_constructor = false;
#endif

		c->add_stat(std::int64_t(peerinfo->prev_amount_download) << 10
			, std::int64_t(peerinfo->prev_amount_upload) << 10);
		peerinfo->prev_amount_download = 0;
		peerinfo->prev_amount_upload = 0;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_extensions)
		{
			std::shared_ptr<peer_plugin> pp(ext->new_connection(
					peer_connection_handle(c->self())));
			if (pp) c->add_extension(pp);
		}
#endif

		// add the newly connected peer to this torrent's peer list
		TORRENT_ASSERT(m_iterating_connections == 0);

		// we don't want to have to allocate memory to disconnect this peer, so
		// make sure there's enough memory allocated in the deferred_disconnect
		// list up-front
		m_peers_to_disconnect.reserve(m_connections.size() + 1);

		sorted_insert(m_connections, c.get());
		TORRENT_TRY
		{
			m_outgoing_pids.insert(our_pid);
			m_ses.insert_peer(c);
			need_peer_list();
			m_peer_list->set_connection(peerinfo, c.get());
			if (peerinfo->seed)
			{
				TORRENT_ASSERT(m_num_seeds < 0xffff);
				++m_num_seeds;
			}
			update_want_peers();
			update_want_tick();
			c->start();

			if (c->is_disconnecting()) return false;
		}
		TORRENT_CATCH (std::exception const&)
		{
			TORRENT_ASSERT(m_iterating_connections == 0);
			c->disconnect(errors::no_error, operation_t::bittorrent, peer_connection_interface::failure);
			return false;
		}

#ifndef TORRENT_DISABLE_SHARE_MODE
		if (m_share_mode)
			recalc_share_mode();
#endif

		return peerinfo->connection != nullptr;
	}

	bool torrent::set_metadata(span<char const> metadata_buf)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_torrent_file->is_valid()) return false;

		sha1_hash const info_hash = hasher(metadata_buf).final();
		if (info_hash != m_torrent_file->info_hash())
		{
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().emplace_alert<metadata_failed_alert>(get_handle()
					, errors::mismatching_info_hash);
			}
			return false;
		}

		bdecode_node metadata;
		error_code ec;
		int ret = bdecode(metadata_buf.begin(), metadata_buf.end(), metadata, ec);
		if (ret != 0 || !m_torrent_file->parse_info_section(metadata, ec))
		{
			update_gauge();
			// this means the metadata is correct, since we
			// verified it against the info-hash, but we
			// failed to parse it. Pause the torrent
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().emplace_alert<metadata_failed_alert>(get_handle(), ec);
			}
			set_error(errors::invalid_swarm_metadata, torrent_status::error_file_none);
			pause();
			return false;
		}

		update_gauge();
		update_want_tick();

		if (m_ses.alerts().should_post<metadata_received_alert>())
		{
			m_ses.alerts().emplace_alert<metadata_received_alert>(
				get_handle());
		}

		// we have to initialize the torrent before we start
		// disconnecting redundant peers, otherwise we'll think
		// we're a seed, because we have all 0 pieces
		init();

		inc_stats_counter(counters::num_total_pieces_added
			, m_torrent_file->num_pieces());

		// disconnect redundant peers
		for (auto p : m_connections)
			p->disconnect_if_redundant();

		set_need_save_resume();

		return true;
	}

	namespace {

	bool connecting_time_compare(peer_connection const* lhs, peer_connection const* rhs)
	{
		bool const lhs_connecting = lhs->is_connecting() && !lhs->is_disconnecting();
		bool const rhs_connecting = rhs->is_connecting() && !rhs->is_disconnecting();
		if (lhs_connecting != rhs_connecting) return (int(lhs_connecting) < int(rhs_connecting));

		// a lower value of connected_time means it's been waiting
		// longer. This is a less-than comparison, so if lhs has
		// waited longer than rhs, we should return false.
		return lhs->connected_time() > rhs->connected_time();
	}

	} // anonymous namespace

	bool torrent::attach_peer(peer_connection* p) try
	{
//		INVARIANT_CHECK;

#ifdef TORRENT_USE_OPENSSL
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
		if (is_ssl_torrent())
		{
			// if this is an SSL torrent, don't allow non SSL peers on it
			std::shared_ptr<aux::socket_type> s = p->get_socket();

			//
#define SSL(t) aux::socket_type_int_impl<ssl_stream<t>>::value: \
			ssl_conn = s->get<ssl_stream<t>>()->native_handle(); \
			break;

			SSL* ssl_conn = nullptr;

			switch (s->type())
			{
				case SSL(tcp::socket)
				case SSL(socks5_stream)
				case SSL(http_stream)
				case SSL(utp_stream)
			}

#undef SSL

			if (ssl_conn == nullptr)
			{
				// don't allow non SSL peers on SSL torrents
				p->disconnect(errors::requires_ssl_connection, operation_t::bittorrent);
				return false;
			}

			if (!m_ssl_ctx)
			{
				// we don't have a valid cert, don't accept any connection!
				p->disconnect(errors::invalid_ssl_cert, operation_t::ssl_handshake);
				return false;
			}

			if (SSL_get_SSL_CTX(ssl_conn) != m_ssl_ctx->native_handle())
			{
				// if the SSL_CTX associated with this connection is
				// not the one belonging to this torrent, the SSL handshake
				// connected to one torrent, and the BitTorrent protocol
				// to a different one. This is probably an attempt to circumvent
				// access control. Don't allow it.
				p->disconnect(errors::invalid_ssl_cert, operation_t::bittorrent);
				return false;
			}
		}
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
#else // TORRENT_USE_OPENSSL
		if (is_ssl_torrent())
		{
			// Don't accidentally allow seeding of SSL torrents, just
			// because libtorrent wasn't built with SSL support
			p->disconnect(errors::requires_ssl_connection, operation_t::ssl_handshake);
			return false;
		}
#endif // TORRENT_USE_OPENSSL

		TORRENT_ASSERT(p != nullptr);
		TORRENT_ASSERT(!p->is_outgoing());

		m_has_incoming = true;

		if (m_apply_ip_filter
			&& m_ip_filter
			&& m_ip_filter->access(p->remote().address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, p->remote(), peer_blocked_alert::ip_filter);
			p->disconnect(errors::banned_by_ip_filter, operation_t::bittorrent);
			return false;
		}

		if (!is_downloading_state(m_state) && valid_metadata())
		{
			p->disconnect(errors::torrent_not_ready, operation_t::bittorrent);
			return false;
		}

		if (!m_ses.has_connection(p))
		{
			p->disconnect(errors::peer_not_constructed, operation_t::bittorrent);
			return false;
		}

		if (m_ses.is_aborted())
		{
			p->disconnect(errors::session_closing, operation_t::bittorrent);
			return false;
		}

		int connection_limit_factor = 0;
		for (int i = 0; i < p->num_classes(); ++i)
		{
			peer_class_t pc = p->class_at(i);
			if (m_ses.peer_classes().at(pc) == nullptr) continue;
			int f = m_ses.peer_classes().at(pc)->connection_limit_factor;
			if (connection_limit_factor < f) connection_limit_factor = f;
		}
		if (connection_limit_factor == 0) connection_limit_factor = 100;

		std::int64_t const limit = std::int64_t(m_max_connections) * 100 / connection_limit_factor;

		bool maybe_replace_peer = false;

		if (m_connections.end_index() >= limit)
		{
			// if more than 10% of the connections are outgoing
			// connection attempts that haven't completed yet,
			// disconnect one of them and let this incoming
			// connection through.
			if (m_num_connecting > m_max_connections / 10)
			{
				// find one of the connecting peers and disconnect it
				// find any peer that's connecting (i.e. a half-open TCP connection)
				// that's also not disconnecting
				// disconnect the peer that's been waiting to establish a connection
				// the longest
				auto i = std::max_element(begin(), end(), &connecting_time_compare);

				if (i == end() || !(*i)->is_connecting() || (*i)->is_disconnecting())
				{
					// this seems odd, but we might as well handle it
					p->disconnect(errors::too_many_connections, operation_t::bittorrent);
					return false;
				}
				(*i)->disconnect(errors::too_many_connections, operation_t::bittorrent);

				// if this peer was let in via connections slack,
				// it has done its duty of causing the disconnection
				// of another peer
				p->peer_disconnected_other();
			}
			else
			{
				maybe_replace_peer = true;
			}
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			std::shared_ptr<peer_plugin> pp(ext->new_connection(
					peer_connection_handle(p->self())));
			if (pp) p->add_extension(pp);
		}
#endif
		torrent_state st = get_peer_list_state();
		need_peer_list();
		if (!m_peer_list->new_connection(*p, m_ses.session_time(), &st))
		{
			peers_erased(st.erased);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("CLOSING CONNECTION \"%s\" peer list full "
					"connections: %d limit: %d"
					, print_endpoint(p->remote()).c_str()
					, num_peers()
					, m_max_connections);
			}
#endif
			p->disconnect(errors::too_many_connections, operation_t::bittorrent);
			return false;
		}
		peers_erased(st.erased);

		m_peers_to_disconnect.reserve(m_connections.size() + 1);
		m_connections.reserve(m_connections.size() + 1);

#if TORRENT_USE_ASSERTS
		error_code ec;
		TORRENT_ASSERT(p->remote() == p->get_socket()->remote_endpoint(ec) || ec);
#endif

		TORRENT_ASSERT(p->peer_info_struct() != nullptr);

		// we need to do this after we've added the peer to the peer_list
		// since that's when the peer is assigned its peer_info object,
		// which holds the rank
		if (maybe_replace_peer)
		{
			// now, find the lowest rank peer and disconnect that
			// if it's lower rank than the incoming connection
			peer_connection* peer = find_lowest_ranking_peer();

			// TODO: 2 if peer is a really good peer, maybe we shouldn't disconnect it
			// perhaps this logic should be disabled if we have too many idle peers
			// (with some definition of idle)
			if (peer != nullptr && peer->peer_rank() < p->peer_rank())
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("CLOSING CONNECTION \"%s\" peer list full (low peer rank) "
						"connections: %d limit: %d"
						, print_endpoint(peer->remote()).c_str()
						, num_peers()
						, m_max_connections);
				}
#endif
				peer->disconnect(errors::too_many_connections, operation_t::bittorrent);
				p->peer_disconnected_other();
			}
			else
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("CLOSING CONNECTION \"%s\" peer list full (low peer rank) "
						"connections: %d limit: %d"
						, print_endpoint(p->remote()).c_str()
						, num_peers()
						, m_max_connections);
				}
#endif
				p->disconnect(errors::too_many_connections, operation_t::bittorrent);
				// we have to do this here because from the peer's point of view
				// it wasn't really attached to the torrent, but we do need
				// to let peer_list know we're removing it
				remove_peer(p->self());
				return false;
			}
		}

#if TORRENT_USE_INVARIANT_CHECKS
		if (m_peer_list) m_peer_list->check_invariant();
#endif

#ifndef TORRENT_DISABLE_SHARE_MODE
		if (m_share_mode)
			recalc_share_mode();
#endif

		// once we add the peer to our m_connections list, we can't throw an
		// exception. That will end up violating an invariant between the session,
		// torrent and peers
		TORRENT_ASSERT(sorted_find(m_connections, p) == m_connections.end());
		TORRENT_ASSERT(m_iterating_connections == 0);
		sorted_insert(m_connections, p);
		update_want_peers();
		update_want_tick();

		if (p->peer_info_struct() && p->peer_info_struct()->seed)
		{
			TORRENT_ASSERT(m_num_seeds < 0xffff);
			++m_num_seeds;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log()) try
		{
			debug_log("ATTACHED CONNECTION \"%s\" connections: %d limit: %d num-peers: %d"
				, print_endpoint(p->remote()).c_str(), num_peers()
				, m_max_connections
				, num_peers());
		}
		catch (std::exception const&) {}
#endif

		return true;
	}
	catch (...)
	{
		p->disconnect(errors::torrent_not_ready, operation_t::bittorrent);
		// from the peer's point of view it was never really added to the torrent.
		// So we need to clean it up here before propagating the error
		remove_peer(p->self());
		return false;
	}

	bool torrent::want_tick() const
	{
		if (m_abort) return false;

		if (!m_connections.empty()) return true;

		// we might want to connect web seeds
		if (!is_finished() && !m_web_seeds.empty() && m_files_checked)
			return true;

		if (m_stat.low_pass_upload_rate() > 0 || m_stat.low_pass_download_rate() > 0)
			return true;

		// if we don't get ticks we won't become inactive
		if (!m_paused && !m_inactive) return true;

		return false;
	}

	void torrent::update_want_tick()
	{
		update_list(aux::session_interface::torrent_want_tick, want_tick());
	}

	// this function adjusts which lists this torrent is part of (checking,
	// seeding or downloading)
	void torrent::update_state_list()
	{
		bool is_checking = false;
		bool is_downloading = false;
		bool is_seeding = false;

		if (is_auto_managed() && !has_error())
		{
			if (m_state == torrent_status::checking_files
				|| m_state == torrent_status::allocating)
			{
				is_checking = true;
			}
			else if (m_state == torrent_status::downloading_metadata
				|| m_state == torrent_status::downloading
				|| m_state == torrent_status::finished
				|| m_state == torrent_status::seeding)
			{
				// torrents that are started (not paused) and
				// inactive are not part of any list. They will not be touched because
				// they are inactive
				if (is_finished())
					is_seeding = true;
				else
					is_downloading = true;
			}
		}

		update_list(aux::session_interface::torrent_downloading_auto_managed
			, is_downloading);
		update_list(aux::session_interface::torrent_seeding_auto_managed
			, is_seeding);
		update_list(aux::session_interface::torrent_checking_auto_managed
			, is_checking);
	}

	// returns true if this torrent is interested in connecting to more peers
	bool torrent::want_peers() const
	{
		// if all our connection slots are taken, we can't connect to more
		if (num_peers() >= int(m_max_connections)) return false;

		// if we're paused, obviously we're not connecting to peers
		if (is_paused() || m_abort || m_graceful_pause_mode) return false;

		if ((m_state == torrent_status::checking_files
			|| m_state == torrent_status::checking_resume_data)
			&& valid_metadata())
			return false;

		// if we don't know of any more potential peers to connect to, there's
		// no point in trying
		if (!m_peer_list || m_peer_list->num_connect_candidates() == 0)
			return false;

		// if the user disabled outgoing connections for seeding torrents,
		// don't make any
		if (!settings().get_bool(settings_pack::seeding_outgoing_connections)
			&& (m_state == torrent_status::seeding
				|| m_state == torrent_status::finished))
			return false;

		return true;
	}

	bool torrent::want_peers_download() const
	{
		return (m_state == torrent_status::downloading
			|| m_state == torrent_status::downloading_metadata)
			&& want_peers();
	}

	bool torrent::want_peers_finished() const
	{
		return (m_state == torrent_status::finished
			|| m_state == torrent_status::seeding)
			&& want_peers();
	}

	void torrent::update_want_peers()
	{
		update_list(aux::session_interface::torrent_want_peers_download, want_peers_download());
		update_list(aux::session_interface::torrent_want_peers_finished, want_peers_finished());
	}

	void torrent::update_want_scrape()
	{
		update_list(aux::session_interface::torrent_want_scrape
			, m_paused && m_auto_managed && !m_abort);
	}

	namespace {

#ifndef TORRENT_DISABLE_LOGGING
	char const* list_name(torrent_list_index_t const idx)
	{
#define TORRENT_LIST_NAME(n) case static_cast<int>(aux::session_interface:: n): return #n
		switch (static_cast<int>(idx))
		{
			TORRENT_LIST_NAME(torrent_state_updates);
			TORRENT_LIST_NAME(torrent_want_tick);
			TORRENT_LIST_NAME(torrent_want_peers_download);
			TORRENT_LIST_NAME(torrent_want_peers_finished);
			TORRENT_LIST_NAME(torrent_want_scrape);
			TORRENT_LIST_NAME(torrent_downloading_auto_managed);
			TORRENT_LIST_NAME(torrent_seeding_auto_managed);
			TORRENT_LIST_NAME(torrent_checking_auto_managed);
			default: TORRENT_ASSERT_FAIL_VAL(idx);
		}
#undef TORRENT_LIST_NAME
		return "";
	}
#endif // TORRENT_DISABLE_LOGGING

	} // anonymous namespace

	void torrent::update_list(torrent_list_index_t const list, bool in)
	{
		link& l = m_links[list];
		aux::vector<torrent*>& v = m_ses.torrent_list(list);

		if (in)
		{
			if (l.in_list()) return;
			l.insert(v, this);
		}
		else
		{
			if (!l.in_list()) return;
			l.unlink(v, list);
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
			debug_log("*** UPDATE LIST [ %s : %d ]", list_name(list), int(in));
#endif
	}

	void torrent::disconnect_all(error_code const& ec, operation_t op)
	{
		TORRENT_ASSERT(m_iterating_connections == 0);
		for (auto const& p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
			p->disconnect(ec, op);
		}

		update_want_peers();
		update_want_tick();
	}

	namespace {

	// this returns true if lhs is a better disconnect candidate than rhs
	bool compare_disconnect_peer(peer_connection const* lhs, peer_connection const* rhs)
	{
		// prefer to disconnect peers that are already disconnecting
		if (lhs->is_disconnecting() != rhs->is_disconnecting())
			return lhs->is_disconnecting();

		// prefer to disconnect peers we're not interested in
		if (lhs->is_interesting() != rhs->is_interesting())
			return rhs->is_interesting();

		// prefer to disconnect peers that are not seeds
		if (lhs->is_seed() != rhs->is_seed())
			return rhs->is_seed();

		// prefer to disconnect peers that are on parole
		if (lhs->on_parole() != rhs->on_parole())
			return lhs->on_parole();

		// prefer to disconnect peers that send data at a lower rate
		std::int64_t lhs_transferred = lhs->statistics().total_payload_download();
		std::int64_t rhs_transferred = rhs->statistics().total_payload_download();

		time_point const now = aux::time_now();
		std::int64_t const lhs_time_connected = total_seconds(now - lhs->connected_time());
		std::int64_t const rhs_time_connected = total_seconds(now - rhs->connected_time());

		lhs_transferred /= lhs_time_connected + 1;
		rhs_transferred /= (rhs_time_connected + 1);
		if (lhs_transferred != rhs_transferred)
			return lhs_transferred < rhs_transferred;

		// prefer to disconnect peers that chokes us
		if (lhs->is_choked() != rhs->is_choked())
			return lhs->is_choked();

		return lhs->last_received() < rhs->last_received();
	}

	} // anonymous namespace

	int torrent::disconnect_peers(int const num, error_code const& ec)
	{
		INVARIANT_CHECK;

#if TORRENT_USE_ASSERTS
		// make sure we don't have any dangling pointers
		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			TORRENT_ASSERT(m_ses.has_peer(p));
		}
#endif
		aux::vector<peer_connection*> to_disconnect;
		to_disconnect.resize(num);
		auto end = std::partial_sort_copy(m_connections.begin(), m_connections.end()
			, to_disconnect.begin(), to_disconnect.end(), compare_disconnect_peer);
		for (auto p : range(to_disconnect.begin(), end))
		{
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
			p->disconnect(ec, operation_t::bittorrent);
		}
		return static_cast<int>(end - to_disconnect.begin());
	}

	// called when torrent is finished (all interesting
	// pieces have been downloaded)
	void torrent::finished()
	{
		update_state_list();

		INVARIANT_CHECK;

		TORRENT_ASSERT(is_finished());

		set_state(torrent_status::finished);
		set_queue_position(no_pos);

		m_became_finished = aux::time_now32();

		// we have to call completed() before we start
		// disconnecting peers, since there's an assert
		// to make sure we're cleared the piece picker
		if (is_seed()) completed();

		send_upload_only();
		state_updated();

		if (m_completed_time == 0)
			m_completed_time = time(nullptr);

		// disconnect all seeds
		if (settings().get_bool(settings_pack::close_redundant_connections))
		{
			// TODO: 1 should disconnect all peers that have the pieces we have
			// not just seeds. It would be pretty expensive to check all pieces
			// for all peers though
			std::vector<peer_connection*> seeds;
			for (auto const p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
				if (p->upload_only())
				{
#ifndef TORRENT_DISABLE_LOGGING
					p->peer_log(peer_log_alert::info, "SEED", "CLOSING CONNECTION");
#endif
					seeds.push_back(p);
				}
			}
			for (auto& p : seeds)
				p->disconnect(errors::torrent_finished, operation_t::bittorrent
					, peer_connection_interface::normal);
		}

		if (m_abort) return;

		update_want_peers();

		if (m_storage)
		{
			// we need to keep the object alive during this operation
			m_ses.disk_thread().async_release_files(m_storage
				, std::bind(&torrent::on_cache_flushed, shared_from_this(), false));
		}

		// this torrent just completed downloads, which means it will fall
		// under a different limit with the auto-manager. Make sure we
		// update auto-manage torrents in that case
		if (m_auto_managed)
			m_ses.trigger_auto_manage();
	}

	// this is called when we were finished, but some files were
	// marked for downloading, and we are no longer finished
	void torrent::resume_download()
	{
		// the invariant doesn't hold here, because it expects the torrent
		// to be in downloading state (which it will be set to shortly)
//		INVARIANT_CHECK;

		TORRENT_ASSERT(m_state != torrent_status::checking_resume_data
			&& m_state != torrent_status::checking_files
			&& m_state != torrent_status::allocating);

		// we're downloading now, which means we're no longer in seed mode
		if (m_seed_mode)
			leave_seed_mode(seed_mode_t::check_files);

		TORRENT_ASSERT(!is_finished());
		set_state(torrent_status::downloading);
		set_queue_position(last_pos);

		m_completed_time = 0;

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RESUME_DOWNLOAD");
#endif
		send_upload_only();
		update_want_tick();
		update_state_list();
	}

	void torrent::maybe_done_flushing()
	{
		if (!has_picker()) return;

		if (m_picker->is_seeding())
		{
			// no need for the piece picker anymore
			// when we're suggesting read cache pieces, we
			// still need the piece picker, to keep track
			// of availability counts for pieces
			if (settings().get_int(settings_pack::suggest_mode)
				!= settings_pack::suggest_read_cache)
			{
				m_picker.reset();
				m_file_progress.clear();
			}
			m_have_all = true;
		}
		update_gauge();
	}

	// called when torrent is complete. i.e. all pieces downloaded
	// not necessarily flushed to disk
	void torrent::completed()
	{
		maybe_done_flushing();

		set_state(torrent_status::seeding);
		m_became_seed = aux::time_now32();

		if (!m_announcing) return;

		time_point32 const now = aux::time_now32();
		for (auto& t : m_trackers)
		{
			for (auto& aep : t.endpoints)
			{
				if (aep.complete_sent || !aep.enabled) continue;
				aep.next_announce = now;
				aep.min_announce = now;
			}
		}
		announce_with_tracker();
	}

	int torrent::deprioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_trackers.size()));
		if (index >= int(m_trackers.size())) return -1;

		while (index < int(m_trackers.size()) - 1 && m_trackers[index].tier == m_trackers[index + 1].tier)
		{
			using std::swap;
			swap(m_trackers[index], m_trackers[index + 1]);
			if (m_last_working_tracker == index) ++m_last_working_tracker;
			else if (m_last_working_tracker == index + 1) --m_last_working_tracker;
			++index;
		}
		return index;
	}

	void torrent::files_checked()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_torrent_file->is_valid());

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("files_checked(), paused");
#endif
			return;
		}

		// calling pause will also trigger the auto managed
		// recalculation
		// if we just got here by downloading the metadata,
		// just keep going, no need to disconnect all peers just
		// to restart the torrent in a second
		if (m_auto_managed)
		{
			// if this is an auto managed torrent, force a recalculation
			// of which torrents to have active
			m_ses.trigger_auto_manage();
		}

		if (!is_seed())
		{
#ifndef TORRENT_DISABLE_SUPERSEEDING
			// turn off super seeding if we're not a seed
			if (m_super_seeding)
			{
				m_super_seeding = false;
				set_need_save_resume();
				state_updated();
			}
#endif

			if (m_state != torrent_status::finished && is_finished())
				finished();
		}
		else
		{
			// we just added this torrent as a seed, or force-rechecked it, and we
			// have all of it. Assume that we sent the event=completed when we
			// finished downloading it, and don't send any more.
			m_complete_sent = true;
			for (auto& t : m_trackers)
			{
#if TORRENT_ABI_VERSION == 1
				t.complete_sent = true;
#endif
				for (auto& aep : t.endpoints)
					aep.complete_sent = true;
			}

			if (m_state != torrent_status::finished
				&& m_state != torrent_status::seeding)
				finished();
		}

		// we might be finished already, in which case we should
		// not switch to downloading mode. If all files are
		// filtered, we're finished when we start.
		if (m_state != torrent_status::finished
			&& m_state != torrent_status::seeding
			&& !m_seed_mode)
		{
			set_state(torrent_status::downloading);
		}

		INVARIANT_CHECK;

		if (m_ses.alerts().should_post<torrent_checked_alert>())
		{
			m_ses.alerts().emplace_alert<torrent_checked_alert>(
				get_handle());
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			ext->on_files_checked();
		}
#endif

		bool const notify_initialized = !m_connections_initialized;
		m_connections_initialized = true;
		m_files_checked = true;

		update_want_tick();

		for (auto pc : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			// all peer connections have to initialize themselves now that the metadata
			// is available
			if (notify_initialized)
			{
				if (pc->is_disconnecting()) continue;
				pc->on_metadata_impl();
				if (pc->is_disconnecting()) continue;
				pc->init();
			}

#ifndef TORRENT_DISABLE_LOGGING
			pc->peer_log(peer_log_alert::info, "ON_FILES_CHECKED");
#endif
			if (pc->is_interesting() && !pc->has_peer_choked())
			{
				if (request_a_block(*this, *pc))
				{
					inc_stats_counter(counters::unchoke_piece_picks);
					pc->send_block_requests();
				}
			}
		}

		start_announcing();

		maybe_connect_web_seeds();
	}

	alert_manager& torrent::alerts() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_ses.alerts();
	}

	bool torrent::is_seed() const
	{
		if (!valid_metadata()) return false;
		if (m_seed_mode) return true;
		if (m_have_all) return true;
		if (m_picker && m_picker->num_passed() == m_picker->num_pieces()) return true;
		return m_state == torrent_status::seeding;
	}

	bool torrent::is_finished() const
	{
		if (is_seed()) return true;
		return valid_metadata() && has_picker() && m_picker->is_finished();
	}

	bool torrent::is_inactive() const
	{
		if (!settings().get_bool(settings_pack::dont_count_slow_torrents))
			return false;
		return m_inactive;
	}

	std::string torrent::save_path() const
	{
		return m_save_path;
	}

	void torrent::rename_file(file_index_t const index, std::string name)
	{
		INVARIANT_CHECK;

		file_storage const& fs = m_torrent_file->files();
		TORRENT_ASSERT(index >= file_index_t(0));
		TORRENT_ASSERT(index < fs.end_file());
		TORRENT_UNUSED(fs);

		// storage may be nullptr during shutdown
		if (!m_storage)
		{
			if (alerts().should_post<file_rename_failed_alert>())
				alerts().emplace_alert<file_rename_failed_alert>(get_handle()
					, index, errors::session_is_closing);
			return;
		}

		m_ses.disk_thread().async_rename_file(m_storage, index, std::move(name)
			, std::bind(&torrent::on_file_renamed, shared_from_this(), _1, _2, _3));
	}

	void torrent::move_storage(std::string const& save_path, move_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_abort)
		{
			if (alerts().should_post<storage_moved_failed_alert>())
				alerts().emplace_alert<storage_moved_failed_alert>(get_handle()
					, boost::asio::error::operation_aborted
					, "", operation_t::unknown);
			return;
		}

		// if we don't have metadata yet, we don't know anything about the file
		// structure and we have to assume we don't have any file.
		if (!valid_metadata())
		{
			if (alerts().should_post<storage_moved_alert>())
				alerts().emplace_alert<storage_moved_alert>(get_handle(), save_path);
#if TORRENT_USE_UNC_PATHS
			std::string path = canonicalize_path(save_path);
#else
			std::string const& path = save_path;
#endif
			m_save_path = complete(path);
			return;
		}

		// storage may be nullptr during shutdown
		if (m_storage)
		{
#if TORRENT_USE_UNC_PATHS
			std::string path = canonicalize_path(save_path);
#else
			std::string path = save_path;
#endif
			m_ses.disk_thread().async_move_storage(m_storage, std::move(path), flags
				, std::bind(&torrent::on_storage_moved, shared_from_this(), _1, _2, _3));
			m_moving_storage = true;
		}
		else
		{
#if TORRENT_USE_UNC_PATHS
			m_save_path = canonicalize_path(save_path);
#else

			m_save_path = save_path;
#endif
			set_need_save_resume();

			if (alerts().should_post<storage_moved_alert>())
			{
				alerts().emplace_alert<storage_moved_alert>(get_handle(), m_save_path);
			}
		}
	}

	void torrent::on_storage_moved(status_t const status, std::string const& path
		, storage_error const& error) try
	{
		TORRENT_ASSERT(is_single_thread());

		m_moving_storage = false;
		if (status == status_t::no_error
			|| status == status_t::need_full_check)
		{
			if (alerts().should_post<storage_moved_alert>())
				alerts().emplace_alert<storage_moved_alert>(get_handle(), path);
			m_save_path = path;
			set_need_save_resume();
			if (status == status_t::need_full_check)
				force_recheck();
		}
		else
		{
			if (alerts().should_post<storage_moved_failed_alert>())
				alerts().emplace_alert<storage_moved_failed_alert>(get_handle(), error.ec
					, resolve_filename(error.file()), error.operation);
		}
	}
	catch (...) { handle_exception(); }

	torrent_handle torrent::get_handle()
	{
		TORRENT_ASSERT(is_single_thread());
		return torrent_handle(shared_from_this());
	}

	aux::session_settings const& torrent::settings() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_ses.settings();
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void torrent::check_invariant() const
	{
		TORRENT_ASSERT(m_connections.size() >= m_outgoing_pids.size());

		// the piece picker and the file progress states are supposed to be
		// created in sync
		TORRENT_ASSERT(has_picker() == !m_file_progress.empty());
		TORRENT_ASSERT(current_stats_state() == int(m_current_gauge_state + counters::num_checking_torrents)
			|| m_current_gauge_state == no_gauge_state);

		TORRENT_ASSERT(m_sequence_number == no_pos
			|| m_ses.verify_queue_position(this, m_sequence_number));

#ifndef TORRENT_DISABLE_STREAMING
		for (auto const& i : m_time_critical_pieces)
		{
			TORRENT_ASSERT(!is_seed());
			TORRENT_ASSERT(!has_picker() || !m_picker->have_piece(i.piece));
		}
#endif

		switch (current_stats_state())
		{
			case counters::num_error_torrents: TORRENT_ASSERT(has_error()); break;
			case counters::num_checking_torrents:
#if TORRENT_ABI_VERSION == 1
				TORRENT_ASSERT(state() == torrent_status::checking_files
					|| state() == torrent_status::queued_for_checking);
#else
				TORRENT_ASSERT(state() == torrent_status::checking_files);
#endif
				break;
			case counters::num_seeding_torrents: TORRENT_ASSERT(is_seed()); break;
			case counters::num_upload_only_torrents: TORRENT_ASSERT(is_upload_only()); break;
			case counters::num_stopped_torrents: TORRENT_ASSERT(!is_auto_managed()
				&& (m_paused || m_graceful_pause_mode));
				break;
			case counters::num_queued_seeding_torrents:
				TORRENT_ASSERT((m_paused || m_graceful_pause_mode) && is_seed()); break;
		}

		if (m_torrent_file)
		{
			TORRENT_ASSERT(m_info_hash == m_torrent_file->info_hash());
		}

		for (torrent_list_index_t i{}; i != m_links.end_index(); ++i)
		{
			if (!m_links[i].in_list()) continue;
			int const index = m_links[i].index;

			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_ses.torrent_list(i).size()));
		}

		TORRENT_ASSERT(want_peers_download() == m_links[aux::session_interface::torrent_want_peers_download].in_list());
		TORRENT_ASSERT(want_peers_finished() == m_links[aux::session_interface::torrent_want_peers_finished].in_list());
		TORRENT_ASSERT(want_tick() == m_links[aux::session_interface::torrent_want_tick].in_list());
		TORRENT_ASSERT((m_paused && m_auto_managed && !m_abort) == m_links[aux::session_interface::torrent_want_scrape].in_list());

		bool is_checking = false;
		bool is_downloading = false;
		bool is_seeding = false;

		if (is_auto_managed() && !has_error())
		{
			if (m_state == torrent_status::checking_files
				|| m_state == torrent_status::allocating)
			{
				is_checking = true;
			}
			else if (m_state == torrent_status::downloading_metadata
				|| m_state == torrent_status::downloading
				|| m_state == torrent_status::finished
				|| m_state == torrent_status::seeding)
			{
				if (is_finished())
					is_seeding = true;
				else
					is_downloading = true;
			}
		}

		TORRENT_ASSERT(m_links[aux::session_interface::torrent_checking_auto_managed].in_list()
			== is_checking);
		TORRENT_ASSERT(m_links[aux::session_interface::torrent_downloading_auto_managed].in_list()
			== is_downloading);
		TORRENT_ASSERT(m_links[aux::session_interface::torrent_seeding_auto_managed].in_list()
			== is_seeding);

		if (m_seed_mode)
		{
			TORRENT_ASSERT(is_seed());
		}

		TORRENT_ASSERT(is_single_thread());
		// this fires during disconnecting peers
		if (is_paused()) TORRENT_ASSERT(num_peers() == 0 || m_graceful_pause_mode);

		int seeds = 0;
		int num_uploads = 0;
		int num_connecting = 0;
		int num_connecting_seeds = 0;
		std::map<piece_block, int> num_requests;
		for (peer_connection const* peer : *this)
		{
			peer_connection const& p = *peer;

			if (p.is_connecting()) ++num_connecting;

			if (p.is_connecting() && p.peer_info_struct()->seed)
				++num_connecting_seeds;

			if (p.peer_info_struct() && p.peer_info_struct()->seed)
				++seeds;

			for (auto const& j : p.request_queue())
			{
				if (!j.not_wanted && !j.timed_out) ++num_requests[j.block];
			}

			for (auto const& j : p.download_queue())
			{
				if (!j.not_wanted && !j.timed_out) ++num_requests[j.block];
			}

			if (!p.is_choked() && !p.ignore_unchoke_slots()) ++num_uploads;
			torrent* associated_torrent = p.associated_torrent().lock().get();
			if (associated_torrent != this && associated_torrent != nullptr)
				TORRENT_ASSERT_FAIL();
		}
		TORRENT_ASSERT_VAL(num_uploads == int(m_num_uploads), int(m_num_uploads) - num_uploads);
		TORRENT_ASSERT_VAL(seeds == int(m_num_seeds), int(m_num_seeds) - seeds);
		TORRENT_ASSERT_VAL(num_connecting == int(m_num_connecting), int(m_num_connecting) - num_connecting);
		TORRENT_ASSERT_VAL(num_connecting_seeds == int(m_num_connecting_seeds)
			, int(m_num_connecting_seeds) - num_connecting_seeds);
		TORRENT_ASSERT_VAL(int(m_num_uploads) <= num_peers(), m_num_uploads - num_peers());
		TORRENT_ASSERT_VAL(int(m_num_seeds) <= num_peers(), m_num_seeds - num_peers());
		TORRENT_ASSERT_VAL(int(m_num_connecting) <= num_peers(), int(m_num_connecting) - num_peers());
		TORRENT_ASSERT_VAL(int(m_num_connecting_seeds) <= num_peers(), int(m_num_connecting_seeds) - num_peers());
		TORRENT_ASSERT_VAL(int(m_num_connecting) + int(m_num_seeds) >= int(m_num_connecting_seeds)
			, int(m_num_connecting_seeds) - (int(m_num_connecting) + int(m_num_seeds)));
		TORRENT_ASSERT_VAL(int(m_num_connecting) + int(m_num_seeds) - int(m_num_connecting_seeds) <= num_peers()
			, num_peers() - (int(m_num_connecting) + int(m_num_seeds) - int(m_num_connecting_seeds)));

		if (has_picker())
		{
			for (std::map<piece_block, int>::iterator i = num_requests.begin()
				, end(num_requests.end()); i != end; ++i)
			{
				piece_block b = i->first;
				int count = i->second;
				int picker_count = m_picker->num_peers(b);
				// if we're no longer downloading the piece
				// (for instance, it may be fully downloaded and waiting
				// for the hash check to return), the piece picker always
				// returns 0 requests, regardless of how many peers may still
				// have the block in their queue
				if (!m_picker->is_downloaded(b) && m_picker->is_downloading(b.piece_index))
				{
					if (picker_count != count)
					{
						std::fprintf(stderr, "picker count discrepancy: "
							"picker: %d != peerlist: %d\n", picker_count, count);

						for (const_peer_iterator j = this->begin(); j != this->end(); ++j)
						{
							peer_connection const& p = *(*j);
							std::fprintf(stderr, "peer: %s\n", print_endpoint(p.remote()).c_str());
							for (auto const& k : p.request_queue())
							{
								std::fprintf(stderr, "  rq: (%d, %d) %s %s %s\n"
									, static_cast<int>(k.block.piece_index)
									, k.block.block_index, k.not_wanted ? "not-wanted" : ""
									, k.timed_out ? "timed-out" : "", k.busy ? "busy": "");
							}
							for (auto const& k : p.download_queue())
							{
								std::fprintf(stderr, "  dq: (%d, %d) %s %s %s\n"
									, static_cast<int>(k.block.piece_index)
									, k.block.block_index, k.not_wanted ? "not-wanted" : ""
									, k.timed_out ? "timed-out" : "", k.busy ? "busy": "");
							}
						}
						TORRENT_ASSERT_FAIL();
					}
				}
			}
		}

		if (valid_metadata())
		{
			TORRENT_ASSERT(m_abort || m_error || !m_picker || m_picker->num_pieces() == m_torrent_file->num_pieces());
		}
		else
		{
			TORRENT_ASSERT(m_abort || m_error || !m_picker || m_picker->num_pieces() == 0);
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		// make sure we haven't modified the peer object
		// in a way that breaks the sort order
		if (m_peer_list && m_peer_list->begin() != m_peer_list->end())
		{
			auto i = m_peer_list->begin();
			auto p = i++;
			auto end(m_peer_list->end());
			peer_address_compare cmp;
			for (; i != end; ++i, ++p)
			{
				TORRENT_ASSERT(!cmp(*i, *p));
			}
		}
#endif

/*
		if (m_picker && !m_abort)
		{
			// make sure that pieces that have completed the download
			// of all their blocks are in the disk io thread's queue
			// to be checked.
			std::vector<piece_picker::downloading_piece> dl_queue
				= m_picker->get_download_queue();
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				const int blocks_per_piece = m_picker->blocks_in_piece(i->index);

				bool complete = true;
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					if (i->info[j].state == piece_picker::block_info::state_finished)
						continue;
					complete = false;
					break;
				}
				TORRENT_ASSERT(complete);
			}
		}
*/
		if (m_files_checked && valid_metadata())
		{
			TORRENT_ASSERT(block_size() > 0);
		}
	}
#endif

	void torrent::set_sequential_download(bool const sd)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_sequential_download == sd) return;
		m_sequential_download = sd;
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** set-sequential-download: %d", sd);
#endif

		set_need_save_resume();

		state_updated();
	}

	void torrent::queue_up()
	{
		// finished torrents may not change their queue positions, as it's set to
		// -1
		if (m_abort || is_finished()) return;

		set_queue_position(queue_position() == queue_position_t{0}
			? queue_position() : prev(queue_position()));
	}

	void torrent::queue_down()
	{
		set_queue_position(next(queue_position()));
	}

	void torrent::set_queue_position(queue_position_t const p)
	{
		TORRENT_ASSERT(is_single_thread());

		// finished torrents may not change their queue positions, as it's set to
		// -1
		if ((m_abort || is_finished()) && p != no_pos) return;

		TORRENT_ASSERT((p == no_pos) == is_finished()
			|| (!m_auto_managed && p == no_pos)
			|| (m_abort && p == no_pos)
			|| (!m_added && p == no_pos));
		if (p == m_sequence_number) return;

		TORRENT_ASSERT(p >= no_pos);

		state_updated();

		m_ses.set_queue_position(this, p);
	}

	void torrent::set_max_uploads(int limit, bool const state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (1 << 24) - 1;
		if (int(m_max_uploads)!= limit && state_update) state_updated();
		m_max_uploads = aux::numeric_cast<std::uint32_t>(limit);
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log() && state_update)
			debug_log("*** set-max-uploads: %d", m_max_uploads);
#endif

		if (state_update)
			set_need_save_resume();
	}

	void torrent::set_max_connections(int limit, bool const state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (1 << 24) - 1;
		if (int(m_max_connections) != limit && state_update) state_updated();
		m_max_connections = aux::numeric_cast<std::uint32_t>(limit);
		update_want_peers();

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log() && state_update)
			debug_log("*** set-max-connections: %d", m_max_connections);
#endif

		if (num_peers() > int(m_max_connections))
		{
			disconnect_peers(num_peers() - m_max_connections
				, errors::too_many_connections);
		}

		if (state_update)
			set_need_save_resume();
	}

	void torrent::set_upload_limit(int const limit)
	{
		set_limit_impl(limit, peer_connection::upload_channel);
		set_need_save_resume();
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** set-upload-limit: %d", limit);
#endif
	}

	void torrent::set_download_limit(int const limit)
	{
		set_limit_impl(limit, peer_connection::download_channel);
		set_need_save_resume();
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** set-download-limit: %d", limit);
#endif
	}

	void torrent::set_limit_impl(int limit, int const channel, bool const state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = 0;

		if (m_peer_class == peer_class_t{0})
		{
			if (limit == 0) return;
			setup_peer_class();
		}

		struct peer_class* tpc = m_ses.peer_classes().at(m_peer_class);
		TORRENT_ASSERT(tpc);
		if (tpc->channel[channel].throttle() != limit && state_update)
			state_updated();
		tpc->channel[channel].throttle(limit);
	}

	void torrent::setup_peer_class()
	{
		TORRENT_ASSERT(m_peer_class == peer_class_t{0});
		m_peer_class = m_ses.peer_classes().new_peer_class(name());
		add_class(m_ses.peer_classes(), m_peer_class);
	}

	int torrent::limit_impl(int const channel) const
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_peer_class == peer_class_t{0}) return -1;
		int limit = m_ses.peer_classes().at(m_peer_class)->channel[channel].throttle();
		if (limit == std::numeric_limits<int>::max()) limit = -1;
		return limit;
	}

	int torrent::upload_limit() const
	{
		return limit_impl(peer_connection::upload_channel);
	}

	int torrent::download_limit() const
	{
		return limit_impl(peer_connection::download_channel);
	}

	bool torrent::delete_files(remove_flags_t const options)
	{
		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_LOGGING
		log_to_all_peers("deleting files");
#endif

		disconnect_all(errors::torrent_removed, operation_t::bittorrent);
		stop_announcing();

		// storage may be nullptr during shutdown
		if (m_storage)
		{
			TORRENT_ASSERT(m_storage);
			m_ses.disk_thread().async_delete_files(m_storage, options
				, std::bind(&torrent::on_files_deleted, shared_from_this(), _1));
			m_deleted = true;
			return true;
		}
		return false;
	}

	void torrent::clear_error()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_error) return;
		bool const checking_files = should_check_files();
		m_ses.trigger_auto_manage();
		m_error.clear();
		m_error_file = torrent_status::error_file_none;

		update_gauge();
		state_updated();
		update_want_peers();
		update_state_list();

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		// if we haven't downloaded the metadata from m_url, try again
		if (!m_url.empty() && !m_torrent_file->is_valid())
		{
			start_download_url();
			return;
		}
#endif
		// if the error happened during initialization, try again now
		if (!m_torrent_initialized && valid_metadata()) init();
		if (!checking_files && should_check_files())
			start_checking();
	}
	std::string torrent::resolve_filename(file_index_t const file) const
	{
		if (file == torrent_status::error_file_none) return "";
		if (file == torrent_status::error_file_ssl_ctx) return "SSL Context";
		if (file == torrent_status::error_file_exception) return "exception";
		if (file == torrent_status::error_file_partfile) return "partfile";
#if TORRENT_ABI_VERSION == 1
		if (file == torrent_status::error_file_url) return m_url;
		if (file == torrent_status::error_file_metadata) return "metadata (from user load function)";
#endif

		if (m_storage && file >= file_index_t(0))
		{
			file_storage const& st = m_torrent_file->files();
			return st.file_path(file, m_save_path);
		}
		else
		{
			return m_save_path;
		}
	}

	void torrent::set_error(error_code const& ec, file_index_t const error_file)
	{
		TORRENT_ASSERT(is_single_thread());
		m_error = ec;
		m_error_file = error_file;

		update_gauge();

		if (alerts().should_post<torrent_error_alert>())
			alerts().emplace_alert<torrent_error_alert>(get_handle(), ec
				, resolve_filename(error_file));

#ifndef TORRENT_DISABLE_LOGGING
		if (ec)
		{
			char buf[1024];
			std::snprintf(buf, sizeof(buf), "error %s: %s", ec.message().c_str()
				, resolve_filename(error_file).c_str());
			log_to_all_peers(buf);
		}
#endif

		state_updated();
		update_state_list();
	}

	void torrent::auto_managed(bool a)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_auto_managed == a) return;
		bool const checking_files = should_check_files();
		m_auto_managed = a;
		update_gauge();
		update_want_scrape();
		update_state_list();

		state_updated();

		// we need to save this new state as well
		set_need_save_resume();

		// recalculate which torrents should be
		// paused
		m_ses.trigger_auto_manage();

		if (!checking_files && should_check_files())
		{
			start_checking();
		}
	}

	namespace {

	std::uint16_t clamped_subtract_u16(int const a, int const b)
	{
		if (a < b) return 0;
		return std::uint16_t(a - b);
	}

	} // anonymous namespace

	// this is called every time the session timer takes a step back. Since the
	// session time is meant to fit in 16 bits, it only covers a range of
	// about 18 hours. This means every few hours the whole epoch of this
	// clock is shifted forward. All timestamp in this clock must then be
	// shifted backwards to remain the same. Anything that's shifted back
	// beyond the new epoch is clamped to 0 (to represent the oldest timestamp
	// currently representable by the session_time)
	void torrent::step_session_time(int const seconds)
	{
		if (m_peer_list)
		{
			for (auto pe : *m_peer_list)
			{
				pe->last_optimistically_unchoked
					= clamped_subtract_u16(pe->last_optimistically_unchoked, seconds);
				pe->last_connected = clamped_subtract_u16(pe->last_connected, seconds);
			}
		}
	}

	// the higher seed rank, the more important to seed
	int torrent::seed_rank(aux::session_settings const& s) const
	{
		TORRENT_ASSERT(is_single_thread());
		enum flags
		{
			seed_ratio_not_met = 0x40000000,
			no_seeds           = 0x20000000,
			recently_started   = 0x10000000,
			prio_mask          = 0x0fffffff
		};

		if (!is_finished()) return 0;

		int scale = 1000;
		if (!is_seed()) scale = 500;

		int ret = 0;

		seconds32 const act_time = active_time();
		seconds32 const fin_time = finished_time();
		seconds32 const download_time = act_time - fin_time;

		// if we haven't yet met the seed limits, set the seed_ratio_not_met
		// flag. That will make this seed prioritized
		// downloaded may be 0 if the torrent is 0-sized
		std::int64_t const downloaded = std::max(m_total_downloaded, m_torrent_file->total_size());
		if (fin_time < seconds(s.get_int(settings_pack::seed_time_limit))
			&& (download_time.count() > 1
				&& fin_time * 100 / download_time < s.get_int(settings_pack::seed_time_ratio_limit))
			&& downloaded > 0
			&& m_total_uploaded * 100 / downloaded < s.get_int(settings_pack::share_ratio_limit))
			ret |= seed_ratio_not_met;

		// if this torrent is running, and it was started less
		// than 30 minutes ago, give it priority, to avoid oscillation
		if (!is_paused() && act_time < minutes(30))
			ret |= recently_started;

		// if we have any scrape data, use it to calculate
		// seed rank
		int seeds = 0;
		int downloaders = 0;

		if (m_complete != 0xffffff) seeds = m_complete;
		else seeds = m_peer_list ? m_peer_list->num_seeds() : 0;

		if (m_incomplete != 0xffffff) downloaders = m_incomplete;
		else downloaders = m_peer_list ? m_peer_list->num_peers() - m_peer_list->num_seeds() : 0;

		if (seeds == 0)
		{
			ret |= no_seeds;
			ret |= downloaders & prio_mask;
		}
		else
		{
			ret |= ((1 + downloaders) * scale / seeds) & prio_mask;
		}

		return ret;
	}

	// this is an async operation triggered by the client
	// TODO: add a flag to ignore stats, and only care about resume data for
	// content. For unchanged files, don't trigger a load of the metadata
	// just to save an empty resume data file
	void torrent::save_resume_data(resume_data_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (!valid_metadata())
		{
			alerts().emplace_alert<save_resume_data_failed_alert>(get_handle()
				, errors::no_metadata);
			return;
		}

		if ((flags & torrent_handle::only_if_modified) && !m_need_save_resume_data)
		{
			alerts().emplace_alert<save_resume_data_failed_alert>(get_handle()
				, errors::resume_data_not_modified);
			return;
		}

		m_need_save_resume_data = false;
		m_save_resume_flags = flags;
		state_updated();

		if ((flags & torrent_handle::flush_disk_cache) && m_storage)
			m_ses.disk_thread().async_release_files(m_storage);

		state_updated();

		add_torrent_params atp;
		write_resume_data(atp);
		alerts().emplace_alert<save_resume_data_alert>(std::move(atp), get_handle());
	}

	bool torrent::should_check_files() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_state == torrent_status::checking_files
			&& !m_paused
			&& !has_error()
			&& !m_abort
			&& !m_session_paused;
	}

	void torrent::flush_cache()
	{
		TORRENT_ASSERT(is_single_thread());

		// storage may be nullptr during shutdown
		if (!m_storage)
		{
			TORRENT_ASSERT(m_abort);
			return;
		}
		m_ses.disk_thread().async_release_files(m_storage
			, std::bind(&torrent::on_cache_flushed, shared_from_this(), true));
	}

	void torrent::on_cache_flushed(bool const manually_triggered) try
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_ses.is_aborted()) return;

		if (manually_triggered || alerts().should_post<cache_flushed_alert>())
			alerts().emplace_alert<cache_flushed_alert>(get_handle());
	}
	catch (...) { handle_exception(); }

	void torrent::on_torrent_aborted()
	{
		TORRENT_ASSERT(is_single_thread());

		// there should be no more disk activity for this torrent now, we can
		// release the disk io handle
		m_storage.reset();
	}

	bool torrent::is_paused() const
	{
		return m_paused || m_session_paused;
	}

	void torrent::pause(pause_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (!m_paused)
		{
			// we need to save this new state
			set_need_save_resume();
		}

		set_paused(true, flags | torrent_handle::clear_disk_cache);
	}

	void torrent::do_pause(pause_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!is_paused()) return;

		// this torrent may be about to consider itself inactive. If so, we want
		// to prevent it from doing so, since it's being paused unconditionally
		// now. An illustrative example of this is a torrent that completes
		// downloading when active_seeds = 0. It completes, it gets paused and it
		// should not come back to life again.
		if (m_pending_active_change)
		{
			m_inactivity_timer.cancel();
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			if (ext->on_pause()) return;
		}
#endif

		m_connect_boost_counter
			= static_cast<std::uint8_t>(settings().get_int(settings_pack::torrent_connect_boost));
		m_inactive = false;

		update_state_list();
		update_want_tick();

		const time_point now = aux::time_now();

		m_active_time +=
			duration_cast<seconds32>(now - m_started);

		if (is_seed()) m_seeding_time +=
			duration_cast<seconds32>(now - m_became_seed);

		if (is_finished()) m_finished_time +=
			duration_cast<seconds32>(now - m_became_finished);

		m_announce_to_dht = false;
		m_announce_to_trackers = false;
		m_announce_to_lsd = false;

		state_updated();
		update_want_peers();
		update_want_scrape();
		update_gauge();
		update_state_list();

#ifndef TORRENT_DISABLE_LOGGING
		log_to_all_peers("pausing");
#endif

		// when checking and being paused in graceful pause mode, we
		// post the paused alert when the last outstanding disk job completes
		if (m_state == torrent_status::checking_files)
		{
			if (m_checking_piece == m_num_checked_pieces)
			{
				if (alerts().should_post<torrent_paused_alert>())
					alerts().emplace_alert<torrent_paused_alert>(get_handle());
			}
			disconnect_all(errors::torrent_paused, operation_t::bittorrent);
			return;
		}

		if (!m_graceful_pause_mode)
		{
			// this will make the storage close all
			// files and flush all cached data
			if (m_storage && (flags & torrent_handle::clear_disk_cache))
			{
				// the torrent_paused alert will be posted from on_torrent_paused
				m_ses.disk_thread().async_stop_torrent(m_storage
					, std::bind(&torrent::on_torrent_paused, shared_from_this()));
			}
			else
			{
				if (alerts().should_post<torrent_paused_alert>())
					alerts().emplace_alert<torrent_paused_alert>(get_handle());
			}

			disconnect_all(errors::torrent_paused, operation_t::bittorrent);
		}
		else
		{
			// disconnect all peers with no outstanding data to receive
			// and choke all remaining peers to prevent responding to new
			// requests
			for (auto p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

				if (p->is_disconnecting()) continue;

				if (p->outstanding_bytes() > 0)
				{
#ifndef TORRENT_DISABLE_LOGGING
					p->peer_log(peer_log_alert::info, "CHOKING_PEER", "torrent graceful paused");
#endif
					// remove any un-sent requests from the queue
					p->clear_request_queue();
					// don't accept new requests from the peer
					p->choke_this_peer();
					continue;
				}

				// since we're currently in graceful pause mode, the last peer to
				// disconnect (assuming all peers end up begin disconnected here)
				// will post the torrent_paused_alert
#ifndef TORRENT_DISABLE_LOGGING
				p->peer_log(peer_log_alert::info, "CLOSING_CONNECTION", "torrent_paused");
#endif
				p->disconnect(errors::torrent_paused, operation_t::bittorrent);
			}
		}

		stop_announcing();
	}

#ifndef TORRENT_DISABLE_LOGGING
	void torrent::log_to_all_peers(char const* message)
	{
		TORRENT_ASSERT(is_single_thread());

		bool const log_peers = !m_connections.empty()
			&& m_connections.front()->should_log(peer_log_alert::info);

		if (log_peers)
		{
			for (auto const p : m_connections)
			{
				TORRENT_INCREMENT(m_iterating_connections);
				p->peer_log(peer_log_alert::info, "TORRENT", "%s", message);
			}
		}

		debug_log("%s", message);
	}
#endif

	// add or remove a url that will be attempted for
	// finding the file(s) in this torrent.
	web_seed_t* torrent::add_web_seed(std::string const& url
		, web_seed_entry::type_t const type
		, std::string const& auth
		, web_seed_entry::headers_t const& extra_headers
		, web_seed_flag_t const flags)
	{
		web_seed_t ent(url, type, auth, extra_headers);
		ent.ephemeral = bool(flags & ephemeral);

		// don't add duplicates
		auto const it = std::find(m_web_seeds.begin(), m_web_seeds.end(), ent);
		if (it != m_web_seeds.end()) return &*it;
		m_web_seeds.push_back(ent);
		set_need_save_resume();
		update_want_tick();
		return &m_web_seeds.back();
	}

	void torrent::set_session_paused(bool const b)
	{
		if (m_session_paused == b) return;
		bool const paused_before = is_paused();
		m_session_paused = b;

		if (paused_before == is_paused()) return;

		if (b) do_pause();
		else do_resume();
	}

	void torrent::set_paused(bool const b, pause_flags_t flags)
	{
		TORRENT_ASSERT(is_single_thread());

		// if there are no peers, there is no point in a graceful pause mode. In
		// fact, the promise to post the torrent_paused_alert exactly once is
		// maintained by the last peer to be disconnected in graceful pause mode,
		// if there are no peers, we must not enter graceful pause mode, and post
		// the torrent_paused_alert immediately instead.
		if (num_peers() == 0)
			flags &= ~torrent_handle::graceful_pause;

		if (m_paused == b)
		{
			// there is one special case here. If we are
			// currently in graceful pause mode, and we just turned into regular
			// paused mode, we need to actually pause the torrent properly
			if (m_paused == true
				&& m_graceful_pause_mode == true
				&& !(flags & torrent_handle::graceful_pause))
			{
				m_graceful_pause_mode = false;
				update_gauge();
				do_pause();
			}
			return;
		}

		bool const paused_before = is_paused();

		m_paused = b;

		// the session may still be paused, in which case
		// the effective state of the torrent did not change
		if (paused_before == is_paused()) return;

		m_graceful_pause_mode = bool(flags & torrent_handle::graceful_pause);

		if (b) do_pause(flags & torrent_handle::clear_disk_cache);
		else do_resume();
	}

	void torrent::resume()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (!m_paused
			&& m_announce_to_dht
			&& m_announce_to_trackers
			&& m_announce_to_lsd) return;

		m_announce_to_dht = true;
		m_announce_to_trackers = true;
		m_announce_to_lsd = true;
		m_paused = false;
		if (!m_session_paused) m_graceful_pause_mode = false;

		update_gauge();

		// we need to save this new state
		set_need_save_resume();

		do_resume();
	}

	void torrent::do_resume()
	{
		TORRENT_ASSERT(is_single_thread());
		if (is_paused())
		{
			update_want_tick();
			return;
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			if (ext->on_resume()) return;
		}
#endif

		if (alerts().should_post<torrent_resumed_alert>())
			alerts().emplace_alert<torrent_resumed_alert>(get_handle());

		m_started = aux::time_now32();
		if (is_seed()) m_became_seed = m_started;
		if (is_finished()) m_became_finished = m_started;

		clear_error();

		if (m_state == torrent_status::checking_files)
		{
			if (m_auto_managed) m_ses.trigger_auto_manage();
			if (should_check_files()) start_checking();
		}

		state_updated();
		update_want_peers();
		update_want_tick();
		update_want_scrape();
		update_gauge();

		if (should_check_files()) start_checking();

		if (m_state == torrent_status::checking_files) return;

		start_announcing();

		do_connect_boost();
	}

	namespace
	{
		struct timer_state
		{
			explicit timer_state(aux::listen_socket_handle const& s)
				: socket(s) {}

			aux::listen_socket_handle socket;

			int tier = INT_MAX;
			bool found_working = false;
			bool done = false;
		};
	}

	void torrent::update_tracker_timer(time_point32 const now)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_announcing)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** update tracker timer: not announcing");
#endif
			return;
		}

		time_point32 next_announce = time_point32::max();

		std::vector<timer_state> listen_socket_states;

#ifndef TORRENT_DISABLE_LOGGING
		int idx = -1;
		if (should_log())
		{
			debug_log("*** update_tracker_timer: "
				"[ announce_to_all_tiers: %d announce_to_all_trackers: %d num_trackers: %d ]"
				, settings().get_bool(settings_pack::announce_to_all_tiers)
				, settings().get_bool(settings_pack::announce_to_all_trackers)
				, int(m_trackers.size()));
		}
#endif
		for (auto const& t : m_trackers)
		{
#ifndef TORRENT_DISABLE_LOGGING
			++idx;
#endif
			for (auto const& aep : t.endpoints)
			{
				auto aep_state_iter = std::find_if(listen_socket_states.begin(), listen_socket_states.end()
					, [&](timer_state const& s) { return s.socket == aep.socket; });
				if (aep_state_iter == listen_socket_states.end())
				{
					listen_socket_states.emplace_back(aep.socket);
					aep_state_iter = listen_socket_states.end() - 1;
				}
				timer_state& state = *aep_state_iter;

				if (state.done) continue;

				if (settings().get_bool(settings_pack::announce_to_all_tiers)
					&& state.found_working
					&& t.tier <= state.tier
					&& state.tier != INT_MAX)
					continue;

				if (t.tier > state.tier && !settings().get_bool(settings_pack::announce_to_all_tiers)) break;
				if (aep.is_working()) { state.tier = t.tier; state.found_working = false; }
				if (aep.fails >= t.fail_limit && t.fail_limit != 0) continue;
				if (!aep.enabled) continue;

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					debug_log("*** tracker: (%d) [ep: %s ] \"%s\" [ found: %d i->tier: %d tier: %d"
						" working: %d fails: %d limit: %d upd: %d ]"
						, idx, print_endpoint(aep.local_endpoint).c_str(), t.url.c_str()
						, state.found_working, t.tier, state.tier, aep.is_working()
						, aep.fails, t.fail_limit, aep.updating);
				}
#endif

				if (aep.updating)
				{
					state.found_working = true;
				}
				else
				{
					time_point32 const next_tracker_announce = std::max(aep.next_announce, aep.min_announce);
					if (next_tracker_announce < next_announce
						&& (!state.found_working || aep.is_working()))
						next_announce = next_tracker_announce;
				}
				if (aep.is_working()) state.found_working = true;
				if (state.found_working
					&& !settings().get_bool(settings_pack::announce_to_all_trackers)
					&& !settings().get_bool(settings_pack::announce_to_all_tiers))
					state.done = true;
			}

			if (std::all_of(listen_socket_states.begin(), listen_socket_states.end()
				, [](timer_state const& s) { return s.done; }))
				break;
		}

		if (next_announce <= now) next_announce = now;

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** update tracker timer: next_announce < now %d"
			" m_waiting_tracker: %d next_announce_in: %d"
			, next_announce <= now, m_waiting_tracker
			, int(total_seconds(next_announce - now)));
#endif

		// don't re-issue the timer if it's the same expiration time as last time
		// if m_waiting_tracker is 0, expires_at() is undefined
		if (m_waiting_tracker && m_tracker_timer.expires_at() == next_announce) return;

		error_code ec;
		auto self = shared_from_this();

		m_tracker_timer.expires_at(next_announce, ec);
		ADD_OUTSTANDING_ASYNC("tracker::on_tracker_announce");
		++m_waiting_tracker;
		m_tracker_timer.async_wait([self](error_code const& e)
			{ self->wrap(&torrent::on_tracker_announce, e); });
	}

	void torrent::start_announcing()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(state() != torrent_status::checking_files);
		if (is_paused())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("start_announcing(), paused");
#endif
			return;
		}
		// if we don't have metadata, we need to announce
		// before checking files, to get peers to
		// request the metadata from
		if (!m_files_checked && valid_metadata())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("start_announcing(), files not checked (with valid metadata)");
#endif
			return;
		}
#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		if (!m_torrent_file->is_valid() && !m_url.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("start_announcing(), downloading URL");
#endif
			return;
		}
#endif
		if (m_announcing) return;

		m_announcing = true;

#ifndef TORRENT_DISABLE_DHT
		if ((!m_peer_list || m_peer_list->num_peers() < 50) && m_ses.dht())
		{
			// we don't have any peers, prioritize
			// announcing this torrent with the DHT
			m_ses.prioritize_dht(shared_from_this());
		}
#endif

		if (!m_trackers.empty())
		{
			// tell the tracker that we're back
			for (auto& t : m_trackers) t.reset();
		}

		// reset the stats, since from the tracker's
		// point of view, this is a new session
		m_total_failed_bytes = 0;
		m_total_redundant_bytes = 0;
		m_stat.clear();

		update_want_tick();

		announce_with_tracker();

		lsd_announce();
	}

	void torrent::stop_announcing()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_announcing) return;

		error_code ec;
		m_tracker_timer.cancel(ec);

		m_announcing = false;

		time_point32 const now = aux::time_now32();
		for (auto& t : m_trackers)
		{
			for (auto& aep : t.endpoints)
			{
				aep.next_announce = now;
				aep.min_announce = now;
			}
		}
		announce_with_tracker(tracker_request::stopped);
	}

	seconds32 torrent::finished_time() const
	{
		if(!is_finished() || is_paused())
			return m_finished_time;

		return m_finished_time + duration_cast<seconds32>(
			aux::time_now() - m_became_finished);
	}

	seconds32 torrent::active_time() const
	{
		if(is_paused())
			return m_active_time;

		// m_active_time does not account for the current "session", just the
		// time before we last started this torrent. To get the current time, we
		// need to add the time since we started it
		return m_active_time + duration_cast<seconds32>(
			aux::time_now() - m_started);
	}

	seconds32 torrent::seeding_time() const
	{
		if(!is_seed() || is_paused())
			return m_seeding_time;
		// m_seeding_time does not account for the current "session", just the
		// time before we last started this torrent. To get the current time, we
		// need to add the time since we started it
		return m_seeding_time + duration_cast<seconds32>(
			aux::time_now() - m_became_seed);
	}

	seconds32 torrent::upload_mode_time() const
	{
		if(!m_upload_mode)
			return seconds32(0);

		return aux::time_now32() - m_upload_mode_time;
	}

	void torrent::second_tick(int const tick_interval_ms)
	{
		TORRENT_ASSERT(want_tick());
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		auto self = shared_from_this();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_extensions)
		{
			ext->tick();
		}

		if (m_abort) return;
#endif

		// if we're in upload only mode and we're auto-managed
		// leave upload mode every 10 minutes hoping that the error
		// condition has been fixed
		if (m_upload_mode && m_auto_managed && upload_mode_time() >=
			seconds(settings().get_int(settings_pack::optimistic_disk_retry)))
		{
			set_upload_mode(false);
		}

		if (is_paused() && !m_graceful_pause_mode)
		{
			// let the stats fade out to 0
			// check the rate before ticking the stats so that the last update is sent
			// with the rate equal to zero
			if (m_stat.low_pass_upload_rate() > 0 || m_stat.low_pass_download_rate() > 0)
				state_updated();
			m_stat.second_tick(tick_interval_ms);

			// the low pass transfer rate may just have dropped to 0
			update_want_tick();

			return;
		}

		if (settings().get_bool(settings_pack::rate_limit_ip_overhead))
		{
			int const up_limit = upload_limit();
			int const down_limit = download_limit();

			if (down_limit > 0
				&& m_stat.download_ip_overhead() >= down_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().emplace_alert<performance_alert>(get_handle()
					, performance_alert::download_limit_too_low);
			}

			if (up_limit > 0
				&& m_stat.upload_ip_overhead() >= up_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().emplace_alert<performance_alert>(get_handle()
					, performance_alert::upload_limit_too_low);
			}
		}

#ifndef TORRENT_DISABLE_STREAMING
		// ---- TIME CRITICAL PIECES ----

#if TORRENT_DEBUG_STREAMING > 0
		std::vector<partial_piece_info> queue;
		get_download_queue(&queue);

		std::vector<peer_info> peer_list;
		get_peer_info(peer_list);

		std::sort(queue.begin(), queue.end(), std::bind(&partial_piece_info::piece_index, _1)
			< std::bind(&partial_piece_info::piece_index, _2));

		std::printf("average piece download time: %.2f s (+/- %.2f s)\n"
			, m_average_piece_time / 1000.f
			, m_piece_time_deviation / 1000.f);
		for (auto& i : queue)
		{
			extern void print_piece(libtorrent::partial_piece_info* pp
				, std::vector<libtorrent::peer_info> const& peers
				, std::vector<time_critical_piece> const& time_critical);

			print_piece(&i, peer_list, m_time_critical_pieces);
		}
#endif // TORRENT_DEBUG_STREAMING

		if (!m_time_critical_pieces.empty() && !upload_mode())
		{
			request_time_critical_pieces();
		}
#endif // TORRENT_DISABLE_STREAMING

		// ---- WEB SEEDS ----

		maybe_connect_web_seeds();

		m_swarm_last_seen_complete = m_last_seen_complete;
		for (auto p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);

			// look for the peer that saw a seed most recently
			m_swarm_last_seen_complete = std::max(p->last_seen_complete(), m_swarm_last_seen_complete);

			// updates the peer connection's ul/dl bandwidth
			// resource requests
			p->second_tick(tick_interval_ms);
		}
		if (m_ses.alerts().should_post<stats_alert>())
			m_ses.alerts().emplace_alert<stats_alert>(get_handle(), tick_interval_ms, m_stat);

		m_total_uploaded += m_stat.last_payload_uploaded();
		m_total_downloaded += m_stat.last_payload_downloaded();
		m_stat.second_tick(tick_interval_ms);

		// these counters are saved in the resume data, since they updated
		// we need to save the resume data too
		m_need_save_resume_data = true;

		// if the rate is 0, there's no update because of network transfers
		if (m_stat.low_pass_upload_rate() > 0 || m_stat.low_pass_download_rate() > 0)
			state_updated();

		// this section determines whether the torrent is active or not. When it
		// changes state, it may also trigger the auto-manage logic to reconsider
		// which torrents should be queued and started. There is a low pass
		// filter in order to avoid flapping (auto_manage_startup).
		bool is_inactive = is_inactive_internal();

		if (settings().get_bool(settings_pack::dont_count_slow_torrents))
		{
			if (is_inactive != m_inactive && !m_pending_active_change)
			{
				int const delay = settings().get_int(settings_pack::auto_manage_startup);
				m_inactivity_timer.expires_from_now(seconds(delay));
				m_inactivity_timer.async_wait([self](error_code const& ec) {
					self->wrap(&torrent::on_inactivity_tick, ec); });
				m_pending_active_change = true;
			}
			else if (is_inactive == m_inactive
				&& m_pending_active_change)
			{
				m_inactivity_timer.cancel();
			}
		}

		// want_tick depends on whether the low pass transfer rates are non-zero
		// or not. They may just have turned zero in this last tick.
		update_want_tick();
	}

	bool torrent::is_inactive_internal() const
	{
		if (is_finished())
			return m_stat.upload_payload_rate()
				< settings().get_int(settings_pack::inactive_up_rate);
		else
			return m_stat.download_payload_rate()
				< settings().get_int(settings_pack::inactive_down_rate);
	}

	void torrent::on_inactivity_tick(error_code const& ec) try
	{
		m_pending_active_change = false;

		if (ec) return;

		bool const is_inactive = is_inactive_internal();
		if (is_inactive == m_inactive) return;

		m_inactive = is_inactive;

		update_state_list();
		update_want_tick();

		if (settings().get_bool(settings_pack::dont_count_slow_torrents))
			m_ses.trigger_auto_manage();
	}
	catch (...) { handle_exception(); }

	namespace {
		int zero_or(int const val, int const def_val)
		{ return (val <= 0) ? def_val : val; }
	}

	void torrent::maybe_connect_web_seeds()
	{
		if (m_abort) return;

		// if we have everything we want we don't need to connect to any web-seed
		if (m_web_seeds.empty()
			|| is_finished()
			|| !m_files_checked
			|| num_peers() >= int(m_max_connections)
			|| m_ses.num_connections() >= settings().get_int(settings_pack::connections_limit))
		{
			return;
		}

		// when set to unlimited, use 100 as the limit
		int limit = zero_or(settings().get_int(settings_pack::max_web_seed_connections)
			, 100);

		auto const now = aux::time_now32();

		// keep trying web-seeds if there are any
		// first find out which web seeds we are connected to
		for (auto i = m_web_seeds.begin(); i != m_web_seeds.end() && limit > 0;)
		{
			auto const w = i++;
			if (w->removed || w->retry > now || !w->interesting)
				continue;

			--limit;
			if (w->peer_info.connection || w->resolving)
				continue;

			connect_to_url_seed(w);
		}
	}

#ifndef TORRENT_DISABLE_SHARE_MODE
	void torrent::recalc_share_mode()
	{
		TORRENT_ASSERT(share_mode());
		if (is_seed()) return;

		int const pieces_in_torrent = m_torrent_file->num_pieces();
		int num_seeds = 0;
		int num_peers = 0;
		int num_downloaders = 0;
		int missing_pieces = 0;
		int num_interested = 0;
		for (auto const p : m_connections)
		{
			TORRENT_INCREMENT(m_iterating_connections);
			if (p->is_connecting()) continue;
			if (p->is_disconnecting()) continue;
			++num_peers;
			if (p->is_seed())
			{
				++num_seeds;
				continue;
			}

			if (p->share_mode()) continue;
			if (p->upload_only()) continue;

			if (p->is_peer_interested()) ++num_interested;

			++num_downloaders;
			missing_pieces += pieces_in_torrent - p->num_have_pieces();
		}

		if (num_peers == 0) return;

		if (num_seeds * 100 / num_peers > 50
			&& (num_peers * 100 / m_max_connections > 90
				|| num_peers > 20))
		{
			// we are connected to more than 50% seeds (and we're beyond
			// 90% of the max number of connections). That will
			// limit our ability to upload. We need more downloaders.
			// disconnect some seeds so that we don't have more than 50%
			int const to_disconnect = num_seeds - num_peers / 2;
			aux::vector<peer_connection*> seeds;
			seeds.reserve(num_seeds);
			std::copy_if(m_connections.begin(), m_connections.end(), std::back_inserter(seeds)
				, [](peer_connection const* p) { return p->is_seed(); });

			aux::random_shuffle(seeds);
			TORRENT_ASSERT(to_disconnect <= seeds.end_index());
			for (auto const& p : span<peer_connection*>(seeds).first(to_disconnect))
				p->disconnect(errors::upload_upload_connection, operation_t::bittorrent);
		}

		if (num_downloaders == 0) return;

		// assume that the seeds are about as fast as us. During the time
		// we can download one piece, and upload one piece, each seed
		// can upload two pieces.
		missing_pieces -= 2 * num_seeds;

		if (missing_pieces <= 0) return;

		// missing_pieces represents our opportunity to download pieces
		// and share them more than once each

		// now, download at least one piece, otherwise download one more
		// piece if our downloaded (and downloading) pieces is less than 50%
		// of the uploaded bytes
		int const num_downloaded_pieces = std::max(m_picker->have().num_pieces
			, m_picker->want().num_pieces);

		if (std::int64_t(num_downloaded_pieces) * m_torrent_file->piece_length()
			* settings().get_int(settings_pack::share_mode_target) > m_total_uploaded
			&& num_downloaded_pieces > 0)
			return;

		// don't have more pieces downloading in parallel than 5% of the total
		// number of pieces we have downloaded
		if (m_picker->get_download_queue_size() > num_downloaded_pieces / 20)
			return;

		// one more important property is that there are enough pieces
		// that more than one peer wants to download
		// make sure that there are enough downloaders for the rarest
		// piece. Go through all pieces, figure out which one is the rarest
		// and how many peers that has that piece

		aux::vector<piece_index_t> rarest_pieces;

		int const num_pieces = m_torrent_file->num_pieces();
		int rarest_rarity = INT_MAX;
		for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
		{
			piece_picker::piece_stats_t ps = m_picker->piece_stats(i);
			if (ps.peer_count == 0) continue;
			if (ps.priority == 0 && (ps.have || ps.downloading))
			{
				m_picker->set_piece_priority(i, default_priority);
				continue;
			}
			// don't count pieces we already have or are trying to download
			if (ps.priority > 0 || ps.have) continue;
			if (ps.peer_count > rarest_rarity) continue;
			if (ps.peer_count == rarest_rarity)
			{
				rarest_pieces.push_back(i);
				continue;
			}

			rarest_pieces.clear();
			rarest_rarity = ps.peer_count;
			rarest_pieces.push_back(i);
		}

		update_gauge();
		update_want_peers();

		// now, rarest_pieces is a list of all pieces that are the rarest ones.
		// and rarest_rarity is the number of peers that have the rarest pieces

		// if there's only a single peer that doesn't have the rarest piece
		// it's impossible for us to download one piece and upload it
		// twice. i.e. we cannot get a positive share ratio
		if (num_peers - rarest_rarity
			< settings().get_int(settings_pack::share_mode_target))
			return;

		// now, pick one of the rarest pieces to download
		int const pick = int(random(aux::numeric_cast<std::uint32_t>(rarest_pieces.end_index() - 1)));
		bool const was_finished = is_finished();
		m_picker->set_piece_priority(rarest_pieces[pick], default_priority);
		update_gauge();
		update_peer_interest(was_finished);
		update_want_peers();
	}
#endif // TORRENT_DISABLE_SHARE_MODE

	void torrent::sent_bytes(int const bytes_payload, int const bytes_protocol)
	{
		m_stat.sent_bytes(bytes_payload, bytes_protocol);
		m_ses.sent_bytes(bytes_payload, bytes_protocol);
	}

	void torrent::received_bytes(int const bytes_payload, int const bytes_protocol)
	{
		m_stat.received_bytes(bytes_payload, bytes_protocol);
		m_ses.received_bytes(bytes_payload, bytes_protocol);
	}

	void torrent::trancieve_ip_packet(int const bytes, bool const ipv6)
	{
		m_stat.trancieve_ip_packet(bytes, ipv6);
		m_ses.trancieve_ip_packet(bytes, ipv6);
	}

	void torrent::sent_syn(bool const ipv6)
	{
		m_stat.sent_syn(ipv6);
		m_ses.sent_syn(ipv6);
	}

	void torrent::received_synack(bool const ipv6)
	{
		m_stat.received_synack(ipv6);
		m_ses.received_synack(ipv6);
	}

#ifndef TORRENT_DISABLE_STREAMING

#if TORRENT_DEBUG_STREAMING > 0
	char const* esc(char const* code)
	{
		// this is a silly optimization
		// to avoid copying of strings
		int const num_strings = 200;
		static char buf[num_strings][20];
		static int round_robin = 0;
		char* ret = buf[round_robin];
		++round_robin;
		if (round_robin >= num_strings) round_robin = 0;
		ret[0] = '\033';
		ret[1] = '[';
		int i = 2;
		int j = 0;
		while (code[j]) ret[i++] = code[j++];
		ret[i++] = 'm';
		ret[i++] = 0;
		return ret;
	}

	int peer_index(libtorrent::tcp::endpoint addr
		, std::vector<libtorrent::peer_info> const& peers)
	{
		std::vector<peer_info>::const_iterator i = std::find_if(peers.begin()
			, peers.end(), std::bind(&peer_info::ip, _1) == addr);
		if (i == peers.end()) return -1;

		return i - peers.begin();
	}

	void print_piece(libtorrent::partial_piece_info* pp
		, std::vector<libtorrent::peer_info> const& peers
		, std::vector<time_critical_piece> const& time_critical)
	{
		time_point const now = clock_type::now();

		float deadline = 0.f;
		float last_request = 0.f;
		int timed_out = -1;

		int piece = pp->piece_index;
		std::vector<time_critical_piece>::const_iterator i
			= std::find_if(time_critical.begin(), time_critical.end()
				, std::bind(&time_critical_piece::piece, _1) == piece);
		if (i != time_critical.end())
		{
			deadline = total_milliseconds(i->deadline - now) / 1000.f;
			if (i->last_requested == min_time())
				last_request = -1;
			else
				last_request = total_milliseconds(now - i->last_requested) / 1000.f;
			timed_out = i->timed_out;
		}

		int num_blocks = pp->blocks_in_piece;

		std::printf("%5d: [", piece);
		for (int j = 0; j < num_blocks; ++j)
		{
			int index = pp ? peer_index(pp->blocks[j].peer(), peers) % 36 : -1;
			char chr = '+';
			if (index >= 0)
				chr = (index < 10)?'0' + index:'A' + index - 10;

			char const* color = "";
			char const* multi_req = "";

			if (pp->blocks[j].num_peers > 1)
				multi_req = esc("1");

			if (pp->blocks[j].bytes_progress > 0
				&& pp->blocks[j].state == block_info::requested)
			{
				color = esc("33;7");
				chr = '0' + (pp->blocks[j].bytes_progress * 10 / pp->blocks[j].block_size);
			}
			else if (pp->blocks[j].state == block_info::finished) color = esc("32;7");
			else if (pp->blocks[j].state == block_info::writing) color = esc("36;7");
			else if (pp->blocks[j].state == block_info::requested) color = esc("0");
			else { color = esc("0"); chr = ' '; }

			std::printf("%s%s%c%s", color, multi_req, chr, esc("0"));
		}
		std::printf("%s]", esc("0"));
		if (deadline != 0.f)
			std::printf(" deadline: %f last-req: %f timed_out: %d\n"
				, deadline, last_request, timed_out);
		else
			std::printf("\n");
	}
#endif // TORRENT_DEBUG_STREAMING

	namespace {

	struct busy_block_t
	{
		int peers;
		int index;
		bool operator<(busy_block_t const& rhs) const { return peers < rhs.peers; }
	};

	void pick_busy_blocks(piece_picker const* picker
		, piece_index_t const piece
		, int const blocks_in_piece
		, int const timed_out
		, std::vector<piece_block>& interesting_blocks
		, piece_picker::downloading_piece const& pi)
	{
		// if there aren't any free blocks in the piece, and the piece is
		// old enough, we may switch into busy mode for this piece. In this
		// case busy_blocks and busy_count are set to contain the eligible
		// busy blocks we may pick
		// first, figure out which blocks are eligible for picking
		// in "busy-mode"
		TORRENT_ALLOCA(busy_blocks, busy_block_t, blocks_in_piece);
		int busy_count = 0;

		// pick busy blocks from the piece
		int idx = -1;
		for (auto const& info : picker->blocks_for_piece(pi))
		{
			++idx;
			// only consider blocks that have been requested
			// and we're still waiting for them
			if (info.state != piece_picker::block_info::state_requested)
				continue;

			piece_block b(piece, idx);

			// only allow a single additional request per block, in order
			// to spread it out evenly across all stalled blocks
			if (int(info.num_peers) > timed_out)
				continue;

			busy_blocks[busy_count].peers = info.num_peers;
			busy_blocks[busy_count].index = idx;
			++busy_count;

#if TORRENT_DEBUG_STREAMING > 1
			std::printf(" [%d (%d)]", b.block_index, info.num_peers);
#endif
		}
#if TORRENT_DEBUG_STREAMING > 1
		std::printf("\n");
#endif

		busy_blocks = busy_blocks.first(busy_count);

		// then sort blocks by the number of peers with requests
		// to the blocks (request the blocks with the fewest peers
		// first)
		std::sort(busy_blocks.begin(), busy_blocks.end());

		// then insert them into the interesting_blocks vector
		for (auto const& block : busy_blocks)
			interesting_blocks.emplace_back(piece, block.index);
	}

	void pick_time_critical_block(std::vector<peer_connection*>& peers
		, std::vector<peer_connection*>& ignore_peers
		, std::set<peer_connection*>& peers_with_requests
		, piece_picker::downloading_piece const& pi
		, time_critical_piece* i
		, piece_picker const* picker
		, int const blocks_in_piece
		, int const timed_out)
	{
		std::vector<piece_block> interesting_blocks;
		std::vector<piece_block> backup1;
		std::vector<piece_block> backup2;
		std::vector<piece_index_t> ignore;

		time_point const now = aux::time_now();

		// loop until every block has been requested from this piece (i->piece)
		do
		{
			// if this peer's download time exceeds 2 seconds, we're done.
			// We don't want to build unreasonably long request queues
			if (!peers.empty() && peers[0]->download_queue_time() > milliseconds(2000))
			{
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("queue time: %d ms, done\n"
					, int(total_milliseconds(peers[0]->download_queue_time())));
#endif
				break;
			}

			// pick the peer with the lowest download_queue_time that has i->piece
			auto p = std::find_if(peers.begin(), peers.end()
				, std::bind(&peer_connection::has_piece, _1, i->piece));

			// obviously we'll have to skip it if we don't have a peer that has
			// this piece
			if (p == peers.end())
			{
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("out of peers, done\n");
#endif
				break;
			}
			peer_connection& c = **p;

			interesting_blocks.clear();
			backup1.clear();
			backup2.clear();

			// specifically request blocks with no affinity towards fast or slow
			// pieces. If we would, the picked block might end up in one of
			// the backup lists
			picker->add_blocks(i->piece, c.get_bitfield(), interesting_blocks
				, backup1, backup2, blocks_in_piece, 0, c.peer_info_struct()
				, ignore, {});

			interesting_blocks.insert(interesting_blocks.end()
				, backup1.begin(), backup1.end());
			interesting_blocks.insert(interesting_blocks.end()
				, backup2.begin(), backup2.end());

			bool busy_mode = false;

			if (interesting_blocks.empty())
			{
				busy_mode = true;

#if TORRENT_DEBUG_STREAMING > 1
				std::printf("interesting_blocks.empty()\n");
#endif

				// there aren't any free blocks to pick, and the piece isn't
				// old enough to pick busy blocks yet. break to continue to
				// the next piece.
				if (timed_out == 0)
				{
#if TORRENT_DEBUG_STREAMING > 1
					std::printf("not timed out, moving on to next piece\n");
#endif
					break;
				}

#if TORRENT_DEBUG_STREAMING > 1
				std::printf("pick busy blocks\n");
#endif

				pick_busy_blocks(picker, i->piece, blocks_in_piece, timed_out
					, interesting_blocks, pi);
			}

			// we can't pick anything from this piece, we're done with it.
			// move on to the next one
			if (interesting_blocks.empty()) break;

			piece_block const b = interesting_blocks.front();

			// in busy mode we need to make sure we don't do silly
			// things like requesting the same block twice from the
			// same peer
			std::vector<pending_block> const& dq = c.download_queue();

			bool const already_requested = std::find_if(dq.begin(), dq.end()
				, aux::has_block(b)) != dq.end();

			if (already_requested)
			{
				// if the piece is stalled, we may end up picking a block
				// that we've already requested from this peer. If so, we should
				// simply disregard this peer from this piece, since this peer
				// is likely to be causing the stall. We should request it
				// from the next peer in the list
				// the peer will be put back in the set for the next piece
				ignore_peers.push_back(*p);
				peers.erase(p);
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("piece already requested by peer, try next peer\n");
#endif
				// try next peer
				continue;
			}

			std::vector<pending_block> const& rq = c.request_queue();

			bool const already_in_queue = std::find_if(rq.begin(), rq.end()
				, aux::has_block(b)) != rq.end();

			if (already_in_queue)
			{
				if (!c.make_time_critical(b))
				{
#if TORRENT_DEBUG_STREAMING > 1
					std::printf("piece already time-critical and in queue for peer, trying next peer\n");
#endif
					ignore_peers.push_back(*p);
					peers.erase(p);
					continue;
				}
				i->last_requested = now;

#if TORRENT_DEBUG_STREAMING > 1
				std::printf("piece already in queue for peer, making time-critical\n");
#endif

				// we inserted a new block in the request queue, this
				// makes us actually send it later
				peers_with_requests.insert(peers_with_requests.begin(), &c);
			}
			else
			{
				if (!c.add_request(b, peer_connection::time_critical
					| (busy_mode ? peer_connection::busy : request_flags_t{})))
				{
#if TORRENT_DEBUG_STREAMING > 1
					std::printf("failed to request block [%d, %d]\n"
						, b.piece_index, b.block_index);
#endif
					ignore_peers.push_back(*p);
					peers.erase(p);
					continue;
				}

#if TORRENT_DEBUG_STREAMING > 1
				std::printf("requested block [%d, %d]\n"
					, b.piece_index, b.block_index);
#endif
				peers_with_requests.insert(peers_with_requests.begin(), &c);
			}

			if (!busy_mode) i->last_requested = now;

			if (i->first_requested == min_time()) i->first_requested = now;

			if (!c.can_request_time_critical())
			{
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("peer cannot pick time critical pieces\n");
#endif
				peers.erase(p);
				// try next peer
				continue;
			}

			// resort p, since it will have a higher download_queue_time now
			while (p != peers.end()-1 && (*p)->download_queue_time()
				> (*(p+1))->download_queue_time())
			{
				std::iter_swap(p, p+1);
				++p;
			}
		} while (!interesting_blocks.empty());
	}

	} // anonymous namespace

	void torrent::request_time_critical_pieces()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!upload_mode());

		// build a list of peers and sort it by download_queue_time
		// we use this sorted list to determine which peer we should
		// request a block from. The earlier a peer is in the list,
		// the sooner we will fully download the block we request.
		aux::vector<peer_connection*> peers;
		peers.reserve(num_peers());

		// some peers are marked as not being able to request time critical
		// blocks from. For instance, peers that have choked us, peers that are
		// on parole (i.e. they are believed to have sent us bad data), peers
		// that are being disconnected, in upload mode etc.
		std::remove_copy_if(m_connections.begin(), m_connections.end()
			, std::back_inserter(peers), [] (peer_connection* p)
			{ return !p->can_request_time_critical(); });

		// sort by the time we believe it will take this peer to send us all
		// blocks we've requested from it. The shorter time, the better candidate
		// it is to request a time critical block from.
		std::sort(peers.begin(), peers.end()
			, [] (peer_connection const* lhs, peer_connection const* rhs)
			{ return lhs->download_queue_time(16*1024) < rhs->download_queue_time(16*1024); });

		// remove the bottom 10% of peers from the candidate set.
		// this is just to remove outliers that might stall downloads
		int const new_size = (peers.end_index() * 9 + 9) / 10;
		TORRENT_ASSERT(new_size <= peers.end_index());
		peers.resize(new_size);

		// remember all the peers we issued requests to, so we can commit them
		// at the end of this function. Instead of sending the requests right
		// away, we batch them up and send them in a single write to the TCP
		// socket, increasing the chance that they will all be sent in the same
		// packet.
		std::set<peer_connection*> peers_with_requests;

		// peers that should be temporarily ignored for a specific piece
		// in order to give priority to other peers. They should be used for
		// subsequent pieces, so they are stored in this vector until the
		// piece is done
		std::vector<peer_connection*> ignore_peers;

		time_point const now = clock_type::now();

		// now, iterate over all time critical pieces, in order of importance, and
		// request them from the peers, in order of responsiveness. i.e. request
		// the most time critical pieces from the fastest peers.
		bool first_piece{true};
		for (auto& i : m_time_critical_pieces)
		{
#if TORRENT_DEBUG_STREAMING > 1
			std::printf("considering %d\n", i->piece);
#endif

			if (peers.empty())
			{
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("out of peers, done\n");
#endif
				break;
			}

			// the +1000 is to compensate for the fact that we only call this
			// function once per second, so if we need to request it 500 ms from
			// now, we should request it right away
			if (!first_piece && i.deadline > now
				+ milliseconds(m_average_piece_time + m_piece_time_deviation * 4 + 1000))
			{
				// don't request pieces whose deadline is too far in the future
				// this is one of the termination conditions. We don't want to
				// send requests for all pieces in the torrent right away
#if TORRENT_DEBUG_STREAMING > 0
				std::printf("reached deadline horizon [%f + %f * 4 + 1]\n"
					, m_average_piece_time / 1000.f
					, m_piece_time_deviation / 1000.f);
#endif
				break;
			}
			first_piece = false;

			piece_picker::downloading_piece pi;
			m_picker->piece_info(i.piece, pi);

			// the number of "times" this piece has timed out.
			int timed_out = 0;

			int const blocks_in_piece = m_picker->blocks_in_piece(i.piece);

#if TORRENT_DEBUG_STREAMING > 0
			i.timed_out = timed_out;
#endif
			int const free_to_request = blocks_in_piece
				- pi.finished - pi.writing - pi.requested;

			if (free_to_request == 0)
			{
				if (i.last_requested == min_time())
					i.last_requested = now;

				// if it's been more than half of the typical download time
				// of a piece since we requested the last block, allow
				// one more request per block
				if (m_average_piece_time > 0)
					timed_out = int(total_milliseconds(now - i.last_requested)
						/ std::max(int(m_average_piece_time + m_piece_time_deviation / 2), 1));

#if TORRENT_DEBUG_STREAMING > 0
				i.timed_out = timed_out;
#endif
				// every block in this piece is already requested
				// there's no need to consider this piece, unless it
				// appears to be stalled.
				if (pi.requested == 0 || timed_out == 0)
				{
#if TORRENT_DEBUG_STREAMING > 1
					std::printf("skipping %d (full) [req: %d timed_out: %d ]\n"
						, i.piece, pi.requested
						, timed_out);
#endif

					// if requested is 0, it means all blocks have been received, and
					// we're just waiting for it to flush them to disk.
					// if last_requested is recent enough, we should give it some
					// more time
					// skip to the next piece
					continue;
				}

				// it's been too long since we requested the last block from
				// this piece. Allow re-requesting blocks from this piece
#if TORRENT_DEBUG_STREAMING > 1
				std::printf("timed out [average-piece-time: %d ms ]\n"
					, m_average_piece_time);
#endif
			}

			// pick all blocks for this piece. the peers list is kept up to date
			// and sorted. when we issue a request to a peer, its download queue
			// time will increase and it may need to be bumped in the peers list,
			// since it's ordered by download queue time
			pick_time_critical_block(peers, ignore_peers
				, peers_with_requests
				, pi, &i, m_picker.get()
				, blocks_in_piece, timed_out);

			// put back the peers we ignored into the peer list for the next piece
			if (!ignore_peers.empty())
			{
				peers.insert(peers.begin(), ignore_peers.begin(), ignore_peers.end());
				ignore_peers.clear();

				// TODO: instead of resorting the whole list, insert the peers
				// directly into the right place
				std::sort(peers.begin(), peers.end()
					, [] (peer_connection const* lhs, peer_connection const* rhs)
					{ return lhs->download_queue_time(16*1024) < rhs->download_queue_time(16*1024); });
			}

			// if this peer's download time exceeds 2 seconds, we're done.
			// We don't want to build unreasonably long request queues
			if (!peers.empty() && peers[0]->download_queue_time() > milliseconds(2000))
				break;
		}

		// commit all the time critical requests
		for (auto p : peers_with_requests)
		{
			p->send_block_requests();
		}
	}
#endif // TORRENT_DISABLE_STREAMING

	std::set<std::string> torrent::web_seeds(web_seed_entry::type_t const type) const
	{
		TORRENT_ASSERT(is_single_thread());
		std::set<std::string> ret;
		for (auto const& s : m_web_seeds)
		{
			if (s.peer_info.banned) continue;
			if (s.removed) continue;
			if (s.type != type) continue;
			ret.insert(s.url);
		}
		return ret;
	}

	void torrent::remove_web_seed(std::string const& url, web_seed_entry::type_t const type)
	{
		auto const i = std::find_if(m_web_seeds.begin(), m_web_seeds.end()
			, [&] (web_seed_t const& w) { return w.url == url && w.type == type; });

		if (i != m_web_seeds.end())
		{
			remove_web_seed_iter(i);
			set_need_save_resume();
		}
	}

	void torrent::disconnect_web_seed(peer_connection* p)
	{
		auto const i = std::find_if(m_web_seeds.begin(), m_web_seeds.end()
			, [p] (web_seed_t const& ws) { return ws.peer_info.connection == p; });

		// this happens if the web server responded with a redirect
		// or with something incorrect, so that we removed the web seed
		// immediately, before we disconnected
		if (i == m_web_seeds.end()) return;

		TORRENT_ASSERT(i->resolving == false);

		TORRENT_ASSERT(i->peer_info.connection);
		i->peer_info.connection = nullptr;
	}

	void torrent::remove_web_seed_conn(peer_connection* p, error_code const& ec
		, operation_t const op, disconnect_severity_t const error)
	{
		auto const i = std::find_if(m_web_seeds.begin(), m_web_seeds.end()
			, [p] (web_seed_t const& ws) { return ws.peer_info.connection == p; });

		TORRENT_ASSERT(i != m_web_seeds.end());
		if (i == m_web_seeds.end()) return;

		auto* peer = static_cast<peer_connection*>(i->peer_info.connection);
		if (peer != nullptr)
		{
			// if we have a connection for this web seed, we also need to
			// disconnect it and clear its reference to the peer_info object
			// that's part of the web_seed_t we're about to remove
			TORRENT_ASSERT(peer->m_in_use == 1337);
			peer->disconnect(ec, op, error);
			peer->set_peer_info(nullptr);
		}
		remove_web_seed_iter(i);
	}

	void torrent::retry_web_seed(peer_connection* p, boost::optional<seconds32> const retry)
	{
		TORRENT_ASSERT(is_single_thread());
		auto const i = std::find_if(m_web_seeds.begin(), m_web_seeds.end()
			, [p] (web_seed_t const& ws) { return ws.peer_info.connection == p; });

		TORRENT_ASSERT(i != m_web_seeds.end());
		if (i == m_web_seeds.end()) return;
		if (i->removed) return;
		i->retry = aux::time_now32() + value_or(retry, seconds32(
			settings().get_int(settings_pack::urlseed_wait_retry)));
	}

	torrent_state torrent::get_peer_list_state()
	{
		torrent_state ret;
		ret.is_paused = is_paused();
		ret.is_finished = is_finished();
		ret.allow_multiple_connections_per_ip = settings().get_bool(settings_pack::allow_multiple_connections_per_ip);
		ret.max_peerlist_size = is_paused()
			? settings().get_int(settings_pack::max_paused_peerlist_size)
			: settings().get_int(settings_pack::max_peerlist_size);
		ret.min_reconnect_time = settings().get_int(settings_pack::min_reconnect_time);

		ret.ip = m_ses.external_address();
		ret.port = m_ses.listen_port();
		ret.max_failcount = settings().get_int(settings_pack::max_failcount);
		return ret;
	}

	bool torrent::try_connect_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(want_peers());

		torrent_state st = get_peer_list_state();
		need_peer_list();
		torrent_peer* p = m_peer_list->connect_one_peer(m_ses.session_time(), &st);
		peers_erased(st.erased);
		inc_stats_counter(counters::connection_attempt_loops, st.loop_counter);

		if (p == nullptr)
		{
			m_stats_counters.inc_stats_counter(counters::no_peer_connection_attempts);
			update_want_peers();
			return false;
		}

		if (!connect_to_peer(p))
		{
			m_stats_counters.inc_stats_counter(counters::missed_connection_attempts);
			m_peer_list->inc_failcount(p);
			update_want_peers();
			return false;
		}
		update_want_peers();

		return true;
	}

	torrent_peer* torrent::add_peer(tcp::endpoint const& adr
		, peer_source_flags_t const source, pex_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_DHT
		if (source != peer_info::resume_data)
		{
			// try to send a DHT ping to this peer
			// as well, to figure out if it supports
			// DHT (uTorrent and BitComet don't
			// advertise support)
			session().add_dht_node({adr.address(), adr.port()});
		}
#endif

		if (m_apply_ip_filter
			&& m_ip_filter
			&& m_ip_filter->access(adr.address()) & ip_filter::blocked)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, adr, peer_blocked_alert::ip_filter);

#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return nullptr;
		}

		if (m_ses.get_port_filter().access(adr.port()) & port_filter::blocked)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, adr, peer_blocked_alert::port_filter);
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return nullptr;
		}

#if TORRENT_USE_I2P
		// if this is an i2p torrent, and we don't allow mixed mode
		// no regular peers should ever be added!
		if (!settings().get_bool(settings_pack::allow_i2p_mixed) && is_i2p())
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, adr, peer_blocked_alert::i2p_mixed);
			return nullptr;
		}
#endif

		if (settings().get_bool(settings_pack::no_connect_privileged_ports) && adr.port() < 1024)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, adr, peer_blocked_alert::privileged_ports);
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return nullptr;
		}

		need_peer_list();
		torrent_state st = get_peer_list_state();
		torrent_peer* p = m_peer_list->add_peer(adr, source, flags, &st);
		peers_erased(st.erased);

		if (p)
		{
			state_updated();
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source
				, st.first_time_seen
					? torrent_plugin::first_time
					: add_peer_flags_t{});
#endif
		}
		else
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
		}
		update_want_peers();
		state_updated();
		return p;
	}

	bool torrent::ban_peer(torrent_peer* tp)
	{
		if (!settings().get_bool(settings_pack::ban_web_seeds) && tp->web_seed)
			return false;

		need_peer_list();
		if (!m_peer_list->ban_peer(tp)) return false;
		update_want_peers();

		inc_stats_counter(counters::num_banned_peers);
		return true;
	}

	void torrent::set_seed(torrent_peer* p, bool const s)
	{
		if (p->seed != s)
		{
			if (s)
			{
				TORRENT_ASSERT(m_num_seeds < 0xffff);
				++m_num_seeds;
			}
			else
			{
				TORRENT_ASSERT(m_num_seeds > 0);
				--m_num_seeds;
			}
		}

		need_peer_list();
		m_peer_list->set_seed(p, s);
		update_auto_sequential();
	}

	void torrent::clear_failcount(torrent_peer* p)
	{
		need_peer_list();
		m_peer_list->set_failcount(p, 0);
		update_want_peers();
	}

	std::pair<peer_list::iterator, peer_list::iterator> torrent::find_peers(address const& a)
	{
		need_peer_list();
		return m_peer_list->find_peers(a);
	}

	void torrent::update_peer_port(int const port, torrent_peer* p
		, peer_source_flags_t const src)
	{
		need_peer_list();
		torrent_state st = get_peer_list_state();
		m_peer_list->update_peer_port(port, p, src, &st);
		peers_erased(st.erased);
		update_want_peers();
	}

	// verify piece is used when checking resume data or when the user
	// adds a piece
	void torrent::verify_piece(piece_index_t const piece)
	{
//		picker().mark_as_checking(piece);

		TORRENT_ASSERT(m_storage);

		m_ses.disk_thread().async_hash(m_storage, piece, {}
			, std::bind(&torrent::on_piece_verified, shared_from_this(), _1, _2, _3));
	}

	announce_entry* torrent::find_tracker(std::string const& url)
	{
		auto i = std::find_if(m_trackers.begin(), m_trackers.end()
			, [&url](announce_entry const& ae) { return ae.url == url; });
		if (i == m_trackers.end()) return nullptr;
		return &*i;
	}

	void torrent::ip_filter_updated()
	{
		if (!m_apply_ip_filter) return;
		if (!m_peer_list) return;
		if (!m_ip_filter) return;

		torrent_state st = get_peer_list_state();
		std::vector<address> banned;
		m_peer_list->apply_ip_filter(*m_ip_filter, &st, banned);

		if (alerts().should_post<peer_blocked_alert>())
		{
			for (auto const& addr : banned)
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, tcp::endpoint(addr, 0)
					, peer_blocked_alert::ip_filter);
		}

		peers_erased(st.erased);
	}

	void torrent::port_filter_updated()
	{
		if (!m_apply_ip_filter) return;
		if (!m_peer_list) return;

		torrent_state st = get_peer_list_state();
		std::vector<address> banned;
		m_peer_list->apply_port_filter(m_ses.get_port_filter(), &st, banned);

		if (alerts().should_post<peer_blocked_alert>())
		{
			for (auto const& addr : banned)
				alerts().emplace_alert<peer_blocked_alert>(get_handle()
					, tcp::endpoint(addr, 0)
					, peer_blocked_alert::port_filter);
		}

		peers_erased(st.erased);
	}

	// this is called when torrent_peers are removed from the peer_list
	// (peer-list). It removes any references we may have to those torrent_peers,
	// so we don't leave then dangling
	void torrent::peers_erased(std::vector<torrent_peer*> const& peers)
	{
		if (!has_picker()) return;

		for (auto const p : peers)
		{
			m_picker->clear_peer(p);
		}
#if TORRENT_USE_INVARIANT_CHECKS
		m_picker->check_peers();
#endif
	}

#if TORRENT_ABI_VERSION == 1
#if !TORRENT_NO_FPU
	void torrent::file_progress_float(aux::vector<float, file_index_t>& fp)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!valid_metadata())
		{
			fp.clear();
			return;
		}

		fp.resize(m_torrent_file->num_files(), 1.f);
		if (is_seed()) return;

		aux::vector<std::int64_t, file_index_t> progress;
		file_progress(progress);
		file_storage const& fs = m_torrent_file->files();
		for (auto const i : fs.file_range())
		{
			std::int64_t file_size = m_torrent_file->files().file_size(i);
			if (file_size == 0) fp[i] = 1.f;
			else fp[i] = float(progress[i]) / file_size;
		}
	}
#endif
#endif // TORRENT_ABI_VERSION

	void torrent::file_progress(aux::vector<std::int64_t, file_index_t>& fp, int const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!valid_metadata())
		{
			fp.clear();
			return;
		}

		// if we're a seed, we don't have an m_file_progress anyway
		// since we don't need one. We know we have all files
		// just fill in the full file sizes as a shortcut
		if (is_seed())
		{
			fp.resize(m_torrent_file->num_files());
			file_storage const& fs = m_torrent_file->files();
			for (auto const i : fs.file_range())
				fp[i] = fs.file_size(i);
			return;
		}

		if (num_have() == 0 || m_file_progress.empty())
		{
			// if we don't have any pieces, just return zeroes
			fp.clear();
			fp.resize(m_torrent_file->num_files(), 0);
			return;
		}

		m_file_progress.export_progress(fp);

		if (flags & torrent_handle::piece_granularity)
			return;

		TORRENT_ASSERT(has_picker());

		std::vector<piece_picker::downloading_piece> q = m_picker->get_download_queue();

		file_storage const& fs = m_torrent_file->files();
		for (auto const& dp : q)
		{
			std::int64_t offset = std::int64_t(static_cast<int>(dp.index))
				* m_torrent_file->piece_length();
			file_index_t file = fs.file_index_at_offset(offset);
			int idx = -1;
			for (auto const& info : m_picker->blocks_for_piece(dp))
			{
				++idx;
				TORRENT_ASSERT(file < fs.end_file());
				TORRENT_ASSERT(offset == std::int64_t(static_cast<int>(dp.index))
					* m_torrent_file->piece_length()
					+ idx * block_size());
				TORRENT_ASSERT(offset < m_torrent_file->total_size());
				while (offset >= fs.file_offset(file) + fs.file_size(file))
				{
					++file;
				}
				TORRENT_ASSERT(file < fs.end_file());

				std::int64_t block = block_size();

				if (info.state == piece_picker::block_info::state_none)
				{
					offset += block;
					continue;
				}

				if (info.state == piece_picker::block_info::state_requested)
				{
					block = 0;
					torrent_peer* p = info.peer;
					if (p != nullptr && p->connection)
					{
						auto* peer = static_cast<peer_connection*>(p->connection);
						auto pbp = peer->downloading_piece_progress();
						if (pbp.piece_index == dp.index && pbp.block_index == idx)
							block = pbp.bytes_downloaded;
						TORRENT_ASSERT(block <= block_size());
					}

					if (block == 0)
					{
						offset += block_size();
						continue;
					}
				}

				if (offset + block > fs.file_offset(file) + fs.file_size(file))
				{
					std::int64_t left_over = block_size() - block;
					// split the block on multiple files
					while (block > 0)
					{
						TORRENT_ASSERT(offset <= fs.file_offset(file) + fs.file_size(file));
						std::int64_t const slice = std::min(fs.file_offset(file) + fs.file_size(file) - offset
							, block);
						fp[file] += slice;
						offset += slice;
						block -= slice;
						TORRENT_ASSERT(offset <= fs.file_offset(file) + fs.file_size(file));
						if (offset == fs.file_offset(file) + fs.file_size(file))
						{
							++file;
							if (file == fs.end_file())
							{
								offset += block;
								break;
							}
						}
					}
					offset += left_over;
					TORRENT_ASSERT(offset == std::int64_t(static_cast<int>(dp.index))
						* m_torrent_file->piece_length()
						+ (idx + 1) * block_size());
				}
				else
				{
					fp[file] += block;
					offset += block_size();
				}
				TORRENT_ASSERT(file <= fs.end_file());
			}
		}
	}

	void torrent::new_external_ip()
	{
		if (m_peer_list) m_peer_list->clear_peer_prio();
	}

	void torrent::stop_when_ready(bool const b)
	{
		m_stop_when_ready = b;

		// to avoid race condition, if we're already in a downloading state,
		// trigger the stop-when-ready logic immediately.
		if (m_stop_when_ready && is_downloading_state(m_state))
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("stop_when_ready triggered");
#endif
			auto_managed(false);
			pause();
			m_stop_when_ready = false;
		}
	}

	void torrent::set_state(torrent_status::state_t const s)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(s != 0); // this state isn't used anymore

#if TORRENT_USE_ASSERTS

		if (s == torrent_status::seeding)
		{
			TORRENT_ASSERT(is_seed());
			TORRENT_ASSERT(is_finished());
		}
		if (s == torrent_status::finished)
			TORRENT_ASSERT(is_finished());
		if (s == torrent_status::downloading && m_state == torrent_status::finished)
			TORRENT_ASSERT(!is_finished());
#endif

		if (int(m_state) == s) return;

		if (m_ses.alerts().should_post<state_changed_alert>())
		{
			m_ses.alerts().emplace_alert<state_changed_alert>(get_handle()
				, s, static_cast<torrent_status::state_t>(m_state));
		}

		if (s == torrent_status::finished
			&& alerts().should_post<torrent_finished_alert>())
		{
			alerts().emplace_alert<torrent_finished_alert>(
				get_handle());
		}

		if (m_stop_when_ready
			&& !is_downloading_state(m_state)
			&& is_downloading_state(s))
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("stop_when_ready triggered");
#endif
			// stop_when_ready is set, and we're transitioning from a downloading
			// state to a non-downloading state. pause the torrent. Note that
			// "downloading" is defined broadly to include any state where we
			// either upload or download (for the purpose of this flag).
			auto_managed(false);
			pause();
			m_stop_when_ready = false;
		}

		m_state = s;

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("set_state() %d", m_state);
#endif

		update_gauge();
		update_want_peers();
		update_want_tick();
		update_state_list();

		state_updated();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_extensions)
		{
			ext->on_state(state());
		}
#endif
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void torrent::notify_extension_add_peer(tcp::endpoint const& ip
		, peer_source_flags_t const src, add_peer_flags_t const flags)
	{
		for (auto& ext : m_extensions)
		{
			ext->on_add_peer(ip, src, flags);
		}
	}
#endif

	void torrent::state_updated()
	{
		// if this fails, this function is probably called
		// from within the torrent constructor, which it
		// shouldn't be. Whichever function ends up calling
		// this should probably be moved to torrent::start()
		TORRENT_ASSERT(shared_from_this());

		// we can't call state_updated() while the session
		// is building the status update alert
		TORRENT_ASSERT(!m_ses.is_posting_torrent_updates());

		// we're not subscribing to this torrent, don't add it
		if (!m_state_subscription) return;

		aux::vector<torrent*>& list = m_ses.torrent_list(aux::session_interface::torrent_state_updates);

		// if it has already been updated this round, no need to
		// add it to the list twice
		if (m_links[aux::session_interface::torrent_state_updates].in_list())
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_ASSERT(find(list.begin(), list.end(), this) != list.end());
#endif
			return;
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_ASSERT(find(list.begin(), list.end(), this) == list.end());
#endif

		m_links[aux::session_interface::torrent_state_updates].insert(list, this);
	}

	void torrent::status(torrent_status* st, status_flags_t const flags)
	{
		INVARIANT_CHECK;

		time_point32 const now = aux::time_now32();

		st->handle = get_handle();
		st->info_hash = info_hash();
#if TORRENT_ABI_VERSION == 1
		st->is_loaded = true;
#endif

		if (flags & torrent_handle::query_name)
			st->name = name();

		if (flags & torrent_handle::query_save_path)
			st->save_path = save_path();

		if (flags & torrent_handle::query_torrent_file)
			st->torrent_file = m_torrent_file;

		st->has_incoming = m_has_incoming;
		st->errc = m_error;
		st->error_file = m_error_file;

#if TORRENT_ABI_VERSION == 1
		if (m_error) st->error = convert_from_native(m_error.message())
			+ ": " + resolve_filename(m_error_file);
		st->seed_mode = m_seed_mode;
#endif
		st->moving_storage = m_moving_storage;

		st->announcing_to_trackers = m_announce_to_trackers;
		st->announcing_to_lsd = m_announce_to_lsd;
		st->announcing_to_dht = m_announce_to_dht;
#if TORRENT_ABI_VERSION == 1
		st->stop_when_ready = m_stop_when_ready;
#endif

		st->added_time = m_added_time;
		st->completed_time = m_completed_time;

#if TORRENT_ABI_VERSION == 1
		st->last_scrape = static_cast<int>(total_seconds(aux::time_now32() - m_last_scrape));
#endif

#if TORRENT_ABI_VERSION == 1
#ifndef TORRENT_DISABLE_SHARE_MODE
		st->share_mode = m_share_mode;
#else
		st->share_mode = false;
#endif
		st->upload_mode = m_upload_mode;
#endif
		st->up_bandwidth_queue = 0;
		st->down_bandwidth_queue = 0;
#if TORRENT_ABI_VERSION == 1
		st->priority = priority();
#endif

		st->num_peers = num_peers() - m_num_connecting;

		st->list_peers = m_peer_list ? m_peer_list->num_peers() : 0;
		st->list_seeds = m_peer_list ? m_peer_list->num_seeds() : 0;
		st->connect_candidates = m_peer_list ? m_peer_list->num_connect_candidates() : 0;
		TORRENT_ASSERT(st->connect_candidates >= 0);
		st->seed_rank = seed_rank(settings());

		st->all_time_upload = m_total_uploaded;
		st->all_time_download = m_total_downloaded;

		// activity time
#if TORRENT_ABI_VERSION == 1
		st->finished_time = int(total_seconds(finished_time()));
		st->active_time = int(total_seconds(active_time()));
		st->seeding_time = int(total_seconds(seeding_time()));

		time_point32 const unset{seconds32(0)};

		st->time_since_upload = m_last_upload == unset ? -1
			: static_cast<int>(total_seconds(aux::time_now32() - m_last_upload));
		st->time_since_download = m_last_download == unset ? -1
			: static_cast<int>(total_seconds(aux::time_now32() - m_last_download));
#endif

		st->finished_duration = finished_time();
		st->active_duration = active_time();
		st->seeding_duration = seeding_time();

		st->last_upload = m_last_upload;
		st->last_download = m_last_download;

		st->storage_mode = static_cast<storage_mode_t>(m_storage_mode);

		st->num_complete = (m_complete == 0xffffff) ? -1 : m_complete;
		st->num_incomplete = (m_incomplete == 0xffffff) ? -1 : m_incomplete;
#if TORRENT_ABI_VERSION == 1
		st->paused = is_torrent_paused();
		st->auto_managed = m_auto_managed;
		st->sequential_download = m_sequential_download;
#endif
		st->is_seeding = is_seed();
		st->is_finished = is_finished();
#if TORRENT_ABI_VERSION == 1
#ifndef TORRENT_DISABLE_SUPERSEEDING
		st->super_seeding = m_super_seeding;
#endif
#endif
		st->has_metadata = valid_metadata();
		bytes_done(*st, flags);
		TORRENT_ASSERT(st->total_wanted_done >= 0);
		TORRENT_ASSERT(st->total_done >= st->total_wanted_done);

		// payload transfer
		st->total_payload_download = m_stat.total_payload_download();
		st->total_payload_upload = m_stat.total_payload_upload();

		// total transfer
		st->total_download = m_stat.total_payload_download()
			+ m_stat.total_protocol_download();
		st->total_upload = m_stat.total_payload_upload()
			+ m_stat.total_protocol_upload();

		// failed bytes
		st->total_failed_bytes = m_total_failed_bytes;
		st->total_redundant_bytes = m_total_redundant_bytes;

		// transfer rate
		st->download_rate = m_stat.download_rate();
		st->upload_rate = m_stat.upload_rate();
		st->download_payload_rate = m_stat.download_payload_rate();
		st->upload_payload_rate = m_stat.upload_payload_rate();

		if (is_paused() || m_tracker_timer.expires_at() < now)
			st->next_announce = seconds(0);
		else
			st->next_announce = m_tracker_timer.expires_at() - now;

		if (st->next_announce.count() < 0)
			st->next_announce = seconds(0);

#if TORRENT_ABI_VERSION == 1
		st->announce_interval = seconds(0);
#endif

		st->current_tracker.clear();
		if (m_last_working_tracker >= 0)
		{
			TORRENT_ASSERT(m_last_working_tracker < m_trackers.end_index());
			const int i = m_last_working_tracker;
			st->current_tracker = m_trackers[i].url;
		}
		else
		{
			for (auto const& t : m_trackers)
			{
				if (std::any_of(t.endpoints.begin(), t.endpoints.end()
					, [](announce_endpoint const& aep) { return aep.updating; })) continue;
				if (!t.verified) continue;
				st->current_tracker = t.url;
				break;
			}
		}

		if ((flags & torrent_handle::query_verified_pieces))
		{
			st->verified_pieces = m_verified;
		}

		st->num_uploads = m_num_uploads;
		st->uploads_limit = m_max_uploads == (1 << 24) - 1 ? -1 : m_max_uploads;
		st->num_connections = num_peers();
		st->connections_limit = m_max_connections == (1 << 24) - 1 ? -1 : m_max_connections;
		// if we don't have any metadata, stop here

		st->queue_position = queue_position();
		st->need_save_resume = need_save_resume_data();
#if TORRENT_ABI_VERSION == 1
		st->ip_filter_applies = m_apply_ip_filter;
#endif

		st->state = static_cast<torrent_status::state_t>(m_state);
		st->flags = this->flags();

#if TORRENT_USE_ASSERTS
		if (st->state == torrent_status::finished
			|| st->state == torrent_status::seeding)
		{
			// it may be tempting to assume that st->is_finished == true here, but
			// this assumption does not always hold. We transition to "finished"
			// when we receive the last block of the last piece, which is before
			// the hash check comes back. "is_finished" is set to true once all the
			// pieces have been hash checked. So, there's a short window where it
			// doesn't hold.
		}
#endif

		if (!valid_metadata())
		{
			st->state = torrent_status::downloading_metadata;
			st->progress_ppm = m_progress_ppm;
#if !TORRENT_NO_FPU
			st->progress = m_progress_ppm / 1000000.f;
#endif
			st->block_size = 0;
			return;
		}

		st->block_size = block_size();

		if (m_state == torrent_status::checking_files)
		{
			st->progress_ppm = m_progress_ppm;
#if !TORRENT_NO_FPU
			st->progress = m_progress_ppm / 1000000.f;
#endif
		}
		else if (st->total_wanted == 0)
		{
			st->progress_ppm = 1000000;
			st->progress = 1.f;
		}
		else
		{
			st->progress_ppm = int(st->total_wanted_done * 1000000
				/ st->total_wanted);
#if !TORRENT_NO_FPU
			st->progress = st->progress_ppm / 1000000.f;
#endif
		}

		if (flags & torrent_handle::query_pieces)
		{
			int const num_pieces = m_torrent_file->num_pieces();
			if (has_picker())
			{
				st->pieces.resize(num_pieces, false);
				for (auto const i : st->pieces.range())
					if (m_picker->has_piece_passed(i)) st->pieces.set_bit(i);
			}
			else if (m_have_all)
			{
				st->pieces.resize(num_pieces, true);
			}
			else
			{
				st->pieces.resize(num_pieces, false);
			}
		}
		st->num_pieces = num_have();
		st->num_seeds = num_seeds();
		if ((flags & torrent_handle::query_distributed_copies) && m_picker.get())
		{
			std::tie(st->distributed_full_copies, st->distributed_fraction) =
				m_picker->distributed_copies();
#if TORRENT_NO_FPU
			st->distributed_copies = -1.f;
#else
			st->distributed_copies = st->distributed_full_copies
				+ float(st->distributed_fraction) / 1000;
#endif
		}
		else
		{
			st->distributed_full_copies = -1;
			st->distributed_fraction = -1;
			st->distributed_copies = -1.f;
		}

		st->last_seen_complete = m_swarm_last_seen_complete;
	}

	int torrent::priority() const
	{
		int priority = 0;
		for (int i = 0; i < num_classes(); ++i)
		{
			int const* prio = m_ses.peer_classes().at(class_at(i))->priority;
			priority = std::max(priority, prio[peer_connection::upload_channel]);
			priority = std::max(priority, prio[peer_connection::download_channel]);
		}
		return priority;
	}

#if TORRENT_ABI_VERSION == 1
	void torrent::set_priority(int const prio)
	{
		// priority 1 is default
		if (prio == 1 && m_peer_class == peer_class_t{}) return;

		if (m_peer_class == peer_class_t{})
			setup_peer_class();

		struct peer_class* tpc = m_ses.peer_classes().at(m_peer_class);
		TORRENT_ASSERT(tpc);
		tpc->priority[peer_connection::download_channel] = prio;
		tpc->priority[peer_connection::upload_channel] = prio;

		state_updated();
	}
#endif

	void torrent::add_redundant_bytes(int const b, waste_reason const reason)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(b > 0);
		TORRENT_ASSERT(static_cast<int>(reason) >= 0);
		TORRENT_ASSERT(static_cast<int>(reason) < static_cast<int>(waste_reason::max));

		if (m_total_redundant_bytes <= std::numeric_limits<std::int32_t>::max() - b)
			m_total_redundant_bytes += b;
		else
			m_total_redundant_bytes = std::numeric_limits<std::int32_t>::max();

		// the stats counters are 64 bits, so we don't check for overflow there
		m_stats_counters.inc_stats_counter(counters::recv_redundant_bytes, b);
		m_stats_counters.inc_stats_counter(counters::waste_piece_timed_out + static_cast<int>(reason), b);
	}

	void torrent::add_failed_bytes(int const b)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(b > 0);
		if (m_total_failed_bytes <= std::numeric_limits<std::int32_t>::max() - b)
			m_total_failed_bytes += b;
		else
			m_total_failed_bytes = std::numeric_limits<std::int32_t>::max();

		// the stats counters are 64 bits, so we don't check for overflow there
		m_stats_counters.inc_stats_counter(counters::recv_failed_bytes, b);
	}

	// the number of connected peers that are seeds
	int torrent::num_seeds() const
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		return int(m_num_seeds) - int(m_num_connecting_seeds);
	}

	// the number of connected peers that are not seeds
	int torrent::num_downloaders() const
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		int const ret = num_peers()
			- m_num_seeds
			- m_num_connecting
			+ m_num_connecting_seeds;
		TORRENT_ASSERT(ret >= 0);
		return ret;
	}

	void torrent::tracker_request_error(tracker_request const& r
		, error_code const& ec, std::string const& msg
		, seconds32 const retry_interval)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			debug_log("*** tracker error: (%d) %s %s", ec.value()
				, ec.message().c_str(), msg.c_str());
		}
#endif
		if (0 == (r.kind & tracker_request::scrape_request))
		{
			// announce request
			announce_entry* ae = find_tracker(r.url);
			int fails = 0;
			tcp::endpoint local_endpoint;
			if (ae)
			{
				auto aep = std::find_if(ae->endpoints.begin(), ae->endpoints.end()
					, [&](announce_endpoint const& e) { return e.socket == r.outgoing_socket; });

				if (aep != ae->endpoints.end())
				{
					local_endpoint = aep->local_endpoint;
					aep->failed(settings().get_int(settings_pack::tracker_backoff)
						, retry_interval);
					aep->last_error = ec;
					aep->message = msg;
					fails = aep->fails;
#ifndef TORRENT_DISABLE_LOGGING
					debug_log("*** increment tracker fail count [ep: %s url: %s %d]"
						, print_endpoint(aep->local_endpoint).c_str(), r.url.c_str(), aep->fails);
#endif
					// don't try to announce from this endpoint again
					if (ec == boost::system::errc::address_family_not_supported
						|| ec == boost::system::errc::host_unreachable)
					{
						aep->enabled = false;
#ifndef TORRENT_DISABLE_LOGGING
						debug_log("*** disabling endpoint [ep: %s url: %s ]"
							, print_endpoint(aep->local_endpoint).c_str(), r.url.c_str());
#endif
					}
				}
				else if (r.outgoing_socket)
				{
#ifndef TORRENT_DISABLE_LOGGING
					debug_log("*** no matching endpoint for request [%s, %s]"
						, r.url.c_str(), print_endpoint(r.outgoing_socket.get_local_endpoint()).c_str());
#endif
				}

				int const tracker_index = int(ae - m_trackers.data());

				// never talk to this tracker again
				if (ec == error_code(410, http_category())) ae->fail_limit = 1;

				// if all endpoints fail, then we de-prioritize the tracker and try
				// the next one in the tier
				if (std::all_of(ae->endpoints.begin(), ae->endpoints.end()
					, [](announce_endpoint const& ep) { return ep.fails > 0; }))
				{
					deprioritize_tracker(tracker_index);
				}
			}
			if (m_ses.alerts().should_post<tracker_error_alert>()
				|| r.triggered_manually)
			{
				m_ses.alerts().emplace_alert<tracker_error_alert>(get_handle()
					, local_endpoint, fails, r.url, ec, msg);
			}
		}
		else
		{
			announce_entry* ae = find_tracker(r.url);

			// scrape request
			if (ec == error_code(410, http_category()))
			{
				// never talk to this tracker again
				if (ae != nullptr) ae->fail_limit = 1;
			}

			// if this was triggered manually we need to post this unconditionally,
			// since the client expects a response from its action, regardless of
			// whether all tracker events have been enabled by the alert mask
			if (m_ses.alerts().should_post<scrape_failed_alert>()
				|| r.triggered_manually)
			{
				tcp::endpoint local_endpoint;
				if (ae != nullptr)
				{
					auto aep = ae->find_endpoint(r.outgoing_socket);
					if (aep != nullptr) local_endpoint = aep->local_endpoint;
				}

				m_ses.alerts().emplace_alert<scrape_failed_alert>(get_handle(), local_endpoint, r.url, ec);
			}
		}
		// announce to the next working tracker
		if ((!m_abort && !is_paused()) || r.event == tracker_request::stopped)
			announce_with_tracker(r.event);
		update_tracker_timer(aux::time_now32());
	}

#ifndef TORRENT_DISABLE_LOGGING
	bool torrent::should_log() const
	{
		return alerts().should_post<torrent_log_alert>();
	}

	TORRENT_FORMAT(2,3)
	void torrent::debug_log(char const* fmt, ...) const noexcept try
	{
		if (!alerts().should_post<torrent_log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		alerts().emplace_alert<torrent_log_alert>(
			const_cast<torrent*>(this)->get_handle(), fmt, v);
		va_end(v);
	}
	catch (std::exception const&) {}
#endif

}
