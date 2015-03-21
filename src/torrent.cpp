/*

Copyright (c) 2003-2014, Arvid Norberg
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

#include <ctime>
#include <algorithm>
#include <set>
#include <cctype>
#include <numeric>
#include <limits> // for numeric_limits

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
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
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/gzip.hpp" // for inflate_gzip
#include "libtorrent/random.hpp"
#include "libtorrent/peer_class.hpp" // for peer_class
#include "libtorrent/string_util.hpp" // for allocate_string_copy
#include "libtorrent/socket_io.hpp" // for read_*_endpoint
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/request_blocks.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_manager.hpp" // for alert_manageralert_manager
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/resolve_links.hpp"

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/ssl_stream.hpp"
#include <boost/asio/ssl/context.hpp>
#include <openssl/rand.h>
#if BOOST_VERSION >= 104700
#include <boost/asio/ssl/verify_context.hpp>
#endif // BOOST_VERSION
#endif // TORRENT_USE_OPENSSL

#if defined TORRENT_LOGGING
#include "libtorrent/aux_/session_impl.hpp" // for tracker_logger
#endif

using namespace libtorrent;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;

namespace libtorrent
{
	int root2(int x)
	{
		int ret = 0;
		x >>= 1;
		while (x > 0)
		{
			// if this assert triggers, the block size
			// is not an even 2 exponent!
			TORRENT_ASSERT(x == 1 || (x & 1) == 0);
			++ret;
			x >>= 1;
		}
		return ret;
	}

	web_seed_t::web_seed_t(web_seed_entry const& wse)
		: web_seed_entry(wse)
		, retry(aux::time_now())
		, peer_info(tcp::endpoint(), true, 0)
		, supports_keepalive(true)
		, resolving(false)
		, removed(false)
	{
		peer_info.web_seed = true;
		restart_request.piece = -1;
	}

	web_seed_t::web_seed_t(std::string const& url_, web_seed_entry::type_t type_
		, std::string const& auth_
		, web_seed_entry::headers_t const& extra_headers_)
		: web_seed_entry(url_, type_, auth_, extra_headers_)
		, retry(aux::time_now())
		, peer_info(tcp::endpoint(), true, 0)
		, supports_keepalive(true)
		, resolving(false)
		, removed(false)
	{
		peer_info.web_seed = true;
		restart_request.piece = -1;
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	// defined in ut_pex.cpp
	bool was_introduced_by(peer_plugin const*, tcp::endpoint const&);
#endif

	torrent_hot_members::torrent_hot_members(aux::session_interface& ses
		, add_torrent_params const& p, int block_size)
		: m_ses(ses)
		, m_complete(0xffffff)
		, m_upload_mode(p.flags & add_torrent_params::flag_upload_mode)
		, m_connections_initialized(false)
		, m_abort(false)
		, m_allow_peers((p.flags & add_torrent_params::flag_paused) == 0)
		, m_share_mode(p.flags & add_torrent_params::flag_share_mode)
		, m_have_all(false)
		, m_graceful_pause_mode(false)
		, m_state_subscription(p.flags & add_torrent_params::flag_update_subscribe)
		, m_max_connections(0xffffff)
		, m_block_size_shift(root2(block_size))
		, m_state(torrent_status::checking_resume_data)
	{}

	torrent::torrent(
		aux::session_interface& ses
		, int block_size
		, int seq
		, add_torrent_params const& p
		, sha1_hash const& info_hash)
		: torrent_hot_members(ses, p, block_size)
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_tracker_timer(ses.get_io_service())
		, m_inactivity_timer(ses.get_io_service())
		, m_trackerid(p.trackerid)
		, m_save_path(complete(p.save_path))
		, m_url(p.url)
		, m_uuid(p.uuid)
		, m_source_feed_url(p.source_feed_url)
		, m_stats_counters(ses.stats_counters())
		, m_storage_constructor(p.storage)
		, m_added_time(time(0))
		, m_completed_time(0)
		, m_last_seen_complete(0)
		, m_swarm_last_seen_complete(0)
		, m_info_hash(info_hash)
		, m_num_verified(0)
		, m_last_saved_resume(ses.session_time())
		, m_started(ses.session_time())
		, m_became_seed(0)
		, m_became_finished(0)
		, m_checking_piece(0)
		, m_num_checked_pieces(0)
		, m_refcount(0)
		, m_error_file(error_file_none)
		, m_average_piece_time(0)
		, m_piece_time_deviation(0)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_sequence_number(seq)
		, m_peer_class(0)
		, m_num_connecting(0)
		, m_upload_mode_time(0)
		, m_announce_to_trackers((p.flags & add_torrent_params::flag_paused) == 0)
		, m_announce_to_lsd((p.flags & add_torrent_params::flag_paused) == 0)
		, m_has_incoming(false)
		, m_files_checked(false)
		, m_storage_mode(p.storage_mode)
		, m_announcing(false)
		, m_waiting_tracker(false)
		, m_active_time(0)
		, m_last_working_tracker(-1)
		, m_finished_time(0)
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_seed_mode(false)
		, m_super_seeding(false)
		, m_override_resume_data(p.flags & add_torrent_params::flag_override_resume_data)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_need_save_resume_data(true)
		, m_seeding_time(0)
		, m_time_scaler(0)
		, m_max_uploads((1<<24)-1)
		, m_save_resume_flags(0)
		, m_num_uploads(0)
		, m_need_suggest_pieces_refresh(false)
		, m_need_connect_boost(true)
		, m_lsd_seq(0)
		, m_magnet_link(false)
		, m_apply_ip_filter(p.flags & add_torrent_params::flag_apply_ip_filter)
		, m_merge_resume_trackers(p.flags & add_torrent_params::flag_merge_resume_trackers)
		, m_padding(0)
		, m_priority(0)
		, m_incomplete(0xffffff)
		, m_announce_to_dht((p.flags & add_torrent_params::flag_paused) == 0)
		, m_in_state_updates(false)
		, m_is_active_download(false)
		, m_is_active_finished(false)
		, m_ssl_torrent(false)
		, m_deleted(false)
		, m_pinned(p.flags & add_torrent_params::flag_pinned)
		, m_should_be_loaded(true)
		, m_last_download((std::numeric_limits<boost::int16_t>::min)())
		, m_num_seeds(0)
		, m_last_upload((std::numeric_limits<boost::int16_t>::min)())
		, m_storage_tick(0)
		, m_auto_managed(p.flags & add_torrent_params::flag_auto_managed)
		, m_current_gauge_state(no_gauge_state)
		, m_moving_storage(false)
		, m_inactive(false)
		, m_auto_sequential(false)
		, m_downloaded(0xffffff)
		, m_last_scrape((std::numeric_limits<boost::int16_t>::min)())
		, m_progress_ppm(0)
		, m_use_resume_save_path(p.flags & add_torrent_params::flag_use_resume_save_path)
	{
		if (m_pinned)
			inc_stats_counter(counters::num_pinned_torrents);

		inc_stats_counter(counters::num_loaded_torrents);

		// if there is resume data already, we don't need to trigger the initial save
		// resume data
		if (!p.resume_data.empty() && (p.flags & add_torrent_params::flag_override_resume_data) == 0)
			m_need_save_resume_data = false;

#if TORRENT_USE_ASSERTS
		m_resume_data_loaded = false;
#endif
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
			m_torrent_file = (p.ti ? p.ti : boost::make_shared<torrent_info>(info_hash));

		// add web seeds from add_torrent_params
		for (std::vector<std::string>::const_iterator i = p.url_seeds.begin()
			, end(p.url_seeds.end()); i != end; ++i)
		{
			m_web_seeds.push_back(web_seed_t(*i, web_seed_entry::url_seed));
		}

		m_trackers = m_torrent_file->trackers();
		if (m_torrent_file->is_valid())
		{
			m_seed_mode = p.flags & add_torrent_params::flag_seed_mode;
			m_connections_initialized = true;
			m_block_size_shift = root2((std::min)(block_size, m_torrent_file->piece_length()));
		}
		else
		{
			if (!p.name.empty()) m_name.reset(new std::string(p.name));
		}

		if (!m_url.empty() && m_uuid.empty()) m_uuid = m_url;

		TORRENT_ASSERT(is_single_thread());
		m_file_priority = p.file_priorities;

		if (m_seed_mode)
		{
			m_verified.resize(m_torrent_file->num_pieces(), false);
			m_verifying.resize(m_torrent_file->num_pieces(), false);
		}

		if (!p.resume_data.empty())
		{
			m_resume_data.reset(new resume_data_t);
			m_resume_data->buf = p.resume_data;
		}

		update_want_peers();
		update_want_scrape();
		update_want_tick();

		INVARIANT_CHECK;

		if (p.flags & add_torrent_params::flag_sequential_download)
			m_sequential_download = true;

		if (p.flags & add_torrent_params::flag_super_seeding)
		{
			m_super_seeding = true;
			m_need_save_resume_data = true;
		}

		set_max_uploads(p.max_uploads, false);
		set_max_connections(p.max_connections, false);
		set_limit_impl(p.upload_limit, peer_connection::upload_channel, false);
		set_limit_impl(p.download_limit, peer_connection::download_channel, false);

		if (!m_name && !m_url.empty()) m_name.reset(new std::string(m_url));

#ifndef TORRENT_NO_DEPRECATE
		if (p.tracker_url && std::strlen(p.tracker_url) > 0)
		{
			m_trackers.push_back(announce_entry(p.tracker_url));
			m_trackers.back().fail_limit = 0;
			m_trackers.back().source = announce_entry::source_magnet_link;
			m_torrent_file->add_tracker(p.tracker_url);
		}
#endif

		for (std::vector<std::string>::const_iterator i = p.trackers.begin()
			, end(p.trackers.end()); i != end; ++i)
		{
			m_trackers.push_back(announce_entry(*i));
			m_trackers.back().fail_limit = 0;
			m_trackers.back().source = announce_entry::source_magnet_link;
			m_torrent_file->add_tracker(*i);
		}

		if (settings().get_bool(settings_pack::prefer_udp_trackers))
			prioritize_udp_trackers();

		// if we don't have metadata, make this torrent pinned. The
		// client may unpin it once we have metadata and it has had
		// a chance to save it on the metadata_received_alert
		if (!valid_metadata())
		{
			if (!m_pinned && m_refcount == 0)
				inc_stats_counter(counters::num_pinned_torrents);

			m_pinned = true;
		}
		else
		{
			inc_stats_counter(counters::num_total_pieces_added
				, m_torrent_file->num_pieces());
		}

		update_gauge();
	}

	void torrent::inc_stats_counter(int c, int value)
	{ m_ses.stats_counters().inc_stats_counter(c, value); }

#if 0
	
	// NON BOTTLED VERSION. SUPPORTS PROGRESS REPORTING

	// since this download is not bottled, this callback will
	// be called every time we receive another piece of the
	// .torrent file
	void torrent::on_torrent_download(error_code const& ec
		, http_parser const& parser
		, char const* data, int size)
	{
		if (m_abort) return;

		if (ec && ec != asio::error::eof)
		{
			set_error(ec, error_file_url);
			pause();
			return;
		}

		if (size > 0)
		{
			m_torrent_file_buf.insert(m_torrent_file_buf.end(), data, data + size);
			if (parser.content_length() > 0)
				set_progress_ppm(boost::int64_t(m_torrent_file_buf.size())
					* 1000000 / parser.content_length());
		}

		if (parser.header_finished() && parser.status_code() != 200)
		{
			set_error(error_code(parser.status_code(), get_http_category()), error_file_url);
			pause();
			return;
		}

		if (!ec) return;

		// if this was received with chunked encoding, we need to strip out
		// the chunk headers
		size = parser.collapse_chunk_headers((char*)&m_torrent_file_buf[0], m_torrent_file_buf.size());
		m_torrent_file_buf.resize(size);

		std::string const& encoding = parser.header("content-encoding");
		if ((encoding == "gzip" || encoding == "x-gzip") && m_torrent_file_buf.size())
		{
			std::vector<char> buf;
			error_code ec;
			inflate_gzip(&m_torrent_file_buf[0], m_torrent_file_buf.size()
				, buf, 4 * 1024 * 1024, ex);
			if (ec)
			{
				set_error(ec, error_file_url);
				pause();
				std::vector<char>().swap(m_torrent_file_buf);
				return;
			}
			m_torrent_file_buf.swap(buf);
		}

		// we're done!
		error_code e;
		boost::shared_ptr<torrent_info> tf(boost::make_shared<torrent_info>(
			&m_torrent_file_buf[0], m_torrent_file_buf.size(), e));
		if (e)
		{
			set_error(e, error_file_url);
			pause();
			std::vector<char>().swap(m_torrent_file_buf);
			return;
		}
		std::vector<char>().swap(m_torrent_file_buf);
		
		// update our torrent_info object and move the
		// torrent from the old info-hash to the new one
		// as we replace the torrent_info object
#if TORRENT_USE_ASSERTS
		int num_torrents = m_ses.m_torrents.size();
#endif
		// we're about to erase the session's reference to this
		// torrent, create another reference
		boost::shared_ptr<torrent> me(shared_from_this());

		m_ses.remove_torrent_impl(me, 0);

		m_torrent_file = tf;

		// now, we might already have this torrent in the session.
		boost::shared_ptr<torrent> t = m_ses.find_torrent(m_torrent_file->info_hash()).lock();
		if (t)
		{
			if (!m_uuid.empty() && t->uuid().empty())
				t->set_uuid(m_uuid);
			if (!m_url.empty() && t->url().empty())
				t->set_url(m_url);
			if (!m_source_feed_url.empty() && t->source_feed_url().empty())
				t->set_source_feed_url(m_source_feed_url);

			// insert this torrent in the uuid index
			if (!m_uuid.empty() || !m_url.empty())
			{
				m_ses.insert_uuid_torrent(m_uuid.empty() ? m_url : m_uuid, t);
			}

			// TODO: if the existing torrent doesn't have metadata, insert
			// the metadata we just downloaded into it.

			set_error(error_code(errors::duplicate_torrent, get_libtorrent_category()), error_file_url);
			abort();
			return;
		}

		m_ses.insert_torrent(m_torrent_file->info_hash(), me, m_uuid);

		TORRENT_ASSERT(num_torrents == int(m_ses.m_torrents.size()));

		// if the user added any trackers while downloading the
		// .torrent file, serge them into the new tracker list
		std::vector<announce_entry> new_trackers = m_torrent_file->trackers();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			// if we already have this tracker, ignore it
			if (std::find_if(new_trackers.begin(), new_trackers.end()
				, boost::bind(&announce_entry::url, _1) == i->url) != new_trackers.end())
				continue;

			// insert the tracker ordered by tier
			new_trackers.insert(std::find_if(new_trackers.begin(), new_trackers.end()
				, boost::bind(&announce_entry::tier, _1) >= i->tier), *i);
		}
		m_trackers.swap(new_trackers);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		hasher h;
		h.update("req2", 4);
		h.update((char*)&m_torrent_file->info_hash()[0], 20);
		// this is SHA1("req2" + info-hash), used for
		// encrypted hand shakes
		m_ses.add_obfuscated_hash(h.final(), shared_from_this());
#endif

		if (m_ses.alerts().should_post<metadata_received_alert>())
		{
			m_ses.alerts().post_alert(metadata_received_alert(
				get_handle()));
		}

		state_updated();

		set_state(torrent_status::downloading);

		m_override_resume_data = true;
		init();
	}
#else // if 0

	int torrent::current_stats_state() const
	{
		if (m_abort) return counters::num_checking_torrents + no_gauge_state;

		if (has_error()) return counters::num_error_torrents;
		if (!m_allow_peers || m_graceful_pause_mode)
		{
			if (!is_auto_managed()) return counters::num_stopped_torrents;
			if (is_seed()) return counters::num_queued_seeding_torrents;
			return counters::num_queued_download_torrents;
		}
		if (state() == torrent_status::checking_files
#ifndef TORRENT_NO_DEPRECATE
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
		int new_gauge_state = current_stats_state() - counters::num_checking_torrents;
		TORRENT_ASSERT(new_gauge_state >= 0);
		TORRENT_ASSERT(new_gauge_state <= no_gauge_state);

		if (new_gauge_state == m_current_gauge_state) return;

		if (m_current_gauge_state != no_gauge_state)
			inc_stats_counter(m_current_gauge_state + counters::num_checking_torrents, -1);
		if (new_gauge_state != no_gauge_state)
			inc_stats_counter(new_gauge_state + counters::num_checking_torrents, 1);

		m_current_gauge_state = new_gauge_state;
	}

	void torrent::on_torrent_download(error_code const& ec
		, http_parser const& parser, char const* data, int size)
	{
		if (m_abort) return;

		if (ec && ec != asio::error::eof)
		{
			set_error(ec, error_file_url);
			pause();
			return;
		}

		if (parser.status_code() != 200)
		{
			set_error(error_code(parser.status_code(), get_http_category()), error_file_url);
			pause();
			return;
		}

		error_code e;
		boost::shared_ptr<torrent_info> tf(boost::make_shared<torrent_info>(data, size, boost::ref(e), 0));
		if (e)
		{
			set_error(e, error_file_url);
			pause();
			return;
		}
		
		// update our torrent_info object and move the
		// torrent from the old info-hash to the new one
		// as we replace the torrent_info object
		// we're about to erase the session's reference to this
		// torrent, create another reference
		boost::shared_ptr<torrent> me(shared_from_this());

		m_ses.remove_torrent_impl(me, 0);

		if (alerts().should_post<torrent_update_alert>())
			alerts().post_alert(torrent_update_alert(get_handle(), info_hash(), tf->info_hash()));

		m_torrent_file = tf;
		m_info_hash = tf->info_hash();

		// now, we might already have this torrent in the session.
		boost::shared_ptr<torrent> t = m_ses.find_torrent(m_torrent_file->info_hash()).lock();
		if (t)
		{
			if (!m_uuid.empty() && t->uuid().empty())
				t->set_uuid(m_uuid);
			if (!m_url.empty() && t->url().empty())
				t->set_url(m_url);
			if (!m_source_feed_url.empty() && t->source_feed_url().empty())
				t->set_source_feed_url(m_source_feed_url);

			// insert this torrent in the uuid index
			if (!m_uuid.empty() || !m_url.empty())
			{
				m_ses.insert_uuid_torrent(m_uuid.empty() ? m_url : m_uuid, t);
			}

			// TODO: if the existing torrent doesn't have metadata, insert
			// the metadata we just downloaded into it.

			set_error(error_code(errors::duplicate_torrent, get_libtorrent_category()), error_file_url);
			abort();
			return;
		}

		m_ses.insert_torrent(m_torrent_file->info_hash(), me, m_uuid);

		// if the user added any trackers while downloading the
		// .torrent file, merge them into the new tracker list
		std::vector<announce_entry> new_trackers = m_torrent_file->trackers();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			// if we already have this tracker, ignore it
			if (std::find_if(new_trackers.begin(), new_trackers.end()
				, boost::bind(&announce_entry::url, _1) == i->url) != new_trackers.end())
				continue;

			// insert the tracker ordered by tier
			new_trackers.insert(std::find_if(new_trackers.begin(), new_trackers.end()
				, boost::bind(&announce_entry::tier, _1) >= i->tier), *i);
		}
		m_trackers.swap(new_trackers);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		hasher h;
		h.update("req2", 4);
		h.update((char*)&m_torrent_file->info_hash()[0], 20);
		m_ses.add_obfuscated_hash(h.final(), shared_from_this());
#endif

		if (m_ses.alerts().should_post<metadata_received_alert>())
		{
			m_ses.alerts().post_alert(metadata_received_alert(
				get_handle()));
		}

		state_updated();

		set_state(torrent_status::downloading);

		m_override_resume_data = true;
		init();
	}

#endif // if 0

	void torrent::leave_seed_mode(bool seed)
	{
		if (!m_seed_mode) return;

		if (!seed)
		{
			// this means the user promised we had all the
			// files, but it turned out we didn't. This is
			// an error.

			// TODO: 2 post alert
		
#if defined TORRENT_LOGGING
			debug_log("*** FAILED SEED MODE, rechecking");
#endif
		}

#if defined TORRENT_LOGGING
		debug_log("*** LEAVING SEED MODE (%s)", seed ? "as seed" : "as non-seed");
#endif
		m_seed_mode = false;
		// seed is false if we turned out not
		// to be a seed after all
		if (!seed)
		{
			m_have_all = false;
			set_state(torrent_status::downloading);
			force_recheck();
		}
		m_num_verified = 0;
		m_verified.clear();
		m_verifying.clear();

		m_need_save_resume_data = true;
	}

	void torrent::verified(int piece)
	{
		TORRENT_ASSERT(piece < int(m_verified.size()));
		TORRENT_ASSERT(piece >= 0);
		TORRENT_ASSERT(m_verified.get_bit(piece) == false);
		++m_num_verified;
		m_verified.set_bit(piece);
	}

	void torrent::start()
	{
		TORRENT_ASSERT(is_single_thread());
#if defined TORRENT_LOGGING
		debug_log("starting torrent");
#endif
		std::vector<boost::uint64_t>().swap(m_file_progress);

		if (m_resume_data)
		{
			int pos;
			error_code ec;
			if (bdecode(&m_resume_data->buf[0], &m_resume_data->buf[0]
					+ m_resume_data->buf.size(), m_resume_data->node, ec, &pos) != 0)
			{
				m_resume_data.reset();
#if defined TORRENT_LOGGING
				debug_log("resume data rejected: %s pos: %d", ec.message().c_str(), pos);
#endif
				if (m_ses.alerts().should_post<fastresume_rejected_alert>())
					m_ses.alerts().post_alert(fastresume_rejected_alert(get_handle(), ec, "", 0));
			}
		}

		if (!m_torrent_file->is_valid() && !m_url.empty())
		{
			// we need to download the .torrent file from m_url
			start_download_url();
		}
		else if (m_torrent_file->is_valid())
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
	}

	void torrent::start_download_url()
	{
		TORRENT_ASSERT(!m_url.empty());
		TORRENT_ASSERT(!m_torrent_file->is_valid());
		boost::shared_ptr<http_connection> conn(
			new http_connection(m_ses.get_io_service()
				, m_ses.get_resolver()
				, boost::bind(&torrent::on_torrent_download, shared_from_this()
					, _1, _2, _3, _4)
				, true // bottled
				//bottled buffer size
				, m_ses.settings().get_int(settings_pack::max_http_recv_buffer_size)
				, http_connect_handler()
				, http_filter_handler()
#ifdef TORRENT_USE_OPENSSL
				, m_ssl_ctx.get()
#endif
				));
		proxy_settings ps = m_ses.proxy();
		conn->get(m_url, seconds(30), 0, &ps
			, 5, m_ses.settings().get_str(settings_pack::user_agent));
		set_state(torrent_status::downloading_metadata);
	}

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

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ses.announce_dht()) return false;

		if (!m_ses.dht()) return false;
		if (m_torrent_file->is_valid() && !m_files_checked) return false;
		if (!m_announce_to_dht) return false;
		if (!m_allow_peers) return false;

		// if we don't have the metadata, and we're waiting
		// for a web server to serve it to us, no need to announce
		// because the info-hash is just the URL hash
		if (!m_torrent_file->is_valid() && !m_url.empty()) return false;

		// don't announce private torrents
		if (m_torrent_file->is_valid() && m_torrent_file->priv()) return false;
		if (m_trackers.empty()) return true;
		if (!settings().get_bool(settings_pack::use_dht_as_fallback)) return true;

		int verified_trackers = 0;
		for (std::vector<announce_entry>::const_iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
			if (i->verified) ++verified_trackers;
			
		return verified_trackers == 0;
	}

#endif

	torrent::~torrent()
	{
		TORRENT_ASSERT(m_abort);
		TORRENT_ASSERT(prev == NULL && next == NULL);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		for (int i = 0; i < aux::session_interface::num_torrent_lists; ++i)
		{
			if (!m_links[i].in_list()) continue;
			m_links[i].unlink(m_ses.torrent_list(i), i);
		}
#endif

		TORRENT_ASSERT(m_refcount == 0);

		if (m_pinned)
			inc_stats_counter(counters::num_pinned_torrents, -1);

		if (is_loaded())
			inc_stats_counter(counters::num_loaded_torrents, -1);

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

		TORRENT_ASSERT(m_abort);
		TORRENT_ASSERT(m_connections.empty());
		if (!m_connections.empty())
			disconnect_all(errors::torrent_aborted, op_bittorrent);
	}

	void torrent::read_piece(int piece)
	{
		if (m_abort || m_deleted)
		{
			// failed
			m_ses.alerts().post_alert(read_piece_alert(
				get_handle(), piece, error_code(boost::system::errc::operation_canceled, system_category())));
			return;
		}

		TORRENT_ASSERT(piece >= 0 && piece < m_torrent_file->num_pieces());
		int piece_size = m_torrent_file->piece_size(piece);
		int blocks_in_piece = (piece_size + block_size() - 1) / block_size();

		// if blocks_in_piece is 0, rp will leak
		TORRENT_ASSERT(blocks_in_piece > 0);
		TORRENT_ASSERT(piece_size > 0);

		read_piece_struct* rp = new read_piece_struct;
		rp->piece_data.reset(new (std::nothrow) char[piece_size]);
		rp->blocks_left = 0;
		rp->fail = false;

		peer_request r;
		r.piece = piece;
		r.start = 0;
		rp->blocks_left = blocks_in_piece;
		if (!need_loaded())
		{
			rp->piece_data.reset();
			m_ses.alerts().post_alert(read_piece_alert(
				get_handle(), r.piece, rp->piece_data, 0));
			delete rp;
			return;
		}
		for (int i = 0; i < blocks_in_piece; ++i, r.start += block_size())
		{
			r.length = (std::min)(piece_size - r.start, block_size());
			inc_refcount("read_piece");
			m_ses.disk_thread().async_read(&storage(), r, boost::bind(&torrent::on_disk_read_complete
				, shared_from_this(), _1, r, rp), (void*)1);
		}
	}

	void torrent::send_share_mode()
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			if ((*i)->type() != peer_connection::bittorrent_connection) continue;
			bt_peer_connection* p = (bt_peer_connection*)*i;
			p->write_share_mode();
		}
#endif
	}

	void torrent::send_upload_only()
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (share_mode()) return;
		if (super_seeding()) return;

		int idx = 0;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++idx)
		{
			// since the call to disconnect_if_redundant() may
			// delete the entry from this container, make sure
			// to increment the iterator early
			bt_peer_connection* p = (bt_peer_connection*)*i;
			if (p->type() == peer_connection::bittorrent_connection)
			{
				boost::shared_ptr<peer_connection> me(p->self());
				if (!p->is_disconnecting())
				{
					p->send_not_interested();
					p->write_upload_only();
				}
			}


			if (p->is_disconnecting())
			{
				i = m_connections.begin() + idx;
				--idx;
			}
			else
			{
				++i;
			}
		}
#endif
	}

	void torrent::set_share_mode(bool s)
	{
		if (s == m_share_mode) return;

		m_share_mode = s;

		// in share mode, all pieces have their priorities initialized to 0
		if (m_share_mode && valid_metadata())
		{
			m_file_priority.clear();
			m_file_priority.resize(m_torrent_file->num_files(), 0);
		}

		update_piece_priorities();

		if (m_share_mode) recalc_share_mode();
	}

	void torrent::set_upload_mode(bool b)
	{
		if (b == m_upload_mode) return;

		m_upload_mode = b;

		update_gauge();
		state_updated();
		send_upload_only();

		if (m_upload_mode)
		{
			// clear request queues of all peers
			for (peer_iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = (*i);
				p->cancel_all_requests();
			}
			// this is used to try leaving upload only mode periodically
			m_upload_mode_time = m_ses.session_time();
		}
		else if (m_peer_list)
		{
			// reset last_connected, to force fast reconnect after leaving upload mode
			for (peer_list::iterator i = m_peer_list->begin_peer()
				, end(m_peer_list->end_peer()); i != end; ++i)
			{
				(*i)->last_connected = 0;
			}

			// send_block_requests on all peers
			for (peer_iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = (*i);
				p->send_block_requests();
			}
		}
	}

	void torrent::need_policy()
	{
		if (m_peer_list) return;
		m_peer_list.reset(new peer_list);
	}

	void torrent::handle_disk_error(disk_io_job const* j, peer_connection* c)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!j->error) return;

#if defined TORRENT_LOGGING
		debug_log("disk error: (%d) %s in file: %s", j->error.ec.value(), j->error.ec.message().c_str()
			, resolve_filename(j->error.file).c_str());
#endif

		TORRENT_ASSERT(j->piece >= 0);

		if (j->action == disk_io_job::write)
		{
			piece_block block_finished(j->piece, j->d.io.offset / block_size());

			// we failed to write j->piece to disk tell the piece picker
			if (j->piece >= 0)
			{
				// this will block any other peer from issuing requests
				// to this piece, until we've cleared it.
				if (j->error.ec == asio::error::operation_aborted)
				{
					if (has_picker())
						picker().mark_as_canceled(block_finished, NULL);
				}
				else
				{
					// if any other peer has a busy request to this block, we need
					// to cancel it too
					cancel_block(block_finished);
					if (has_picker())
						picker().write_failed(block_finished);

					if (m_storage)
					{
						// when this returns, all outstanding jobs to the
						// piece are done, and we can restore it, allowing
						// new requests to it
						m_ses.disk_thread().async_clear_piece(m_storage.get(), j->piece
							, boost::bind(&torrent::on_piece_fail_sync, shared_from_this(), _1, block_finished));
					}
					else
					{
						// is m_abort true? if so, we should probably just
						// exit this function early, no need to keep the picker
						// state up-to-date, right?
						disk_io_job sj;
						sj.piece = j->piece;
						on_piece_fail_sync(&sj, block_finished);
					}
				}
				update_gauge();
			}
		}

		if (j->error.ec == error_code(boost::system::errc::not_enough_memory, generic_category()))
		{
			if (alerts().should_post<file_error_alert>())
				alerts().post_alert(file_error_alert(j->error.ec
					, resolve_filename(j->error.file), j->error.operation_str(), get_handle()));
			if (c) c->disconnect(errors::no_memory, op_file);
			return;
		}

		if (j->error.ec == asio::error::operation_aborted) return;

		// notify the user of the error
		if (alerts().should_post<file_error_alert>())
			alerts().post_alert(file_error_alert(j->error.ec
				, resolve_filename(j->error.file), j->error.operation_str(), get_handle()));

		// put the torrent in an error-state
		set_error(j->error.ec, j->error.file);

		// if a write operation failed, and future writes are likely to
		// fail, while reads may succeed, just set the torrent to upload mode
		// if we make an incorrect assumption here, it's not the end of the
		// world, if we ever issue a read request and it fails as well, we
		// won't get in here and we'll actually end up pausing the torrent
		if (j->action == disk_io_job::write
			&& (j->error.ec == boost::system::errc::read_only_file_system
			|| j->error.ec == boost::system::errc::permission_denied
			|| j->error.ec == boost::system::errc::operation_not_permitted
			|| j->error.ec == boost::system::errc::no_space_on_device
			|| j->error.ec == boost::system::errc::file_too_large))
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

		// if the error appears to be more serious than a full disk, just pause the torrent
		pause();
	}

	void torrent::on_piece_fail_sync(disk_io_job const* j, piece_block b)
	{
		update_gauge();
		// some peers that previously was no longer interesting may
		// now have become interesting, since we lack this one piece now.
		for (peer_iterator i = begin(); i != end();)
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

	void torrent::on_disk_read_complete(disk_io_job const* j, peer_request r, read_piece_struct* rp)
	{
		// hold a reference until this function returns
		torrent_ref_holder h(this, "read_piece");

		dec_refcount("read_piece");
		TORRENT_ASSERT(is_single_thread());

		disk_buffer_holder buffer(m_ses, *j);

		--rp->blocks_left;
		if (j->ret != r.length)
		{
			rp->fail = true;
			rp->error = j->error.ec;
			handle_disk_error(j);
		}
		else
		{
			std::memcpy(rp->piece_data.get() + r.start, j->buffer, r.length);
		}

		if (rp->blocks_left == 0)
		{
			int size = m_torrent_file->piece_size(r.piece);
			if (rp->fail)
			{
				m_ses.alerts().post_alert(read_piece_alert(
					get_handle(), r.piece, rp->error));
			}
			else
			{
				m_ses.alerts().post_alert(read_piece_alert(
					get_handle(), r.piece, rp->piece_data, size));
			}
			delete rp;
		}
	}

	void torrent::need_picker()
	{
		if (m_picker) return;

		INVARIANT_CHECK;

		// if we have all pieces we should not have a picker
		TORRENT_ASSERT(!m_have_all);

		m_picker.reset(new piece_picker());
		int blocks_per_piece = (m_torrent_file->piece_length() + block_size() - 1) / block_size();
		int blocks_in_last_piece = ((m_torrent_file->total_size() % m_torrent_file->piece_length())
			+ block_size() - 1) / block_size();
		m_picker->init(blocks_per_piece, blocks_in_last_piece, m_torrent_file->num_pieces());

		update_gauge();

		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			peer_has((*i)->get_bitfield(), *i);
		}
	}

	void torrent::add_piece(int piece, char const* data, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(piece >= 0 && piece < m_torrent_file->num_pieces());
		int piece_size = m_torrent_file->piece_size(piece);
		int blocks_in_piece = (piece_size + block_size() - 1) / block_size();

		if (m_deleted) return;

		// avoid crash trying to access the picker when there is none
		if (m_have_all && !has_picker()) return;

		need_picker();

		if (picker().have_piece(piece)
			&& (flags & torrent::overwrite_existing) == 0)
			return;

		peer_request p;
		p.piece = piece;
		p.start = 0;
		picker().inc_refcount(piece, 0);
		for (int i = 0; i < blocks_in_piece; ++i, p.start += block_size())
		{
			if (picker().is_finished(piece_block(piece, i))
				&& (flags & torrent::overwrite_existing) == 0)
				continue;

			p.length = (std::min)(piece_size - p.start, int(block_size()));
			char* buffer = m_ses.allocate_disk_buffer("add piece");
			// out of memory
			if (buffer == 0)
			{
				picker().dec_refcount(piece, 0);
				return;
			}
			disk_buffer_holder holder(m_ses, buffer);
			std::memcpy(buffer, data + p.start, p.length);
	
			if (!need_loaded())
			{
				// failed to load .torrent file
				picker().dec_refcount(piece, 0);
				return;
			}
			inc_refcount("add_piece");
			m_ses.disk_thread().async_write(&storage(), p, holder
				, boost::bind(&torrent::on_disk_write_complete
				, shared_from_this(), _1, p));
			piece_block block(piece, i);
			picker().mark_as_downloading(block, 0);
			picker().mark_as_writing(block, 0);
		}
		verify_piece(piece);
		picker().dec_refcount(piece, 0);
	}

	void torrent::schedule_storage_tick()
	{
		// schedule a disk tick in 2 minutes or so
		if (m_storage_tick != 0) return;
		m_storage_tick = 120 + (random() % 60);
		update_want_tick();
	}

	void torrent::on_disk_write_complete(disk_io_job const* j
		, peer_request p)
	{
		// hold a reference until this function returns
		torrent_ref_holder h(this, "add_piece");

		dec_refcount("add_piece");
		TORRENT_ASSERT(is_single_thread());

		schedule_storage_tick();

//		fprintf(stderr, "torrent::on_disk_write_complete ret:%d piece:%d block:%d\n"
//			, j->ret, j->piece, j->offset/0x4000);

		INVARIANT_CHECK;

		if (m_abort)
		{
			piece_block block_finished(p.piece, p.start / block_size());
			return;
		}

		piece_block block_finished(p.piece, p.start / block_size());

		if (j->ret == -1)
		{
			handle_disk_error(j);
			return;
		}

		if (!has_picker()) return;

		// if we already have this block, just ignore it.
		// this can happen if the same block is passed in through
		// add_piece() multiple times
		if (picker().is_finished(block_finished)) return;

		picker().mark_as_finished(block_finished, 0);
		maybe_done_flushing();
	}
	
	void torrent::on_disk_cache_complete(disk_io_job const* j)
	{
		TORRENT_ASSERT(have_piece(j->piece));

		dec_refcount("cache_piece");

		if (j->ret < 0) return;

		// suggest this piece to all peers
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
			(*i)->send_suggest(j->piece);
	}

	void torrent::on_disk_tick_done(disk_io_job const* j)
	{
		if (j->ret && m_storage_tick == 0)
		{
			m_storage_tick = 120 + (random() % 20);
			update_want_tick();
		}
	}

	bool torrent::add_merkle_nodes(std::map<int, sha1_hash> const& nodes, int piece)
	{
		return m_torrent_file->add_merkle_nodes(nodes, piece);
	}

	peer_request torrent::to_req(piece_block const& p) const
	{
		int block_offset = p.block_index * block_size();
		int block = (std::min)(torrent_file().piece_size(
			p.piece_index) - block_offset, int(block_size()));
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

	void torrent::add_extension(boost::shared_ptr<torrent_plugin> ext)
	{
		m_extensions.push_back(ext);
	}

	void torrent::remove_extension(boost::shared_ptr<torrent_plugin> ext)
	{
		extension_list_t::iterator i = std::find(m_extensions.begin(), m_extensions.end(), ext);
		if (i == m_extensions.end()) return;
		m_extensions.erase(i);
	}

	void torrent::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
		, void* userdata)
	{
		boost::shared_ptr<torrent_plugin> tp(ext(this, userdata));
		if (!tp) return;

		add_extension(tp);
		
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			boost::shared_ptr<peer_plugin> pp(tp->new_connection(p));
			if (pp) p->add_extension(pp);
		}

		// if files are checked for this torrent, call the extension
		// to let it initialize itself
		if (m_connections_initialized)
			tp->on_files_checked();
	}

#endif

#ifdef TORRENT_USE_OPENSSL

#if BOOST_VERSION >= 104700
	bool torrent::verify_peer_cert(bool preverified, boost::asio::ssl::verify_context& ctx)
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
		GENERAL_NAMES* gens = static_cast<GENERAL_NAMES*>(
			X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0));

#if defined TORRENT_LOGGING
		std::string names;
		bool match = false;
#endif
		for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i)
		{
			GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
			if (gen->type != GEN_DNS) continue;
			ASN1_IA5STRING* domain = gen->d.dNSName;
			if (domain->type != V_ASN1_IA5STRING || !domain->data || !domain->length) continue;
			const char* torrent_name = reinterpret_cast<const char*>(domain->data);
			std::size_t name_length = domain->length;

#if defined TORRENT_LOGGING
			if (i > 1) names += " | n: ";
			names.append(torrent_name, name_length);
#endif
			if (strncmp(torrent_name, "*", name_length) == 0
				|| strncmp(torrent_name, m_torrent_file->name().c_str(), name_length) == 0)
			{
#if defined TORRENT_LOGGING
				match = true;
				// if we're logging, keep looping over all names,
				// for completeness of the log
				continue;
#endif
				return true;
			}
		}

		// no match in the alternate names, so try the common names. We should only
		// use the "most specific" common name, which is the last one in the list.
		X509_NAME* name = X509_get_subject_name(cert);
		int i = -1;
		ASN1_STRING* common_name = 0;
		while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0)
		{
			X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
			common_name = X509_NAME_ENTRY_get_data(name_entry);
		}
		if (common_name && common_name->data && common_name->length)
		{
			const char* torrent_name = reinterpret_cast<const char*>(common_name->data);
			std::size_t name_length = common_name->length;

#if defined TORRENT_LOGGING
			if (!names.empty()) names += " | n: ";
			names.append(torrent_name, name_length);
#endif

			if (strncmp(torrent_name, "*", name_length) == 0
				|| strncmp(torrent_name, m_torrent_file->name().c_str(), name_length) == 0)
			{
#if !defined TORRENT_LOGGING
				return true;
#else
				match = true;
#endif
			}
		}

#if defined TORRENT_LOGGING
		debug_log("<== incoming SSL CONNECTION [ n: %s | match: %s ]"
			, names.c_str(), match?"yes":"no");
		return match;
#endif

		return false;
	}
#endif // BOOST_VERSION

	void torrent::init_ssl(std::string const& cert)
	{
		using boost::asio::ssl::context;

		// this is needed for openssl < 1.0 to decrypt keys created by openssl 1.0+
		OpenSSL_add_all_algorithms();

		boost::uint64_t now = clock_type::now().time_since_epoch().count();
		// assume 9 bits of entropy (i.e. about 1 millisecond)
		RAND_add(&now, 8, 1.125);
		RAND_add(&info_hash()[0], 20, 3);
		// entropy is also added on incoming and completed connection attempts

		TORRENT_ASSERT(RAND_status() == 1);

#if BOOST_VERSION >= 104700
		// create the SSL context for this torrent. We need to
		// inject the root certificate, and no other, to
		// verify other peers against
		boost::shared_ptr<context> ctx = boost::make_shared<context>(boost::ref(m_ses.get_io_service()), context::sslv23);

		if (!ctx)
		{
			error_code ec(::ERR_get_error(),
				asio::error::get_ssl_category());
			set_error(ec, error_file_ssl_ctx);
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
			set_error(ec, error_file_ssl_ctx);
			pause();
			return;
		}

		// the verification function verifies the distinguished name
		// of a peer certificate to make sure it matches the info-hash
		// of the torrent, or that it's a "star-cert"
		ctx->set_verify_callback(boost::bind(&torrent::verify_peer_cert, this, _1, _2), ec);
		if (ec)
		{
			set_error(ec, error_file_ssl_ctx);
			pause();
			return;
		}

		SSL_CTX* ssl_ctx = ctx->impl();
		// create a new x.509 certificate store
		X509_STORE* cert_store = X509_STORE_new();
		if (!cert_store)
		{
			error_code ec(::ERR_get_error(),
				asio::error::get_ssl_category());
			set_error(ec, error_file_ssl_ctx);
			pause();
			return;
		}

		// wrap the PEM certificate in a BIO, for openssl to read
		BIO* bp = BIO_new_mem_buf((void*)cert.c_str(), cert.size());

		// parse the certificate into OpenSSL's internal
		// representation
		X509* certificate = PEM_read_bio_X509_AUX(bp, 0, 0, 0);

		BIO_free(bp);

		if (!certificate)
		{
			error_code ec(::ERR_get_error(),
				asio::error::get_ssl_category());
			X509_STORE_free(cert_store);
			set_error(ec, error_file_ssl_ctx);
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
		snprintf(filename, sizeof(filename), "/tmp/%u.pem", random());
		FILE* f = fopen(filename, "w+");
		fwrite(cert.c_str(), cert.size(), 1, f);
		fclose(f);
		ctx->load_verify_file(filename);
#endif
		// if all went well, set the torrent ssl context to this one
		m_ssl_ctx = ctx;
		// tell the client we need a cert for this torrent
		alerts().post_alert(torrent_need_cert_alert(get_handle()));
#else
		set_error(asio::error::operation_not_supported, error_file_ssl_ctx);
		pause();
#endif
	}

#endif // TORRENT_OPENSSL

	void torrent::construct_storage()
	{
		storage_params params;

		if (&m_torrent_file->orig_files() != &m_torrent_file->files())
		{
			params.mapped_files = &m_torrent_file->files();
			params.files = &m_torrent_file->orig_files();
		}
		else
		{
			params.files = &m_torrent_file->files();
			params.mapped_files = 0;
		}
		params.path = m_save_path;
		params.pool = &m_ses.disk_thread().files();
		params.mode = (storage_mode_t)m_storage_mode;
		params.priorities = &m_file_priority;
		params.info = m_torrent_file.get();

		TORRENT_ASSERT(m_storage_constructor);
		storage_interface* storage_impl = m_storage_constructor(params);

		// the shared_from_this() will create an intentional
		// cycle of ownership, se the hpp file for description.
		m_storage = boost::make_shared<piece_manager>(
			storage_impl, shared_from_this(), (file_storage*)&m_torrent_file->files());
	}

	peer_connection* torrent::find_lowest_ranking_peer() const
	{
		const_peer_iterator lowest_rank = end();
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			// disconnecting peers don't count
			if ((*i)->is_disconnecting()) continue;
			if (lowest_rank == end() || (*lowest_rank)->peer_rank() > (*i)->peer_rank())
				lowest_rank = i;
		}

		if (lowest_rank == end()) return NULL;
		return *lowest_rank;
	}

	// this may not be called from a constructor because of the call to
	// shared_from_this()
	void torrent::init()
	{
		TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_LOGGING
		debug_log("init torrent: %s", torrent_file().name().c_str());
#endif

		if (!need_loaded()) return;
		TORRENT_ASSERT(m_torrent_file->num_files() > 0);
		TORRENT_ASSERT(m_torrent_file->is_valid());
		TORRENT_ASSERT(m_torrent_file->total_size() >= 0);

		if (int(m_file_priority.size()) > m_torrent_file->num_files())
			m_file_priority.resize(m_torrent_file->num_files());

		std::string cert = m_torrent_file->ssl_cert();
		if (!cert.empty())
		{
			m_ssl_torrent = true;
#ifdef TORRENT_USE_OPENSSL
			init_ssl(cert);
#endif
		}

		m_block_size_shift = root2((std::min)(int(block_size()), m_torrent_file->piece_length()));

		if (m_torrent_file->num_pieces() > piece_picker::max_pieces)
		{
			set_error(errors::too_many_pieces_in_torrent, error_file_none);
			pause();
			return;
		}

		if (m_torrent_file->num_pieces() == 0)
		{
			set_error(errors::torrent_invalid_length, error_file_none);
			pause();
			return;
		}

		if (m_resume_data && m_resume_data->node.type() == bdecode_node::dict_t)
		{
			int ev = 0;
			if (m_resume_data->node.dict_find_string_value("file-format")
				!= "libtorrent resume file")
			{
				ev = errors::invalid_file_tag;
			}
	
			std::string info_hash = m_resume_data->node.dict_find_string_value("info-hash");
			if (!ev && info_hash.empty())
				ev = errors::missing_info_hash;

			if (!ev && sha1_hash(info_hash) != m_torrent_file->info_hash())
				ev = errors::mismatching_info_hash;

			if (ev && m_ses.alerts().should_post<fastresume_rejected_alert>())
			{
				error_code ec = error_code(ev, get_libtorrent_category());
				m_ses.alerts().post_alert(fastresume_rejected_alert(get_handle(), ec, "", 0));
			}

			if (ev)
			{
#if defined TORRENT_LOGGING
				debug_log("fastresume data rejected: %s"
					, error_code(ev, get_libtorrent_category()).message().c_str());
#endif
				m_resume_data.reset();
			}
			else
			{
				read_resume_data(m_resume_data->node);
			}
		}
	
#if TORRENT_USE_ASSERTS
		m_resume_data_loaded = true;
#endif

		construct_storage();

		if (!m_seed_mode && m_resume_data)
		{
			bdecode_node piece_priority = m_resume_data->node
				.dict_find_string("piece_priority");

			if (piece_priority && piece_priority.string_length()
				== m_torrent_file->num_pieces())
			{
				char const* p = piece_priority.string_ptr();
				for (int i = 0; i < piece_priority.string_length(); ++i)
				{
					int prio = p[i];
					if (!has_picker() && prio == 1) continue;
					need_picker();
					m_picker->set_piece_priority(i, p[i]);
					update_gauge();
				}
			}
		}

		if (m_share_mode && valid_metadata())
		{
			// in share mode, all pieces have their priorities initialized to 0
			m_file_priority.clear();
			m_file_priority.resize(m_torrent_file->num_files(), 0);
		}

		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			// copy the peer list since peers may disconnect and invalidate
			// m_connections as we initialize them
			std::vector<peer_connection*> peers = m_connections;
			for (torrent::peer_iterator i = peers.begin();
				i != peers.end(); ++i)
			{
				peer_connection* pc = *i;
				if (pc->is_disconnecting()) continue;
				pc->on_metadata_impl();
				if (pc->is_disconnecting()) continue;
				pc->init();
			}
		}

		// in case file priorities were passed in via the add_torrent_params
		// and also in the case of share mode, we need to update the priorities
		update_piece_priorities();

		std::vector<web_seed_entry> const& web_seeds = m_torrent_file->web_seeds();
		m_web_seeds.insert(m_web_seeds.end(), web_seeds.begin(), web_seeds.end());

		set_state(torrent_status::checking_resume_data);
	
#if TORRENT_USE_ASSERTS
		m_resume_data_loaded = true;
#endif

		if (m_seed_mode)
		{
			m_have_all = true;
			m_ses.get_io_service().post(boost::bind(&torrent::files_checked, shared_from_this()));
			m_resume_data.reset();
			update_gauge();
			return;
		}

		int num_pad_files = 0;
		TORRENT_ASSERT(block_size() > 0);
		file_storage const& fs = m_torrent_file->files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			if (fs.pad_file_at(i)) ++num_pad_files;

			if (!fs.pad_file_at(i) || fs.file_size(i) == 0) continue;
			m_padding += boost::uint32_t(fs.file_size(i));
			
			// TODO: instead of creating the picker up front here,
			// maybe this whole section should move to need_picker()
			need_picker();

			peer_request pr = m_torrent_file->map_file(i, 0, fs.file_size(i));
			int off = pr.start & (block_size()-1);
			if (off != 0) { pr.length -= block_size() - off; pr.start += block_size() - off; }
			TORRENT_ASSERT((pr.start & (block_size()-1)) == 0);

			int block = block_size();
			int blocks_per_piece = m_torrent_file->piece_length() / block;
			piece_block pb(pr.piece, pr.start / block);
			for (; pr.length >= block; pr.length -= block, ++pb.block_index)
			{
				if (int(pb.block_index) == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
				m_picker->mark_as_finished(pb, 0);
			}
			// ugly edge case where padfiles are not used they way they're
			// supposed to be. i.e. added back-to back or at the end
			if (int(pb.block_index) == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
			if (pr.length > 0 && ((i+1 != fs.num_files() && fs.pad_file_at(i+1))
				|| i + 1 == fs.num_files()))
			{
				m_picker->mark_as_finished(pb, 0);
			}
		}

		if (m_padding > 0)
		{
			// if we marked an entire piece as finished, we actually
			// need to consider it finished

			std::vector<piece_picker::downloading_piece> dq
				= m_picker->get_download_queue();

			std::vector<int> have_pieces;

			for (std::vector<piece_picker::downloading_piece>::const_iterator i
				= dq.begin(); i != dq.end(); ++i)
			{
				int num_blocks = m_picker->blocks_in_piece(i->index);
				if (i->finished < num_blocks) continue;
				have_pieces.push_back(i->index);
			}

			for (std::vector<int>::iterator i = have_pieces.begin();
				i != have_pieces.end(); ++i)
			{
				picker().piece_passed(*i);
				TORRENT_ASSERT(picker().have_piece(*i));
				we_have(*i);
				update_gauge();
			}
		}

		if (!need_loaded()) return;

		if (num_pad_files > 0)
			m_picker->set_num_pad_files(num_pad_files);

		std::auto_ptr<std::vector<std::string> > links;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		if (!m_torrent_file->similar_torrents().empty()
			|| !m_torrent_file->collections().empty())
		{
			resolve_links res(m_torrent_file);

			std::vector<sha1_hash> s = m_torrent_file->similar_torrents();
			for (std::vector<sha1_hash>::iterator i = s.begin(), end(s.end());
				i != end; ++i)
			{
				boost::shared_ptr<torrent> t = m_ses.find_torrent(*i).lock();
				if (!t) continue;

				// Only attempt to reuse files from torrents that are seeding.
				// TODO: this could be optimized by looking up which files are
				// complete and just look at those
				if (!t->is_seed()) continue;

				res.match(t->get_torrent_copy(), t->save_path());
			}
			std::vector<std::string> c = m_torrent_file->collections();
			for (std::vector<std::string>::iterator i = c.begin(), end(c.end());
				i != end; ++i)
			{
				std::vector<boost::shared_ptr<torrent> > ts = m_ses.find_collection(*i);

				for (std::vector<boost::shared_ptr<torrent> >::iterator k = ts.begin()
					, end(ts.end()); k != end; ++k)
				{
					// Only attempt to reuse files from torrents that are seeding.
					// TODO: this could be optimized by looking up which files are
					// complete and just look at those
					if (!(*k)->is_seed()) continue;

					res.match((*k)->get_torrent_copy(), (*k)->save_path());
				}
			}

			std::vector<resolve_links::link_t> const& l = res.get_links();
			if (!l.empty())
			{
				links.reset(new std::vector<std::string>(l.size()));
				for (std::vector<resolve_links::link_t>::const_iterator i = l.begin()
					, end(l.end()); i != end; ++i)
				{
					if (!i->ti) continue;

					torrent_info const& ti = *i->ti;
					std::string const& save_path = i->save_path;
					links->push_back(combine_path(save_path
						, ti.files().file_path(i->file_idx)));
				}
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		inc_refcount("check_fastresume");
		// async_check_fastresume will release links
		m_ses.disk_thread().async_check_fastresume(
			m_storage.get(), m_resume_data ? &m_resume_data->node : NULL
			, links, boost::bind(&torrent::on_resume_data_checked
			, shared_from_this(), _1));
#if defined TORRENT_LOGGING
		debug_log("init, async_check_fastresume");
#endif

		update_want_peers();

		maybe_done_flushing();
	}

	bool torrent::need_loaded()
	{
		m_should_be_loaded = true;

		// if we don't have the metadata yet, pretend the file is loaded
		if (!m_torrent_file->is_valid()
			|| m_torrent_file->is_loaded())
		{
			// bump this torrent to the top of the torrent LRU of
			// which torrents are most active
			m_ses.bump_torrent(this);

			return true;
		}

		// load the specified torrent and also evict one torrent,
		// except for the one specified. if we're not at our limit
		// yet, no torrent is evicted
		return m_ses.load_torrent(this);
	}

	void torrent::dec_refcount(char const* purpose)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_refcount > 0);
		--m_refcount;
		if (m_refcount == 0)
		{
			if (!m_pinned)
				inc_stats_counter(counters::num_pinned_torrents, -1);

			if (m_should_be_loaded == false)
				unload();
		}
	}

	void torrent::inc_refcount(char const* purpose)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(is_loaded());
		++m_refcount;
		if (!m_pinned && m_refcount == 1)
			inc_stats_counter(counters::num_pinned_torrents);
	}

	void torrent::set_pinned(bool p)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_pinned == p) return;
		m_pinned = p;

		if (m_refcount == 0)
			inc_stats_counter(counters::num_pinned_torrents, p ? 1 : -1);

		// if the torrent was just un-pinned, we need to insert
		// it into the LRU
		m_ses.bump_torrent(this, true);
	}

	bool torrent::load(std::vector<char>& buffer)
	{
		error_code ec;
		m_torrent_file->load(&buffer[0], buffer.size(), ec);
		if (ec)
		{
			set_error(ec, error_file_metadata);
			return false;
		}

		state_updated();
/*
#ifndef TORRENT_DISABLE_EXTENSIONS
		// create the extensions again

		// TOOD: should we store add_torrent_params::userdata
		// in torrent just to have it available here?
		m_ses.add_extensions_to_torrent(shared_from_this(), NULL);

		// and call on_load() on them
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_load();
			} TORRENT_CATCH (std::exception&) {}
		}
#endif
*/

		inc_stats_counter(counters::num_loaded_torrents);

		construct_storage();

		return true;
	}

	// this is called when this torrent hasn't been active in long enough
	// to warrant swapping it out, in favor of a more active torrent.
	void torrent::unload()
	{
		TORRENT_ASSERT(is_loaded());

		// pinned torrents are not allowed to be swapped out
		TORRENT_ASSERT(!m_pinned);

		m_should_be_loaded = false;

		// make sure it's not unloaded in the middle of some operation that uses it
		if (m_refcount > 0) return;

		// call on_unload() on extensions
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_unload();
			} TORRENT_CATCH (std::exception&) {}
		}

		// also remove extensions and re-instantiate them when the torrent is loaded again
		// they end up using a significant amount of memory
		// TODO: there may be peer extensions relying on the torrent extension
		// still being alive. Only do this if there are no peers. And when the last peer
		// is disconnected, if the torrent is unloaded, clear the extensions
//		m_extensions.clear();
#endif

		// someone else holds a reference to the torrent_info
		// make the torrent release its reference to it,
		// after making a copy and then unloading that version
		// as soon as the user is done with its copy of torrent_info
		// it will be freed, and we'll have the unloaded version left
		if (!m_torrent_file.unique())
			m_torrent_file = boost::make_shared<torrent_info>(*m_torrent_file);

		m_torrent_file->unload();
		inc_stats_counter(counters::num_loaded_torrents, -1);

		m_storage.reset();

		state_updated();
	}

	bt_peer_connection* torrent::find_introducer(tcp::endpoint const& ep) const
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (const_peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
		{
			if ((*i)->type() != peer_connection::bittorrent_connection) continue;
			bt_peer_connection* p = (bt_peer_connection*)(*i);
			if (!p->supports_holepunch()) continue;
			peer_plugin const* pp = p->find_plugin("ut_pex");
			if (!pp) continue;
			if (was_introduced_by(pp, ep)) return (bt_peer_connection*)p;
		}
#endif
		return NULL;
	}

	bt_peer_connection* torrent::find_peer(tcp::endpoint const& ep) const
	{
		for (const_peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			if (p->type() != peer_connection::bittorrent_connection) continue;
			if (p->remote() == ep) return (bt_peer_connection*)p;
		}
		return NULL;
	}

	peer_connection* torrent::find_peer(sha1_hash const& pid)
	{
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			if (p->pid() == pid) return p;
		}
		return 0;
	}

	void torrent::on_resume_data_checked(disk_io_job const* j)
	{
		// hold a reference until this function returns
		torrent_ref_holder h(this, "check_fastresume");

		// when applying some of the resume data to the torrent, we will
		// trigger calls that set m_need_save_resume_data, even though we're
		// just applying the state of the resume data we loaded with. We don't
		// want anything in this function to affect the state of
		// m_need_save_resume_data, so we save it in a local variable and reset
		// it at the end of the function.
		bool need_save_resume_data = m_need_save_resume_data;

		dec_refcount("check_fastresume");
		TORRENT_ASSERT(is_single_thread());

		if (j->ret == piece_manager::fatal_disk_error)
		{
			handle_disk_error(j);
			auto_managed(false);
			pause();
			set_state(torrent_status::checking_files);
			if (should_check_files()) start_checking();
			m_resume_data.reset();
			return;
		}

		state_updated();

		if (m_resume_data && m_resume_data->node.type() == bdecode_node::dict_t)
		{
			using namespace libtorrent::detail; // for read_*_endpoint()

			if (bdecode_node peers_entry = m_resume_data->node.dict_find_string("peers"))
			{
				int num_peers = peers_entry.string_length() / (sizeof(address_v4::bytes_type) + 2);
				char const* ptr = peers_entry.string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					add_peer(read_v4_endpoint<tcp::endpoint>(ptr)
						, peer_info::resume_data);
				}
				update_want_peers();
			}

			if (bdecode_node banned_peers_entry
				= m_resume_data->node.dict_find_string("banned_peers"))
			{
				int num_peers = banned_peers_entry.string_length() / (sizeof(address_v4::bytes_type) + 2);
				char const* ptr = banned_peers_entry.string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					std::vector<torrent_peer*> peers;
					torrent_peer* p = add_peer(read_v4_endpoint<tcp::endpoint>(ptr)
						, peer_info::resume_data);
					peers_erased(peers);
					if (p) ban_peer(p);
				}
				update_want_peers();
			}

#if TORRENT_USE_IPV6
			if (bdecode_node peers6_entry = m_resume_data->node.dict_find_string("peers6"))
			{
				int num_peers = peers6_entry.string_length() / (sizeof(address_v6::bytes_type) + 2);
				char const* ptr = peers6_entry.string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					add_peer(read_v6_endpoint<tcp::endpoint>(ptr)
						, peer_info::resume_data);
				}
				update_want_peers();
			}

			if (bdecode_node banned_peers6_entry = m_resume_data->node.dict_find_string("banned_peers6"))
			{
				int num_peers = banned_peers6_entry.string_length() / (sizeof(address_v6::bytes_type) + 2);
				char const* ptr = banned_peers6_entry.string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					torrent_peer* p = add_peer(read_v6_endpoint<tcp::endpoint>(ptr)
						, peer_info::resume_data);
					if (p) ban_peer(p);
				}
				update_want_peers();
			}
#endif

			// parse out "peers" from the resume data and add them to the peer list
			if (bdecode_node peers_entry = m_resume_data->node.dict_find_list("peers"))
			{
				for (int i = 0; i < peers_entry.list_size(); ++i)
				{
					bdecode_node e = peers_entry.list_at(i);
					if (e.type() != bdecode_node::dict_t) continue;
					std::string ip = e.dict_find_string_value("ip");
					int port = e.dict_find_int_value("port");
					if (ip.empty() || port == 0) continue;
					error_code ec;
					tcp::endpoint a(address::from_string(ip, ec), (unsigned short)port);
					if (ec) continue;
					add_peer(a, peer_info::resume_data);
				}
				update_want_peers();
			}

			// parse out "banned_peers" and add them as banned
			if (bdecode_node banned_peers_entry = m_resume_data->node.dict_find_list("banned_peers"))
			{	
				for (int i = 0; i < banned_peers_entry.list_size(); ++i)
				{
					bdecode_node e = banned_peers_entry.list_at(i);
					if (e.type() != bdecode_node::dict_t) continue;
					std::string ip = e.dict_find_string_value("ip");
					int port = e.dict_find_int_value("port");
					if (ip.empty() || port == 0) continue;
					error_code ec;
					tcp::endpoint a(address::from_string(ip, ec), (unsigned short)port);
					if (ec) continue;
					torrent_peer* p = add_peer(a, peer_info::resume_data);
					if (p) ban_peer(p);
				}
				update_want_peers();
			}
		}

#if defined TORRENT_LOGGING
		if (m_peer_list && m_peer_list->num_peers() > 0)
			debug_log("resume added peers (%d)", m_peer_list->num_peers());
#endif

		// only report this error if the user actually provided resume data
		if ((j->error || j->ret != 0) && m_resume_data
			&& m_ses.alerts().should_post<fastresume_rejected_alert>())
		{
			m_ses.alerts().post_alert(fastresume_rejected_alert(get_handle(), j->error.ec
				, resolve_filename(j->error.file), j->error.operation_str()));
		}

#if defined TORRENT_LOGGING
		if (j->ret != 0)
		{
			debug_log("fastresume data rejected: ret: %d (%d) %s"
				, j->ret, j->error.ec.value(), j->error.ec.message().c_str());
		}
#if defined TORRENT_LOGGING
		else
			debug_log("fastresume data accepted");
#endif
#endif

		// if ret != 0, it means we need a full check. We don't necessarily need
		// that when the resume data check fails. For instance, if the resume data
		// is incorrect, but we don't have any files, we skip the check and initialize
		// the storage to not have anything.
		if (j->ret == 0)
		{
			// there are either no files for this torrent
			// or the resume_data was accepted

			if (!j->error && m_resume_data && m_resume_data->node.type() == bdecode_node::dict_t)
			{
				// parse have bitmask
				bdecode_node pieces = m_resume_data->node.dict_find("pieces");
				if (pieces && pieces.type() == bdecode_node::string_t
					&& int(pieces.string_length()) == m_torrent_file->num_pieces())
				{
					char const* pieces_str = pieces.string_ptr();
					for (int i = 0, end(pieces.string_length()); i < end; ++i)
					{
						if (pieces_str[i] & 1)
						{
							need_picker();
							m_picker->we_have(i);
							inc_stats_counter(counters::num_piece_passed);
							update_gauge();
							we_have(i);
						}
						if (m_seed_mode && (pieces_str[i] & 2)) m_verified.set_bit(i);
					}
				}
				else
				{
					bdecode_node slots = m_resume_data->node.dict_find("slots");
					if (slots && slots.type() == bdecode_node::list_t)
					{
						for (int i = 0; i < slots.list_size(); ++i)
						{
							int piece = slots.list_int_value_at(i, -1);
							if (piece >= 0)
							{
								need_picker();
								m_picker->we_have(piece);
								update_gauge();
								inc_stats_counter(counters::num_piece_passed);
								we_have(piece);
							}
						}
					}
				}

				// parse unfinished pieces
				int num_blocks_per_piece =
					static_cast<int>(torrent_file().piece_length()) / block_size();

				if (bdecode_node unfinished_ent
					= m_resume_data->node.dict_find_list("unfinished"))
				{
					for (int i = 0; i < unfinished_ent.list_size(); ++i)
					{
						bdecode_node e = unfinished_ent.list_at(i);
						if (e.type() != bdecode_node::dict_t) continue;
						int piece = e.dict_find_int_value("piece", -1);
						if (piece < 0 || piece > torrent_file().num_pieces()) continue;

						if (has_picker() && m_picker->have_piece(piece))
						{
							m_picker->we_dont_have(piece);
							update_gauge();
						}

						std::string bitmask = e.dict_find_string_value("bitmask");
						if (bitmask.empty()) continue;

						need_picker();

						const int num_bitmask_bytes = (std::max)(num_blocks_per_piece / 8, 1);
						if ((int)bitmask.size() != num_bitmask_bytes) continue;
						for (int k = 0; k < num_bitmask_bytes; ++k)
						{
							unsigned char bits = bitmask[k];
							int num_bits = (std::min)(num_blocks_per_piece - k*8, 8);
							for (int b = 0; b < num_bits; ++b)
							{
								const int block = k * 8 + b;
								if (bits & (1 << b))
								{
									m_picker->mark_as_finished(piece_block(piece, block), 0);
								}
							}
						}
						if (m_picker->is_piece_finished(piece))
						{
							verify_piece(piece);
						}
					}
				}
			}

			files_checked();
		}
		else
		{
			// either the fastresume data was rejected or there are
			// some files
			set_state(torrent_status::checking_files);

			// start the checking right away (potentially)
			m_ses.trigger_auto_manage();
		}

		maybe_done_flushing();
		m_resume_data.reset();

		// restore m_need_save_resume_data to its state when we entered this
		// function.
		m_need_save_resume_data = need_save_resume_data;
	}

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

		if (!need_loaded()) return;

		disconnect_all(errors::stopping_torrent, op_bittorrent);
		stop_announcing();

		m_ses.disk_thread().async_release_files(m_storage.get()
			, boost::function<void(disk_io_job const*)>());

		// forget that we have any pieces
		m_have_all = false;

// removing the piece picker will clear the user priorities
// instead, just clear which pieces we have
//		m_picker.reset();
		if (m_picker)
		{
			int blocks_per_piece = (m_torrent_file->piece_length() + block_size() - 1) / block_size();
			int blocks_in_last_piece = ((m_torrent_file->total_size() % m_torrent_file->piece_length())
				+ block_size() - 1) / block_size();
			m_picker->init(blocks_per_piece, blocks_in_last_piece, m_torrent_file->num_pieces());
		}

		// file progress is allocated lazily, the first time the client
		// asks for it
		std::vector<boost::uint64_t>().swap(m_file_progress);

		// assume that we don't have anything
		m_files_checked = false;

		update_gauge();
		update_want_tick();
		set_state(torrent_status::checking_resume_data);

		if (m_auto_managed && !is_finished())
			set_queue_position((std::numeric_limits<int>::max)());

		m_resume_data.reset();

		std::auto_ptr<std::vector<std::string> > links;
		inc_refcount("force_recheck");
		m_ses.disk_thread().async_check_fastresume(m_storage.get(), NULL
			, links, boost::bind(&torrent::on_force_recheck
			, shared_from_this(), _1));
	}

	void torrent::on_force_recheck(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());

		// hold a reference until this function returns
		torrent_ref_holder h(this, "force_recheck");

		dec_refcount("force_recheck");
		state_updated();

		if (j->ret == piece_manager::fatal_disk_error)
		{
			handle_disk_error(j);
			return;
		}
		if (j->ret == 0)
		{
			// if there are no files, just start
			files_checked();
		}
		else
		{
			set_state(torrent_status::checking_files);
			if (m_auto_managed) pause(true);
			if (should_check_files()) start_checking();
			else m_ses.trigger_auto_manage();
		}
	}

	void torrent::start_checking()
	{
		TORRENT_ASSERT(should_check_files());

		int num_outstanding = m_ses.settings().get_int(settings_pack::checking_mem_usage) * block_size()
			/ m_torrent_file->piece_length();
		if (num_outstanding <= 0) num_outstanding = 1;

		// we might already have some outstanding jobs, if we were paused and
		// resumed quickly, before the outstanding jobs completed
		if (m_checking_piece >= m_torrent_file->num_pieces())
		{
#if defined TORRENT_LOGGING
			debug_log("start_checking, checking_piece >= num_pieces. %d >= %d"
				, m_checking_piece, m_torrent_file->num_pieces());
#endif
			return;
		}

		// subtract the number of pieces we already have outstanding
		num_outstanding -= (m_checking_piece - m_num_checked_pieces);
		if (num_outstanding < 0) num_outstanding = 0;

		if (!need_loaded())
		{
#if defined TORRENT_LOGGING
			debug_log("start_checking, need_loaded() failed");
#endif
			return;
		}

		for (int i = 0; i < num_outstanding; ++i)
		{
			inc_refcount("start_checking");
			m_ses.disk_thread().async_hash(m_storage.get(), m_checking_piece++
				, disk_io_job::sequential_access | disk_io_job::volatile_read
				, boost::bind(&torrent::on_piece_hashed
					, shared_from_this(), _1), (void*)1);
			if (m_checking_piece >= m_torrent_file->num_pieces()) break;
		}
#if defined TORRENT_LOGGING
		debug_log("start_checking, m_checking_piece: %d", m_checking_piece);
#endif
	}
	
	void nop() {}

	// This is only used for checking of torrents. i.e. force-recheck or initial checking
	// of existing files
	void torrent::on_piece_hashed(disk_io_job const* j)
	{
		// hold a reference until this function returns
		torrent_ref_holder h(this, "start_checking");

		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		dec_refcount("start_checking");

		if (j->ret == piece_manager::disk_check_aborted)
		{
			m_checking_piece = 0;
			m_num_checked_pieces = 0;
#if defined TORRENT_LOGGING
			debug_log("on_piece_hashed, disk_check_aborted");
#endif
			pause();
			return;
		}

		state_updated();

		++m_num_checked_pieces;

		if (j->ret < 0)
		{
			if (j->error.ec == boost::system::errc::no_such_file_or_directory
				|| j->error.ec == boost::asio::error::eof
#ifdef TORRENT_WINDOWS
				|| j->error.ec == error_code(ERROR_HANDLE_EOF, system_category())
#endif
				)
			{
				TORRENT_ASSERT(j->error.file >= 0);

				// skip this file by updating m_checking_piece to the first piece following it
				file_storage const& st = m_torrent_file->files();
				boost::uint64_t file_size = st.file_size(j->error.file);
				int last = st.map_file(j->error.file, file_size, 0).piece;
				if (m_checking_piece < last)
				{
					int diff = last - m_checking_piece;
					m_num_checked_pieces += diff;
					m_checking_piece += diff;
				}
			}
			else
			{
				m_checking_piece = 0;
				m_num_checked_pieces = 0;
				if (m_ses.alerts().should_post<file_error_alert>())
					m_ses.alerts().post_alert(file_error_alert(j->error.ec,
						resolve_filename(j->error.file), j->error.operation_str(), get_handle()));

#if defined TORRENT_LOGGING
				debug_log("on_piece_hashed, fatal disk error: (%d) %s", j->error.ec.value(), j->error.ec.message().c_str());
#endif
				auto_managed(false);
				pause();
				set_error(j->error.ec, j->error.file);

				// recalculate auto-managed torrents sooner
				// in order to start checking the next torrent
				m_ses.trigger_auto_manage();
				return;
			}
		}

		m_progress_ppm = boost::int64_t(m_num_checked_pieces) * 1000000 / torrent_file().num_pieces();

		// we're using the piece hashes here, we need the torrent to be loaded
		if (!need_loaded())
		{
#if defined TORRENT_LOGGING
			debug_log("on_piece_hashed, need_loaded failed");
#endif
			return;
		}

		if (m_ses.settings().get_bool(settings_pack::disable_hash_checks)
			|| sha1_hash(j->d.piece_hash) == m_torrent_file->hash_for_piece(j->piece))
		{
			if (has_picker() || !m_have_all)
			{
				need_picker();
				m_picker->we_have(j->piece);
				update_gauge();
			}
			we_have(j->piece);
		}
		else
		{
			// if the hash failed, remove it from the cache
			if (m_storage)
				m_ses.disk_thread().clear_piece(m_storage.get(), j->piece);
		}

		if (m_num_checked_pieces < m_torrent_file->num_pieces())
		{
			// we're not done yet, issue another job
			if (m_checking_piece >= m_torrent_file->num_pieces())
			{
				// actually, we already have outstanding jobs for
				// the remaining pieces. We just need to wait for them
				// to finish
				return;
			}

			if (m_graceful_pause_mode && !m_allow_peers && m_checking_piece == m_num_checked_pieces)
			{
				// we are in graceful pause mode, and we just completed the last outstanding job.
				// now we can be considered paused
				if (alerts().should_post<torrent_paused_alert>())
					alerts().post_alert(torrent_paused_alert(get_handle()));
			}

			// we paused the checking
			if (!should_check_files())
			{
#if defined TORRENT_LOGGING
				debug_log("on_piece_hashed, checking paused");
#endif
				return;
			}

			if (!need_loaded())
			{
#if defined TORRENT_LOGGING
				debug_log("on_piece_hashed, need_loaded failed");
#endif
				return;
			}

			inc_refcount("start_checking");
			m_ses.disk_thread().async_hash(m_storage.get(), m_checking_piece++
				, disk_io_job::sequential_access | disk_io_job::volatile_read
				, boost::bind(&torrent::on_piece_hashed
					, shared_from_this(), _1), (void*)1);
#if defined TORRENT_LOGGING
			debug_log("on_piece_hashed, m_checking_piece: %d", m_checking_piece);
#endif
			return;
		}

#if defined TORRENT_LOGGING
		debug_log("on_piece_hashed, completed");
#endif
		// we're done checking!
		files_checked();

		// recalculate auto-managed torrents sooner
		// in order to start checking the next torrent
		m_ses.trigger_auto_manage();

		// reset the checking state
		m_checking_piece = 0;
		m_num_checked_pieces = 0;
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent::use_interface(std::string net_interfaces)
	{
		settings_pack* p = new settings_pack;
		p->set_str(settings_pack::outgoing_interfaces, net_interfaces);
		m_ses.apply_settings_pack(p);
	}
#endif

	void torrent::on_tracker_announce_disp(boost::weak_ptr<torrent> p
		, error_code const& e)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("tracker::on_tracker_announce_disp");
#endif
		if (e) return;
		boost::shared_ptr<torrent> t = p.lock();
		if (!t) return;
		t->on_tracker_announce();
	}

	void torrent::on_tracker_announce()
	{
		TORRENT_ASSERT(is_single_thread());
		m_waiting_tracker = false;	
		if (m_abort) return;
		announce_with_tracker();
	}

	void torrent::lsd_announce()
	{
		if (m_abort) return;

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
		m_ses.announce_lsd(m_torrent_file->info_hash(), port
			, m_ses.settings().get_bool(settings_pack::broadcast_lsd) && m_lsd_seq == 0);
		++m_lsd_seq;
	}

#ifndef TORRENT_DISABLE_DHT

	void torrent::dht_announce()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ses.dht())
		{
#if defined TORRENT_LOGGING
			debug_log("DHT: no dht initialized");
#endif
			return;
		}
		if (!should_announce_dht())
		{
#if defined TORRENT_LOGGING
			if (!m_ses.announce_dht())
				debug_log("DHT: no listen sockets");

			if (m_torrent_file->is_valid() && !m_files_checked)
				debug_log("DHT: files not checked, skipping DHT announce");

			if (!m_announce_to_dht)
				debug_log("DHT: queueing disabled DHT announce");

			if (!m_allow_peers)
				debug_log("DHT: torrent paused, no DHT announce");

			if (!m_torrent_file->is_valid() && !m_url.empty())
				debug_log("DHT: no info-hash, waiting for \"%s\"", m_url.c_str());

			if (m_torrent_file->is_valid() && m_torrent_file->priv())
				debug_log("DHT: private torrent, no DHT announce");

			if (settings().get_bool(settings_pack::use_dht_as_fallback))
			{
				int verified_trackers = 0;
				for (std::vector<announce_entry>::const_iterator i = m_trackers.begin()
					, end(m_trackers.end()); i != end; ++i)
					if (i->verified) ++verified_trackers;

				if (verified_trackers > 0)
					debug_log("DHT: only using DHT as fallback, and there are %d working trackers", verified_trackers);
			}
#endif
			return;
		}

		TORRENT_ASSERT(m_allow_peers);

#ifdef TORRENT_USE_OPENSSL
		int port = is_ssl_torrent() ? m_ses.ssl_listen_port() : m_ses.listen_port();
#else
		int port = m_ses.listen_port();
#endif

#if defined TORRENT_LOGGING
		debug_log("START DHT announce");
		m_dht_start_time = clock_type::now();
#endif

		// if we're a seed, we tell the DHT for better scrape stats
		int flags = is_seed() ? dht::dht_tracker::flag_seed : 0;
		// if we allow incoming uTP connections, set the implied_port
		// argument in the announce, this will make the DHT node use
		// our source port in the packet as our listen port, which is
		// likely more accurate when behind a NAT
		if (settings().get_bool(settings_pack::enable_incoming_utp))
			flags |= dht::dht_tracker::flag_implied_port;

		boost::weak_ptr<torrent> self(shared_from_this());
		m_ses.dht()->announce(m_torrent_file->info_hash()
			, port, flags
			, boost::bind(&torrent::on_dht_announce_response_disp, self, _1));
	}

	void torrent::on_dht_announce_response_disp(boost::weak_ptr<libtorrent::torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		boost::shared_ptr<libtorrent::torrent> tor = t.lock();
		if (!tor) return;
		tor->on_dht_announce_response(peers);
	}

	void torrent::on_dht_announce_response(std::vector<tcp::endpoint> const& peers)
	{
		TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_LOGGING
		debug_log("END DHT announce (%d ms) (%d peers)"
			, int(total_milliseconds(clock_type::now() - m_dht_start_time))
			, int(peers.size()));
#endif

		if (peers.empty()) return;

		if (m_ses.alerts().should_post<dht_reply_alert>())
		{
			m_ses.alerts().post_alert(dht_reply_alert(
				get_handle(), peers.size()));
		}

		if (torrent_file().priv() || (torrent_file().is_i2p()
			&& !settings().get_bool(settings_pack::allow_i2p_mixed))) return;

		std::for_each(peers.begin(), peers.end(), boost::bind(
			&torrent::add_peer, this, _1, peer_info::dht, 0));

		do_connect_boost();

		update_want_peers();
	}

#endif

	void torrent::announce_with_tracker(boost::uint8_t e
		, address const& bind_interface)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_trackers.empty())
		{
#if defined TORRENT_LOGGING
			debug_log("*** announce_with_tracker: no trackers");
#endif
			return;
		}

		if (m_abort) e = tracker_request::stopped;

		// if we're not announcing to trackers, only allow
		// stopping
		if (e != tracker_request::stopped && !m_announce_to_trackers)
		{
#if defined TORRENT_LOGGING
			debug_log("*** announce_with_tracker: event != stopped && !m_announce_to_trackers");
#endif
			return;
		}

		// if we're not allowing peers, there's no point in announcing
		if (e != tracker_request::stopped && !m_allow_peers)
		{
#if defined TORRENT_LOGGING
			debug_log("*** announce_with_tracker: event != stopped && !m_allow_peers");
#endif
			return;
		}

		TORRENT_ASSERT(m_allow_peers || e == tracker_request::stopped);

		if (e == tracker_request::none && is_finished() && !is_seed())
			e = tracker_request::paused;

		tracker_request req;
		req.apply_ip_filter = m_apply_ip_filter
			&& m_ses.settings().get_bool(settings_pack::apply_ip_filter_to_trackers);
		req.info_hash = m_torrent_file->info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download() - m_total_failed_bytes;
		req.uploaded = m_stat.total_payload_upload();
		req.corrupt = m_total_failed_bytes;
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;
#ifdef TORRENT_USE_OPENSSL
		// if this torrent contains an SSL certificate, make sure
		// any SSL tracker presents a certificate signed by it
		req.ssl_ctx = m_ssl_ctx.get();
#endif

		// exclude redundant bytes if we should
		if (!settings().get_bool(settings_pack::report_true_downloaded))
			req.downloaded -= m_total_redundant_bytes;
		if (req.downloaded < 0) req.downloaded = 0;

		req.event = e;
		error_code ec;

		// if we are aborting. we don't want any new peers
		req.num_want = (req.event == tracker_request::stopped)
			?0:settings().get_int(settings_pack::num_want);

		time_point now = clock_type::now();

		// the tier is kept as INT_MAX until we find the first
		// tracker that works, then it's set to that tracker's
		// tier.
		int tier = INT_MAX;

		// have we sent an announce in this tier yet?
		bool sent_announce = false;

		for (int i = 0; i < int(m_trackers.size()); ++i)
		{
			announce_entry& ae = m_trackers[i];
#if defined TORRENT_LOGGING
			debug_log("*** announce with tracker: considering \"%s\" "
				"[ announce_to_all_tiers: %d announce_to_all_trackers: %d"
				" i->tier: %d tier: %d "
				" is_working: %d fails: %d fail_limit: %d updating: %d"
				" can_announce: %d sent_announce: %d ]"
				, ae.url.c_str(), settings().get_bool(settings_pack::announce_to_all_tiers)
				, settings().get_bool(settings_pack::announce_to_all_trackers)
				, ae.tier, tier, ae.is_working(), ae.fails, ae.fail_limit
				, ae.updating, ae.can_announce(now, is_seed()), sent_announce);
#endif
			// if trackerid is not specified for tracker use default one, probably set explicitly
			req.trackerid = ae.trackerid.empty() ? m_trackerid : ae.trackerid;
			if (settings().get_bool(settings_pack::announce_to_all_tiers)
				&& !settings().get_bool(settings_pack::announce_to_all_trackers)
				&& sent_announce
				&& ae.tier <= tier
				&& tier != INT_MAX)
				continue;

			if (ae.tier > tier && sent_announce
				&& !settings().get_bool(settings_pack::announce_to_all_tiers)) break;
			if (ae.is_working()) { tier = ae.tier; sent_announce = false; }
			if (!ae.can_announce(now, is_seed()))
			{
				// this counts
				if (ae.is_working()) sent_announce = true;
				continue;
			}
			
			req.url = ae.url;
			req.event = e;
			if (req.event == tracker_request::none)
			{
				if (!ae.start_sent) req.event = tracker_request::started;
				else if (!ae.complete_sent && is_seed()) req.event = tracker_request::completed;
			}

			req.bind_ip = bind_interface;

			if (settings().get_bool(settings_pack::force_proxy))
			{
				// in force_proxy mode we don't talk directly to trackers
				// we only allow trackers if there is a proxy and issue
				// a warning if there isn't one
				std::string protocol = req.url.substr(0, req.url.find(':'));
				int proxy_type = m_ses.settings().get_int(settings_pack::proxy_type);
	
				// http can run over any proxy, so as long as one is used
				// it's OK. If no proxy is configured, skip this tracker
				if ((protocol == "http" || protocol == "https")
					&& proxy_type == settings_pack::none)
				{
					ae.next_announce = now + minutes(10);
					if (m_ses.alerts().should_post<anonymous_mode_alert>())
					{
						m_ses.alerts().post_alert(
							anonymous_mode_alert(get_handle()
								, anonymous_mode_alert::tracker_not_anonymous, req.url));
					}
					continue;
				}

				// for UDP, only socks5 and i2p proxies will work.
				// if we're not using one of those proxues with a UDP
				// tracker, skip it
				if (protocol == "udp"
					&& proxy_type != settings_pack::socks5
					&& proxy_type != settings_pack::socks5_pw
					&& proxy_type != settings_pack::i2p_proxy)
				{
					ae.next_announce = now + minutes(10);
					if (m_ses.alerts().should_post<anonymous_mode_alert>())
					{
						m_ses.alerts().post_alert(
							anonymous_mode_alert(get_handle()
								, anonymous_mode_alert::tracker_not_anonymous, req.url));
					}
					continue;
				}
			}

			req.auth = tracker_login();
			req.key = tracker_key();

#if defined TORRENT_LOGGING
			debug_log("==> TRACKER REQUEST \"%s\" event: %s abort: %d"
				, req.url.c_str()
				, (req.event==tracker_request::stopped?"stopped"
					:req.event==tracker_request::started?"started":"")
				, m_abort);

			if (m_abort)
			{
				boost::shared_ptr<aux::tracker_logger> tl(new aux::tracker_logger(m_ses));
				m_ses.queue_tracker_request(req, tl);
			}
			else
#endif
			{
				m_ses.queue_tracker_request(req, shared_from_this());
			}

			ae.updating = true;
			ae.next_announce = now + seconds(20);
			ae.min_announce = now + seconds(10);

			if (m_ses.alerts().should_post<tracker_announce_alert>())
			{
				m_ses.alerts().post_alert(
					tracker_announce_alert(get_handle(), req.url, req.event));
			}

			sent_announce = true;
			if (ae.is_working()
				&& !settings().get_bool(settings_pack::announce_to_all_trackers)
				&& !settings().get_bool(settings_pack::announce_to_all_tiers))
				break;
		}
		update_tracker_timer(now);
	}

	void torrent::scrape_tracker()
	{
		TORRENT_ASSERT(is_single_thread());
		m_last_scrape = m_ses.session_time();

		if (m_trackers.empty()) return;

		int i = m_last_working_tracker;
		if (i == -1) i = 0;
		
		tracker_request req;
		req.apply_ip_filter = m_apply_ip_filter
			&& m_ses.settings().get_bool(settings_pack::apply_ip_filter_to_trackers);
		req.info_hash = m_torrent_file->info_hash();
		req.kind = tracker_request::scrape_request;
		req.url = m_trackers[i].url;
		req.auth = tracker_login();
		req.key = tracker_key();
		m_ses.queue_tracker_request(req, shared_from_this());
	}

	void torrent::tracker_warning(tracker_request const& req, std::string const& msg)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		if (m_ses.alerts().should_post<tracker_warning_alert>())
			m_ses.alerts().post_alert(tracker_warning_alert(get_handle(), req.url, msg));
	}
	
 	void torrent::tracker_scrape_response(tracker_request const& req
 		, int complete, int incomplete, int downloaded, int downloaders)
 	{
		TORRENT_ASSERT(is_single_thread());
 
 		INVARIANT_CHECK;
		TORRENT_ASSERT(req.kind == tracker_request::scrape_request);
 
		announce_entry* ae = find_tracker(req);
		if (ae)
		{
			if (incomplete >= 0) ae->scrape_incomplete = incomplete;
			if (complete >= 0) ae->scrape_complete = complete;
			if (downloaded >= 0) ae->scrape_downloaded = downloaded;

			update_scrape_state();
		}

		if (m_ses.alerts().should_post<scrape_reply_alert>())
		{
			m_ses.alerts().post_alert(scrape_reply_alert(
				get_handle(), incomplete, complete, req.url));
		}
	}

	void torrent::update_scrape_state()
	{
		// loop over all trackers and find the largest numbers for each scrape field
		// then update the torrent-wide understanding of number of downloaders and seeds
		int complete = -1;
		int incomplete = -1;
		int downloaded = -1;
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			complete = (std::max)(i->scrape_complete, complete);
			incomplete = (std::max)(i->scrape_incomplete, incomplete);
			downloaded = (std::max)(i->scrape_downloaded, downloaded);
		}

		if ((complete >= 0 && m_complete != complete)
			|| (incomplete >= 0 && m_incomplete != incomplete)
			|| (downloaded >= 0 && m_downloaded != downloaded))
			state_updated();

		m_complete = complete;
		m_incomplete = incomplete;
		m_downloaded = downloaded;

		update_auto_sequential();

		// these numbers are cached in the resume data
		m_need_save_resume_data = true;
	}
 
	void torrent::tracker_response(
		tracker_request const& r
		, address const& tracker_ip // this is the IP we connected to
		, std::list<address> const& tracker_ips // these are all the IPs it resolved to
		, struct tracker_response const& resp)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;
		TORRENT_ASSERT(r.kind == tracker_request::announce_request);

		if (resp.external_ip != address() && !tracker_ips.empty())
			m_ses.set_external_address(resp.external_ip
				, aux::session_interface::source_tracker
				, *tracker_ips.begin());

		time_point now = aux::time_now();

		int interval = resp.interval;
		if (interval < settings().get_int(settings_pack::min_announce_interval))
			interval = settings().get_int(settings_pack::min_announce_interval);

		announce_entry* ae = find_tracker(r);
		if (ae)
		{
			if (resp.incomplete >= 0) ae->scrape_incomplete = resp.incomplete;
			if (resp.complete >= 0) ae->scrape_complete = resp.complete;
			if (resp.downloaded >= 0) ae->scrape_downloaded = resp.downloaded;
			if (!ae->start_sent && r.event == tracker_request::started)
				ae->start_sent = true;
			if (!ae->complete_sent && r.event == tracker_request::completed)
				ae->complete_sent = true;
			ae->verified = true;
			ae->updating = false;
			ae->fails = 0;
			ae->next_announce = now + seconds(interval);
			ae->min_announce = now + seconds(resp.min_interval);
			int tracker_index = ae - &m_trackers[0];
			m_last_working_tracker = prioritize_tracker(tracker_index);

			if ((!resp.trackerid.empty()) && (ae->trackerid != resp.trackerid))
			{
				ae->trackerid = resp.trackerid;
				if (m_ses.alerts().should_post<trackerid_alert>())
					m_ses.alerts().post_alert(trackerid_alert(get_handle()
						, r.url, resp.trackerid));
			}

			update_scrape_state();
		}
		update_tracker_timer(now);

		if (resp.complete >= 0 && resp.incomplete >= 0)
			m_last_scrape = m_ses.session_time();

#if defined TORRENT_LOGGING
		debug_log("TRACKER RESPONSE\n"
				"interval: %d\n"
				"external ip: %s\n"
				"we connected to: %s\n"
				"peers:"
			, interval
			, print_address(resp.external_ip).c_str()
			, print_address(tracker_ip).c_str());

		for (std::vector<peer_entry>::const_iterator i = resp.peers.begin();
			i != resp.peers.end(); ++i)
		{
			debug_log("  %16s %5d %s %s", i->hostname.c_str(), i->port
				, i->pid.is_all_zeros()?"":to_hex(i->pid.to_string()).c_str()
				, identify_client(i->pid).c_str());
		}
		for (std::vector<ipv4_peer_entry>::const_iterator i = resp.peers4.begin();
			i != resp.peers4.end(); ++i)
		{
			debug_log("  %s:%d", print_address(address_v4(i->ip)).c_str(), i->port);
		}
#if TORRENT_USE_IPV6
		for (std::vector<ipv6_peer_entry>::const_iterator i = resp.peers6.begin();
			i != resp.peers6.end(); ++i)
		{
			debug_log("  [%s]:%d", print_address(address_v6(i->ip)).c_str(), i->port);
		}
#endif
#endif

		// for each of the peers we got from the tracker
		for (std::vector<peer_entry>::const_iterator i = resp.peers.begin();
			i != resp.peers.end(); ++i)
		{
			// don't make connections to ourself
			if (i->pid == m_ses.get_peer_id())
				continue;

#if TORRENT_USE_I2P
			char const* top_domain = strrchr(i->hostname.c_str(), '.');
			if (top_domain && strcmp(top_domain, ".i2p") == 0)
			{
				// this is an i2p name, we need to use the sam connection
				// to do the name lookup
				/*
				m_ses.m_i2p_conn.async_name_lookup(i->ip.c_str()
					, boost::bind(&torrent::on_i2p_resolve
					, shared_from_this(), _1));
				*/
				// it seems like you're not supposed to do a name lookup
				// on the peers returned from the tracker, but just strip
				// the .i2p and use it as a destination
				std::string hostname = i->hostname.substr(i->hostname.size() - 4);
				torrent_state st = get_policy_state();
				need_policy();
				if (m_peer_list->add_i2p_peer(hostname.c_str(), peer_info::tracker, 0, &st))
					state_updated();
				peers_erased(st.erased);
			}
			else
#endif
			{
#if defined TORRENT_ASIO_DEBUGGING
				add_outstanding_async("torrent::on_peer_name_lookup");
#endif
				m_ses.async_resolve(i->hostname, resolver_interface::abort_on_shutdown
					, boost::bind(&torrent::on_peer_name_lookup
						, shared_from_this(), _1, _2, i->port));
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
		for (std::vector<ipv4_peer_entry>::const_iterator i = resp.peers4.begin();
			i != resp.peers4.end(); ++i)
		{
			tcp::endpoint a(address_v4(i->ip), i->port);
			need_update |= bool(add_peer(a, peer_info::tracker));
		}

#if TORRENT_USE_IPV6
		for (std::vector<ipv6_peer_entry>::const_iterator i = resp.peers6.begin();
			i != resp.peers6.end(); ++i)
		{
			tcp::endpoint a(address_v6(i->ip), i->port);
			need_update |= bool(add_peer(a, peer_info::tracker));
		}
#endif
		if (need_update) state_updated();

		update_want_peers();

		if (m_ses.alerts().should_post<tracker_reply_alert>())
		{
			m_ses.alerts().post_alert(tracker_reply_alert(
				get_handle(), resp.peers.size() + resp.peers4.size()
#if TORRENT_USE_IPV6
				+ resp.peers6.size()
#endif
				, r.url));
		}
		m_got_tracker_response = true;

		// we're listening on an interface type that was not used
		// when talking to the tracker. If there is a matching interface
		// type in the tracker IP list, make another tracker request
		// using that interface
		// in order to avoid triggering this case over and over, don't
		// do it if the bind IP for the tracker request that just completed
		// matches one of the listen interfaces, since that means this
		// announce was the second one
		// don't connect twice just to tell it we're stopping

		if (((!is_any(m_ses.get_ipv6_interface().address()) && tracker_ip.is_v4())
			|| (!is_any(m_ses.get_ipv4_interface().address()) && tracker_ip.is_v6()))
			&& r.bind_ip != m_ses.get_ipv4_interface().address()
			&& r.bind_ip != m_ses.get_ipv6_interface().address()
			&& r.event != tracker_request::stopped)
		{
			std::list<address>::const_iterator i = std::find_if(tracker_ips.begin()
				, tracker_ips.end(), boost::bind(&address::is_v4, _1) != tracker_ip.is_v4());
			if (i != tracker_ips.end())
			{
				// the tracker did resolve to a different type of address, so announce
				// to that as well

				// tell the tracker to bind to the opposite protocol type
				address bind_interface = tracker_ip.is_v4()
					?m_ses.get_ipv6_interface().address()
					:m_ses.get_ipv4_interface().address();
				announce_with_tracker(r.event, bind_interface);
#if defined TORRENT_LOGGING
				debug_log("announce again using %s as the bind interface"
					, print_address(bind_interface).c_str());
#endif
			}
		}

		do_connect_boost();

		state_updated();
	}

	void torrent::update_auto_sequential()
	{
		if (!m_ses.settings().get_bool(settings_pack::auto_sequential))
		{
			m_auto_sequential = false;
			return;
		}

		if (int(m_connections.size()) - m_num_connecting < 10)
		{
			// there are too few peers. Be conservative and don't assume it's
			// well seeded until we can connect to more peers
			m_auto_sequential = false;
			return;
		}

		// if there are at least 10 seeds, and there are 10 times more
		// seeds than downloaders, enter sequential download mode
		// (for performance)
		int downloaders = num_downloaders();
		int seeds = num_seeds();
		m_auto_sequential = downloaders * 10 <= seeds
			&& seeds > 9;
	}

	void torrent::do_connect_boost()
	{
		if (!m_need_connect_boost) return;

		// this is the first tracker response for this torrent
		// instead of waiting one second for session_impl::on_tick()
		// to be called, connect to a few peers immediately
		int conns = (std::min)(
			m_ses.settings().get_int(settings_pack::torrent_connect_boost)
			, m_ses.settings().get_int(settings_pack::connections_limit) - m_ses.num_connections());

		if (conns > 0) m_need_connect_boost = false;

		// if we don't know of any peers
		if (!m_peer_list) return;

		while (want_peers() && conns > 0)
		{
			--conns;
			torrent_state st = get_policy_state();
			torrent_peer* p = m_peer_list->connect_one_peer(m_ses.session_time(), &st);
			peers_erased(st.erased);
			inc_stats_counter(counters::connection_attempt_loops, st.loop_counter);
			if (p == NULL)
			{
				update_want_peers();
				continue;
			}

#if defined TORRENT_LOGGING
			external_ip const& external = m_ses.external_address();
			debug_log(" *** FOUND CONNECTION CANDIDATE ["
				" ip: %s rank: %u external: %s t: %d ]"
				, print_endpoint(p->ip()).c_str()
				, p->rank(external, m_ses.listen_port())
				, print_address(external.external_address(p->address())).c_str()
				, m_ses.session_time() - p->last_connected);
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

	time_point torrent::next_announce() const
	{
		return m_waiting_tracker?m_tracker_timer.expires_at():min_time();
	}

	void torrent::force_tracker_request(time_point t, int tracker_idx)
	{
		if (is_paused()) return;
		if (tracker_idx == -1)
		{
			for (std::vector<announce_entry>::iterator i = m_trackers.begin()
				, end(m_trackers.end()); i != end; ++i)
				i->next_announce = (std::max)(t, i->min_announce) + seconds(1);
		}
		else
		{
			TORRENT_ASSERT(tracker_idx >= 0 && tracker_idx < int(m_trackers.size()));
			if (tracker_idx < 0 || tracker_idx >= int(m_trackers.size()))
				return;
			announce_entry& e = m_trackers[tracker_idx];
			e.next_announce = (std::max)(t, e.min_announce) + seconds(1);
		}
		update_tracker_timer(clock_type::now());
	}

	void torrent::set_tracker_login(
		std::string const& name
		, std::string const& pw)
	{
		m_username = name;
		m_password = pw;
	}

#if TORRENT_USE_I2P
	void torrent::on_i2p_resolve(error_code const& ec, char const* dest)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

#if defined TORRENT_LOGGING
		if (ec)
			debug_log("i2p_resolve error: %s", ec.message().c_str());
#endif
		if (ec || m_ses.is_aborted()) return;

		need_policy();
		torrent_state st = get_policy_state();
		if (m_peer_list->add_i2p_peer(dest, peer_info::tracker, 0, &st))
			state_updated();
		peers_erased(st.erased);
	}
#endif

	void torrent::on_peer_name_lookup(error_code const& e
		, std::vector<address> const& host_list, int port)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

#if defined TORRENT_ASIO_DEBUGGING
		complete_async("torrent::on_peer_name_lookup");
#endif

#if defined TORRENT_LOGGING
		if (e)
			debug_log("peer name lookup error: %s", e.message().c_str());
#endif

		if (e || host_list.empty() || m_ses.is_aborted()) return;

		// TODO: add one peer per IP the hostname resolves to
		tcp::endpoint host(host_list.front(), port);

		if (m_apply_ip_filter
			&& m_ses.get_ip_filter().access(host.address()) & ip_filter::blocked)
		{
#if defined TORRENT_LOGGING
			error_code ec;
			debug_log("blocked ip from tracker: %s", host.address().to_string(ec).c_str());
#endif
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().post_alert(peer_blocked_alert(get_handle()
					, host.address(), peer_blocked_alert::ip_filter));
			return;
		}

		if (add_peer(host, peer_info::tracker))
			state_updated();
		update_want_peers();
	}

	boost::int64_t torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return -1;
		return m_torrent_file->total_size()
			- quantized_bytes_done();
	}

	boost::int64_t torrent::quantized_bytes_done() const
	{
//		INVARIANT_CHECK;

		if (!valid_metadata()) return 0;

		if (m_torrent_file->num_pieces() == 0)
			return 0;

		if (!has_picker()) return m_have_all ? m_torrent_file->total_size() : 0;

		// if any piece hash fails, we'll be taken out of seed mode
		// and m_seed_mode will be false
		if (m_seed_mode) return m_torrent_file->total_size();

		const int last_piece = m_torrent_file->num_pieces() - 1;

		boost::int64_t total_done
			= boost::uint64_t(m_picker->num_passed()) * m_torrent_file->piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_picker->has_piece_passed(last_piece))
		{
			int corr = m_torrent_file->piece_size(last_piece)
				- m_torrent_file->piece_length();
			total_done += corr;
		}
		return total_done;
	}

	// returns the number of bytes we are interested
	// in for the given block. This returns block_size()
	// for all blocks except the last one (if it's smaller
	// than block_size()) and blocks that overlap a padding
	// file
	int torrent::block_bytes_wanted(piece_block const& p) const
	{
		file_storage const& fs = m_torrent_file->files();
		int piece_size = m_torrent_file->piece_size(p.piece_index);
		int offset = p.block_index * block_size();
		if (m_padding == 0) return (std::min)(piece_size - offset, int(block_size()));

		std::vector<file_slice> files = fs.map_block(
			p.piece_index, offset, (std::min)(piece_size - offset, int(block_size())));
		int ret = 0;
		for (std::vector<file_slice>::iterator i = files.begin()
			, end(files.end()); i != end; ++i)
		{
			if (fs.pad_file_at(i->file_index)) continue;
			ret += i->size;
		}
		TORRENT_ASSERT(ret <= (std::min)(piece_size - offset, int(block_size())));
		return ret;
	}

	// fills in total_wanted, total_wanted_done and total_done
	void torrent::bytes_done(torrent_status& st, bool accurate) const
	{
		INVARIANT_CHECK;

		st.total_done = 0;
		st.total_wanted_done = 0;
		st.total_wanted = m_torrent_file->total_size();

		TORRENT_ASSERT(st.total_wanted >= m_padding);
		TORRENT_ASSERT(st.total_wanted >= 0);

		if (!valid_metadata() || m_torrent_file->num_pieces() == 0)
			return;

		TORRENT_ASSERT(st.total_wanted >= boost::int64_t(m_torrent_file->piece_length())
			* (m_torrent_file->num_pieces() - 1));

		const int last_piece = m_torrent_file->num_pieces() - 1;
		const int piece_size = m_torrent_file->piece_length();

		// if any piece hash fails, we'll be taken out of seed mode
		// and m_seed_mode will be false
		if (m_seed_mode || is_seed())
		{
			st.total_done = m_torrent_file->total_size() - m_padding;
			st.total_wanted_done = st.total_done;
			st.total_wanted = st.total_done;
			return;
		}
		else if (!has_picker())
		{
			st.total_done = 0;
			st.total_wanted_done = 0;
			st.total_wanted = m_torrent_file->total_size() - m_padding;
			return;
		}

		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		st.total_wanted_done = boost::int64_t(num_passed() - m_picker->num_have_filtered())
			* piece_size;
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		
		st.total_done = boost::int64_t(num_passed()) * piece_size;
		// if num_passed() == num_pieces(), we should be a seed, and taken the
		// branch above
		TORRENT_ASSERT(num_passed() <= m_torrent_file->num_pieces());

		int num_filtered_pieces = m_picker->num_filtered()
			+ m_picker->num_have_filtered();
		int last_piece_index = m_torrent_file->num_pieces() - 1;
		if (m_picker->piece_priority(last_piece_index) == 0)
		{
			st.total_wanted -= m_torrent_file->piece_size(last_piece_index);
			TORRENT_ASSERT(st.total_wanted >= 0);
			--num_filtered_pieces;
		}
		st.total_wanted -= boost::int64_t(num_filtered_pieces) * piece_size;
		TORRENT_ASSERT(st.total_wanted >= 0);
	
		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_picker->has_piece_passed(last_piece))
		{
			TORRENT_ASSERT(st.total_done >= piece_size);
			int corr = m_torrent_file->piece_size(last_piece)
				- piece_size;
			TORRENT_ASSERT(corr <= 0);
			TORRENT_ASSERT(corr > -piece_size);
			st.total_done += corr;
			if (m_picker->piece_priority(last_piece) != 0)
			{
				TORRENT_ASSERT(st.total_wanted_done >= piece_size);
				st.total_wanted_done += corr;
			}
		}
		TORRENT_ASSERT(st.total_wanted >= st.total_wanted_done);

		// this is expensive, we might not want to do it all the time
		if (!accurate) return;

		// subtract padding files
		if (m_padding > 0)
		{
			// this is a bit unfortunate
			// (both the const cast and the requirement to load the torrent)
			if (!const_cast<torrent*>(this)->need_loaded()) return;

			file_storage const& files = m_torrent_file->files();
			for (int i = 0; i < files.num_files(); ++i)
			{
				if (!files.pad_file_at(i)) continue;
				peer_request p = files.map_file(i, 0, files.file_size(i));
				for (int j = p.piece; p.length > 0; ++j)
				{
					int deduction = (std::min)(p.length, piece_size - p.start);
					bool done = m_picker->has_piece_passed(j);
					bool wanted = m_picker->piece_priority(j) > 0;
					if (done) st.total_done -= deduction;
					if (wanted) st.total_wanted -= deduction;
					if (wanted && done) st.total_wanted_done -= deduction;
					TORRENT_ASSERT(st.total_done >= 0);
					TORRENT_ASSERT(st.total_wanted >= 0);
					TORRENT_ASSERT(st.total_wanted_done >= 0);
					p.length -= piece_size - p.start;
					p.start = 0;
					++p.piece;
				}
			}
		}

		TORRENT_ASSERT(!accurate || st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		std::vector<piece_picker::downloading_piece> dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = (piece_size + block_size() - 1) / block_size();

		// look at all unfinished pieces and add the completed
		// blocks to our 'done' counter
		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			// completed pieces are already accounted for
			if (m_picker->has_piece_passed(index)) continue;
			TORRENT_ASSERT(i->finished <= m_picker->blocks_in_piece(index));

#if TORRENT_USE_ASSERTS
			for (std::vector<piece_picker::downloading_piece>::const_iterator j = boost::next(i);
				j != dl_queue.end(); ++j)
			{
				TORRENT_ASSERT(j->index != index);
			}
#endif

			piece_picker::block_info* info = m_picker->blocks_for_piece(*i);
			for (int j = 0; j < blocks_per_piece; ++j)
			{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
				TORRENT_ASSERT(m_picker->is_finished(piece_block(index, j))
					== (info[j].state == piece_picker::block_info::state_finished));
#endif
				if (info[j].state == piece_picker::block_info::state_finished)
				{
					corr += block_bytes_wanted(piece_block(index, j));
				}
				TORRENT_ASSERT(corr >= 0);
				TORRENT_ASSERT(index != last_piece || j < m_picker->blocks_in_last_piece()
					|| info[j].state != piece_picker::block_info::state_finished);
			}

			st.total_done += corr;
			if (m_picker->piece_priority(index) > 0)
				st.total_wanted_done += corr;
		}

		TORRENT_ASSERT(st.total_wanted <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection* pc = *i;
			boost::optional<piece_block_progress> p
				= pc->downloading_piece_progress();
			if (!p) continue;

			if (m_picker->has_piece_passed(p->piece_index))
				continue;

			piece_block block(p->piece_index, p->block_index);
			if (m_picker->is_finished(block))
				continue;

			std::map<piece_block, int>::iterator dp
				= downloading_piece.find(block);
			if (dp != downloading_piece.end())
			{
				if (dp->second < p->bytes_downloaded)
					dp->second = p->bytes_downloaded;
			}
			else
			{
				downloading_piece[block] = p->bytes_downloaded;
			}
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(p->bytes_downloaded <= p->full_block_bytes);
			TORRENT_ASSERT(p->full_block_bytes == to_req(piece_block(
				p->piece_index, p->block_index)).length);
#endif
		}
		for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
			i != downloading_piece.end(); ++i)
		{
			int done = (std::min)(block_bytes_wanted(i->first), i->second);
			st.total_done += done;
			if (m_picker->piece_priority(i->first.piece_index) != 0)
				st.total_wanted_done += done;
		}

		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size() - m_padding);

#ifdef TORRENT_DEBUG

		if (st.total_done >= m_torrent_file->total_size())
		{
			// Thist happens when a piece has been downloaded completely
			// but not yet verified against the hash
			fprintf(stderr, "num_have: %d\nunfinished:\n", num_have());
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				fprintf(stderr, "  %d ", i->index);
				piece_picker::block_info* info = m_picker->blocks_for_piece(*i);
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					char const* state = info[j].state
						== piece_picker::block_info::state_finished ? "1" : "0";
					fputs(state, stderr);
				}
				fputs("\n", stderr);
			}
			
			fputs("downloading pieces:\n", stderr);

			for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
				i != downloading_piece.end(); ++i)
			{
				fprintf(stderr, "   %d:%d  %d\n", int(i->first.piece_index), int(i->first.block_index), i->second);
			}

		}

		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size());

#endif

		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);
	}

	void torrent::on_piece_verified(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());

		torrent_ref_holder h(this, "verify_piece");

		dec_refcount("verify_piece");

		int ret = j->ret;
		if (m_ses.settings().get_bool(settings_pack::disable_hash_checks))
		{
			ret = 0;
		}
		else if (ret == -1)
		{
			handle_disk_error(j);
		}
		// we're using the piece hashes here, we need the torrent to be loaded
		else if (need_loaded())
		{
			if (sha1_hash(j->d.piece_hash) != m_torrent_file->hash_for_piece(j->piece))
				ret = -2;
		}
		else
		{
			// failing to load the .torrent file counts as disk failure
			ret = -1;
		}

		// 0: success, piece passed check
		// -1: disk failure
		// -2: piece failed check

#if defined TORRENT_LOGGING
		debug_log("*** PIECE_FINISHED [ p: %d | chk: %s | size: %d ]"
			, j->piece, ((ret == 0)
				?"passed":ret == -1
				?"disk failed":"failed")
			, m_torrent_file->piece_size(j->piece));
#endif
		TORRENT_ASSERT(valid_metadata());

		// if we're a seed we don't have a picker
		// and we also don't have to do anything because
		// we already have this piece
		if (!has_picker() && m_have_all) return;

		need_picker();

		TORRENT_ASSERT(!m_picker->have_piece(j->piece));

//		picker().mark_as_done_checking(j->piece);

		state_updated();

		// even though the piece passed the hash-check
		// it might still have failed being written to disk
		// if so, piece_picker::write_failed() has been
		// called, and the piece is no longer finished.
		// in this case, we have to ignore the fact that
		// it passed the check
		if (!m_picker->is_piece_finished(j->piece)) return;

		if (ret == 0)
		{
			// the following call may cause picker to become invalid
			// in case we just became a seed
			piece_passed(j->piece);
			// if we're in seed mode, we just acquired this piece
			// mark it as verified
			if (m_seed_mode) verified(j->piece);
		}
		else if (ret == -2)
		{
			// piece_failed() will restore the piece
			piece_failed(j->piece);
		}
		else
		{
			TORRENT_ASSERT(ret == -1);
			update_gauge();
		}

	}

	void torrent::update_sparse_piece_prio(int i, int start, int end)
	{
		TORRENT_ASSERT(m_picker);
		if (m_picker->have_piece(i) || m_picker->piece_priority(i) == 0)
			return;
		bool have_before = i == 0 || m_picker->have_piece(i - 1);
		bool have_after = i == end - 1 || m_picker->have_piece(i + 1);
		if (have_after && have_before)
			m_picker->set_piece_priority(i, 7);
		else if (have_after || have_before)
			m_picker->set_piece_priority(i, 6);
		update_gauge();
	}

	// this is called once we have completely downloaded piece
	// 'index', its hash has been verified. It's also called
	// during initial file check when we find a piece whose hash
	// is correct
	void torrent::we_have(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!has_picker() || m_picker->has_piece_passed(index));

		inc_stats_counter(counters::num_have_pieces);

		// at this point, we have the piece for sure. It has been
		// successfully written to disk. We may announce it to peers
		// (unless it has already been announced through predictive_piece_announce
		// feature).
		bool announce_piece = true;
		std::vector<int>::iterator i = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (i != m_predictive_pieces.end() && *i == index)
		{
			// this means we've already announced the piece
			announce_piece = false;
			m_predictive_pieces.erase(i);
		}

		// make a copy of the peer list since peers
		// may disconnect while looping
		std::vector<peer_connection*> peers = m_connections;

		for (peer_iterator i = peers.begin(); i != peers.end(); ++i)
		{
			boost::shared_ptr<peer_connection> p = (*i)->self();

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

		if (settings().get_int(settings_pack::max_sparse_regions) > 0
			&& has_picker()
			&& m_picker->sparse_regions() > settings().get_int(settings_pack::max_sparse_regions))
		{
			// we have too many sparse regions. Prioritize pieces
			// that won't introduce new sparse regions
			// prioritize pieces that will reduce the number of sparse
			// regions even higher
			int start = m_picker->cursor();
			int end = m_picker->reverse_cursor();
			if (index > start) update_sparse_piece_prio(index - 1, start, end);
			if (index < end - 1) update_sparse_piece_prio(index + 1, start, end);
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_piece_pass(index);
			} TORRENT_CATCH (std::exception&) {}
		}
#endif

		// since this piece just passed, we might have
		// become uninterested in some peers where this
		// was the last piece we were interested in
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			// if we're not interested already, no need to check
			if (!p->is_interesting()) continue;
			// if the peer doesn't have the piece we just got, it
			// shouldn't affect our interest
			if (!p->has_piece(index)) continue;
			p->update_interest();
		}

		if (settings().get_int(settings_pack::suggest_mode) == settings_pack::suggest_read_cache)
		{
			// we just got a new piece. Chances are that it's actually the
			// rarest piece (since we're likely to download pieces rarest first)
			// if it's rarer than any other piece that we currently suggest, insert
			// it in the suggest set and pop the last one out
			add_suggest_piece(index);
		}

		m_need_save_resume_data = true;
		state_updated();

		if (m_ses.alerts().should_post<piece_finished_alert>())
			m_ses.alerts().post_alert(piece_finished_alert(get_handle(), index));

		// update m_file_progress (if we have one)
		if (!m_file_progress.empty())
		{
			const int piece_size = m_torrent_file->piece_length();
			boost::int64_t off = boost::int64_t(index) * piece_size;
			int file_index = m_torrent_file->files().file_index_at_offset(off);
			int size = m_torrent_file->piece_size(index);
			file_storage const& fs = m_torrent_file->files();
			for (; size > 0; ++file_index)
			{
				boost::int64_t file_offset = off - fs.file_offset(file_index);
				TORRENT_ASSERT(file_index != fs.num_files());
				TORRENT_ASSERT(file_offset <= fs.file_size(file_index));
				int add = (std::min)(fs.file_size(file_index) - file_offset, (boost::int64_t)size);
				m_file_progress[file_index] += add;

				TORRENT_ASSERT(m_file_progress[file_index]
						<= m_torrent_file->files().file_size(file_index));

				if (m_file_progress[file_index] >= m_torrent_file->files().file_size(file_index))
				{
					if (!m_torrent_file->files().pad_file_at(file_index))
					{
						if (m_ses.alerts().should_post<file_completed_alert>())
						{
							// this file just completed, post alert
							m_ses.alerts().post_alert(file_completed_alert(get_handle()
										, file_index));
						}
					}
				}
				size -= add;
				off += add;
				TORRENT_ASSERT(size >= 0);
			}
		}

		remove_time_critical_piece(index, true);

		if (is_finished()
			&& m_state != torrent_status::finished
			&& m_state != torrent_status::seeding)
		{
			// torrent finished
			// i.e. all the pieces we're interested in have
			// been downloaded. Release the files (they will open
			// in read only mode if needed)
			finished();
			// if we just became a seed, picker is now invalid, since it
			// is deallocated by the torrent once it starts seeding
		}

		m_last_download = m_ses.session_time();

		if (m_share_mode)
			recalc_share_mode();
	}

	// this is called when the piece hash is checked as correct. Note
	// that the piece picker and the torrent won't necessarily consider
	// us to have this piece yet, since it might not have been flushed
	// to disk yet. Only if we have predictive_piece_announce on will
	// we announce this piece to peers at this point.
	void torrent::piece_passed(int index)
	{
//		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_picker->has_piece_passed(index));

#if defined TORRENT_LOGGING
		debug_log("PIECE_PASSED (%d)", num_passed());
#endif

//		fprintf(stderr, "torrent::piece_passed piece:%d\n", index);

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		m_need_save_resume_data = true;

		inc_stats_counter(counters::num_piece_passed);

		remove_time_critical_piece(index, true);

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<void*> peers;

		// these torrent_peer pointers are owned by m_peer_list and they may be
		// invalidated if a peer disconnects. We cannot keep them across any
		// significant operations, but we should use them right away
		// ignore NULL pointers
		std::remove_copy(downloaders.begin(), downloaders.end()
			, std::inserter(peers, peers.begin()), (torrent_peer*)0);

		for (std::set<void*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			torrent_peer* p = static_cast<torrent_peer*>(*i);
			TORRENT_ASSERT(p != 0);
			if (p == 0) continue;
			TORRENT_ASSERT(p->in_use);
			p->on_parole = false;
			int trust_points = p->trust_points;
			++trust_points;
			if (trust_points > 8) trust_points = 8;
			p->trust_points = trust_points;
			if (p->connection)
			{
				peer_connection* peer = static_cast<peer_connection*>(p->connection);
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
			m_ses.disk_thread().async_flush_piece(m_storage.get(), index);
		m_picker->piece_passed(index);
		update_gauge();
		we_have(index);
	}

	// we believe we will complete this piece very soon
	// announce it to peers ahead of time to eliminate the
	// round-trip times involved in announcing it, requesting it
	// and sending it
	void torrent::predicted_have_piece(int index, int milliseconds)
	{
		std::vector<int>::iterator i = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (i != m_predictive_pieces.end() && *i == index) return;
		
		for (peer_iterator p = m_connections.begin()
			, end(m_connections.end()); p != end; ++p)
		{
#if defined TORRENT_LOGGING
			(*p)->peer_log(">>> PREDICTIVE_HAVE [ piece: %d expected in %d ms]"
				, index, milliseconds);
#endif
			(*p)->announce_piece(index);
		}

		m_predictive_pieces.insert(i, index);
	}

	void torrent::piece_failed(int index)
	{
		// if the last piece fails the peer connection will still
		// think that it has received all of it until this function
		// resets the download queue. So, we cannot do the
		// invariant check here since it assumes:
		// (total_done == m_torrent_file->total_size()) => is_seed()
		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
	  	TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		inc_stats_counter(counters::num_piece_failed);

		if (m_ses.alerts().should_post<hash_failed_alert>())
			m_ses.alerts().post_alert(hash_failed_alert(get_handle(), index));

		std::vector<int>::iterator i = std::lower_bound(m_predictive_pieces.begin()
			, m_predictive_pieces.end(), index);
		if (i != m_predictive_pieces.end() && *i == index)
		{
			for (peer_iterator p = m_connections.begin()
				, end(m_connections.end()); p != end; ++p)
			{
				// send reject messages for
				// potential outstanding requests to this piece
				(*p)->reject_piece(index);
				// let peers that support the dont-have message
				// know that we don't actually have this piece
				(*p)->write_dont_have(index);
			}
			m_predictive_pieces.erase(i);
		}
		// increase the total amount of failed bytes
		add_failed_bytes(m_torrent_file->piece_size(index));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_piece_failed(index);
			} TORRENT_CATCH (std::exception&) {}
		}
#endif

		std::vector<void*> downloaders;
		if (m_picker)
			m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#ifdef TORRENT_DEBUG
		for (std::vector<void*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i)
		{
			torrent_peer* p = (torrent_peer*)*i;
			if (p && p->connection)
			{
				peer_connection* peer = static_cast<peer_connection*>(p->connection);
				peer->piece_failed = true;
			}
		}
#endif

		// did we receive this piece from a single peer?
		bool single_peer = peers.size() == 1;

		for (std::set<void*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			torrent_peer* p = static_cast<torrent_peer*>(*i);
			if (p == 0) continue;
			TORRENT_ASSERT(p->in_use);
			bool allow_disconnect = true;
			if (p->connection)
			{
				peer_connection* peer = static_cast<peer_connection*>(p->connection);
				TORRENT_ASSERT(peer->m_in_use == 1337);

				// the peer implementation can ask not to be disconnected.
				// this is used for web seeds for instance, to instead of
				// disconnecting, mark the file as not being haved.
				allow_disconnect = peer->received_invalid_data(index, single_peer);
			}

			if (m_ses.settings().get_bool(settings_pack::use_parole_mode))
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
			p->hashfails = hashfails;

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
					peer_id pid(0);
					if (p->connection) pid = p->connection->pid();
					m_ses.alerts().post_alert(peer_ban_alert(
						get_handle(), p->ip(), pid));
				}

				// mark the peer as banned
				ban_peer(p);
				update_want_peers();
				inc_stats_counter(counters::banned_for_hash_failure);

				if (p->connection)
				{
					peer_connection* peer = static_cast<peer_connection*>(p->connection);
#if defined TORRENT_LOGGING
					debug_log("*** BANNING PEER: \"%s\" Too many corrupt pieces"
						, print_endpoint(p->ip()).c_str());
#endif
#if defined TORRENT_LOGGING
					peer->peer_log("*** BANNING PEER: Too many corrupt pieces");
#endif
					peer->disconnect(errors::too_many_corrupt_pieces, op_bittorrent);
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
			m_ses.disk_thread().async_clear_piece(m_storage.get(), index
				, boost::bind(&torrent::on_piece_sync, shared_from_this(), _1));
		}
		else
		{
			TORRENT_ASSERT(m_abort);
			// it doesn't really matter what we do
			// here, since we're about to destruct the
			// torrent anyway.
			disk_io_job j;
			j.piece = index;
			on_piece_sync(&j);
		}

#ifdef TORRENT_DEBUG
		for (std::vector<void*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i)
		{
			torrent_peer* p = (torrent_peer*)*i;
			if (p && p->connection)
			{
				peer_connection* peer = static_cast<peer_connection*>(p->connection);
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

	void torrent::on_piece_sync(disk_io_job const* j)
	{
		// the user may have called force_recheck, which clears
		// the piece picker
		if (!has_picker()) return;

		// unlock the piece and restore it, as if no block was
		// ever downloaded for it.
		m_picker->restore_piece(j->piece);

		// we have to let the piece_picker know that
		// this piece failed the check as it can restore it
		// and mark it as being interesting for download
		TORRENT_ASSERT(m_picker->have_piece(j->piece) == false);

		// loop over all peers and re-request potential duplicate
		// blocks to this piece
		for (std::vector<peer_connection*>::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			peer_connection* p = *i;
			std::vector<pending_block> const& dq = p->download_queue();
			std::vector<pending_block> const& rq = p->request_queue();
			for (std::vector<pending_block>::const_iterator k = dq.begin()
				, end(dq.end()); k != end; ++k)
			{
				if (k->timed_out || k->not_wanted) continue;
				if (int(k->block.piece_index) != j->piece) continue;
				m_picker->mark_as_downloading(k->block, p->peer_info_struct()
					, p->picker_options());
			}
			for (std::vector<pending_block>::const_iterator k = rq.begin()
				, end(rq.end()); k != end; ++k)
			{
				if (int(k->block.piece_index) != j->piece) continue;
				m_picker->mark_as_downloading(k->block, p->peer_info_struct()
					, p->picker_options());
			}
		}
	}

	void torrent::peer_has(int index, peer_connection const* peer)
	{
		if (has_picker())
		{
			m_picker->inc_refcount(index, peer);
			update_suggest_piece(index, 1);
		}
#ifdef TORRENT_DEBUG
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
#endif
	}
		
	// when we get a bitfield message, this is called for that piece
	void torrent::peer_has(bitfield const& bits, peer_connection const* peer)
	{
		if (has_picker())
		{
			m_picker->inc_refcount(bits, peer);
			refresh_suggest_pieces();
		}
#ifdef TORRENT_DEBUG
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
#endif
	}

	void torrent::peer_has_all(peer_connection const* peer)
	{
		if (has_picker())
		{
			m_picker->inc_refcount_all(peer);
		}
#ifdef TORRENT_DEBUG
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
#endif
	}

	void torrent::peer_lost(bitfield const& bits, peer_connection const* peer)
	{
		if (has_picker())
		{
			m_picker->dec_refcount(bits, peer);
			// TODO: update suggest_piece?
		}
#ifdef TORRENT_DEBUG
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
#endif
	}

	void torrent::peer_lost(int index, peer_connection const* peer)
	{
		if (m_picker.get())
		{
			m_picker->dec_refcount(index, peer);
			update_suggest_piece(index, -1);
		}
#ifdef TORRENT_DEBUG
		else
		{
			TORRENT_ASSERT(is_seed() || !m_have_all);
		}
#endif
	}

	void torrent::add_suggest_piece(int index)
	{
		// it would be nice if we would keep track of piece
		// availability even when we're a seed, for
		// the suggest piece feature
		if (!has_picker()) return;

		int num_peers = m_picker->get_availability(index);

		TORRENT_ASSERT(has_piece_passed(index));

		// in order to avoid unnecessary churn in the suggested pieces
		// the new piece has to beat the existing piece by at least one
		// peer in availability.
		// m_suggested_pieces is sorted by rarity, the last element
		// should have the most peers (num_peers).
		if (m_suggested_pieces.empty()
			|| num_peers < m_suggested_pieces[m_suggested_pieces.size()-1].num_peers - 1)
		{
			suggest_piece_t p;
			p.piece_index = index;
			p.num_peers = num_peers;

			typedef std::vector<suggest_piece_t>::iterator iter;
			
			std::pair<iter, iter> range = std::equal_range(
				m_suggested_pieces.begin(), m_suggested_pieces.end(), p);

			// make sure this piece isn't already in the suggested set.
			// if it is, just ignore it
			iter i = std::find_if(range.first, range.second
				, boost::bind(&suggest_piece_t::piece_index, _1) == index);
			if (i != range.second) return;

			m_suggested_pieces.insert(range.second, p);
			if (m_suggested_pieces.size() > 0)
				m_suggested_pieces.pop_back();

			// tell all peers about this new suggested piece
			for (peer_iterator p = m_connections.begin()
				, end(m_connections.end()); p != end; ++p)
			{
				(*p)->send_suggest(index);
			}

			refresh_suggest_pieces();
		}
	}

	void torrent::update_suggest_piece(int index, int change)
	{
		for (std::vector<suggest_piece_t>::iterator i = m_suggested_pieces.begin()
			, end(m_suggested_pieces.end()); i != end; ++i)
		{
			if (i->piece_index != index) continue;

			i->num_peers += change;
			if (change > 0)
				std::stable_sort(i, end);
			else if (change < 0)
				std::stable_sort(m_suggested_pieces.begin(), i + 1);
		}

		if (!m_suggested_pieces.empty() && m_suggested_pieces[0].num_peers > m_connections.size() * 2 / 3)
		{
			// the rarest piece we have in the suggest set is not very
			// rare anymore. at least 2/3 of the peers has it now. Refresh
			refresh_suggest_pieces();
		}
	}

	void torrent::refresh_suggest_pieces()
	{
		m_need_suggest_pieces_refresh = true;
	}

	void torrent::do_refresh_suggest_pieces()
	{
		m_need_suggest_pieces_refresh = false;

		if (settings().get_int(settings_pack::suggest_mode)
			== settings_pack::no_piece_suggestions)
			return;

		if (!valid_metadata()) return;

		boost::shared_ptr<torrent> t = shared_from_this();
		TORRENT_ASSERT(t);
		cache_status cs;
		m_ses.disk_thread().get_cache_info(&cs, m_storage.get() == NULL, m_storage.get());

		// remove write cache entries
		cs.pieces.erase(std::remove_if(cs.pieces.begin(), cs.pieces.end()
			, boost::bind(&cached_piece_info::kind, _1) == cached_piece_info::write_cache)
			, cs.pieces.end());

		std::vector<suggest_piece_t>& pieces = m_suggested_pieces;
		pieces.clear();
		pieces.reserve(cs.pieces.size());

		// sort in ascending order, to get most recently used first
		std::sort(cs.pieces.begin(), cs.pieces.end()
			, boost::bind(&cached_piece_info::last_use, _1)
			> boost::bind(&cached_piece_info::last_use, _2));

		for (std::vector<cached_piece_info>::iterator i = cs.pieces.begin()
			, end(cs.pieces.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->storage == m_storage.get());
			if (!has_piece_passed(i->piece)) continue;
			suggest_piece_t p;
			p.piece_index = i->piece;
			if (has_picker())
			{
				p.num_peers = m_picker->get_availability(i->piece);
			}
			else
			{
				// TODO: really, we should just keep the picker around
				// in this case to maintain the availability counters
				p.num_peers = 0;
				for (const_peer_iterator i = m_connections.begin()
					, end(m_connections.end()); i != end; ++i)
				{
					peer_connection* peer = *i;
					if (peer->has_piece(p.piece_index)) ++p.num_peers;
				}
			}
			pieces.push_back(p);
		}

		// sort by rarity (stable, to maintain sort
		// by last use)
		std::stable_sort(pieces.begin(), pieces.end());

		// only suggest half of the pieces
		pieces.resize(pieces.size() / 2);

		// send new suggests to peers
		// the peers will filter out pieces we've
		// already suggested to them
		for (std::vector<suggest_piece_t>::iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i)
		{
			for (peer_iterator p = m_connections.begin();
				p != m_connections.end(); ++p)
				(*p)->send_suggest(i->piece_index);
		}
	}

	void torrent::abort()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_abort) return;

		m_abort = true;
		update_want_peers();
		update_want_tick();
		update_gauge();

		// if the torrent is paused, it doesn't need
		// to announce with even=stopped again.
		if (!is_paused())
			stop_announcing();

		error_code ec;
		m_inactivity_timer.cancel(ec);

#if defined TORRENT_LOGGING
		log_to_all_peers("ABORTING TORRENT");
#endif

		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all(errors::torrent_aborted, op_bittorrent);

		// post a message to the main thread to destruct
		// the torrent object from there
		if (m_storage.get())
		{
			inc_refcount("release_files");
			m_ses.disk_thread().async_stop_torrent(m_storage.get()
				, boost::bind(&torrent::on_cache_flushed, shared_from_this(), _1));
		}
		else
		{
			TORRENT_ASSERT(m_abort);
			if (alerts().should_post<cache_flushed_alert>())
				alerts().post_alert(cache_flushed_alert(get_handle()));
		}
		
		m_storage.reset();

		// TODO: 2 abort lookups this torrent has made via the
		// session host resolver interface

		if (!m_apply_ip_filter)
		{
			inc_stats_counter(counters::non_filter_torrents, -1);
			m_apply_ip_filter = true;
		}

		m_allow_peers = false;
		m_auto_managed = false;
		for (int i = 0; i < aux::session_interface::num_torrent_lists; ++i)
		{
			if (!m_links[i].in_list()) continue;
			m_links[i].unlink(m_ses.torrent_list(i), i);
		}
		// don't re-add this torrent to the state-update list
		m_state_subscription = false;
	}

	void torrent::super_seeding(bool on)
	{
		if (on == m_super_seeding) return;

		m_super_seeding = on;
		m_need_save_resume_data = true;

		if (m_super_seeding) return;

		// disable super seeding for all peers
		for (peer_iterator i = begin(); i != end(); ++i)
		{
			(*i)->superseed_piece(-1, -1);
		}
	}

	int torrent::get_piece_to_super_seed(bitfield const& bits)
	{
		// return a piece with low availability that is not in
		// the bitfield and that is not currently being super
		// seeded by any peer
		TORRENT_ASSERT(m_super_seeding);
		
		// do a linear search from the first piece
		int min_availability = 9999;
		std::vector<int> avail_vec;
		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
		{
			if (bits[i]) continue;

			int availability = 0;
			for (const_peer_iterator j = begin(); j != end(); ++j)
			{
				if ((*j)->super_seeded_piece(i))
				{
					// avoid superseeding the same piece to more than one
					// peer if we can avoid it. Do this by artificially
					// increase the availability
					availability = 999;
					break;
				}
				if ((*j)->has_piece(i)) ++availability;
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

		return avail_vec[random() % avail_vec.size()];
	}

	void torrent::on_files_deleted(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());

		dec_refcount("delete_files");
		if (j->ret != 0)
		{
			if (alerts().should_post<torrent_delete_failed_alert>())
				alerts().post_alert(torrent_delete_failed_alert(get_handle()
					, j->error.ec, m_torrent_file->info_hash()));
		}
		else
		{
			alerts().post_alert(torrent_deleted_alert(get_handle(), m_torrent_file->info_hash()));
		}
	}

	void torrent::on_save_resume_data(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());
		torrent_ref_holder h(this, "save_resume");
		dec_refcount("save_resume");
		m_ses.done_async_resume();

		if (!j->buffer)
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle(), j->error.ec));
			return;
		}

		m_need_save_resume_data = false;
		m_last_saved_resume = m_ses.session_time();
		write_resume_data(*((entry*)j->buffer));
		alerts().post_alert(save_resume_data_alert(boost::shared_ptr<entry>((entry*)j->buffer)
			, get_handle()));
		const_cast<disk_io_job*>(j)->buffer = 0;
		state_updated();
	}

	void torrent::on_file_renamed(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());
		dec_refcount("rename_file");

		if (j->ret == 0)
		{
			if (alerts().should_post<file_renamed_alert>())
				alerts().post_alert(file_renamed_alert(get_handle(), j->buffer, j->piece));
			m_torrent_file->rename_file(j->piece, j->buffer);
		}
		else
		{
			if (alerts().should_post<file_rename_failed_alert>())
				alerts().post_alert(file_rename_failed_alert(get_handle()
					, j->piece, j->error.ec));
		}
	}

	void torrent::on_torrent_paused(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());

		if (alerts().should_post<torrent_paused_alert>())
			alerts().post_alert(torrent_paused_alert(get_handle()));
	}

	// TODO: 2 the tracker login feature should probably be deprecated
	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}

	boost::uint32_t torrent::tracker_key() const
	{
		uintptr_t self = (uintptr_t)this;
		uintptr_t ses = (uintptr_t)&m_ses;
		sha1_hash h = hasher((char*)&self, sizeof(self))
			.update((char*)&m_storage, sizeof(m_storage))
			.update((char*)&ses, sizeof(ses))
			.final();
		unsigned char const* ptr = &h[0];
		return detail::read_uint32(ptr);
	}

	void torrent::cancel_non_critical()
	{
		std::set<int> time_critical;
		for (std::vector<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			time_critical.insert(i->piece);
		}

		for (std::vector<peer_connection*>::iterator i
			= m_connections.begin(), end(m_connections.end()); i != end; ++i)
		{
			// for each peer, go through its download and request queue
			// and cancel everything, except pieces that are time critical
			peer_connection* p = *i;

			std::vector<pending_block> dq = p->download_queue();
			for (std::vector<pending_block>::iterator k = dq.begin()
				, end(dq.end()); k != end; ++k)
			{
				if (time_critical.count(k->block.piece_index)) continue;
				if (k->not_wanted || k->timed_out) continue;
				p->cancel_request(k->block, true);
			}

			std::vector<pending_block> rq = p->request_queue();
			for (std::vector<pending_block>::const_iterator k = rq.begin()
				, end(rq.end()); k != end; ++k)
			{
				if (time_critical.count(k->block.piece_index)) continue;
				p->cancel_request(k->block, true);
			}
		}
	}

	void torrent::set_piece_deadline(int piece, int t, int flags)
	{
		INVARIANT_CHECK;

		if (m_abort)
		{
			// failed
			if (flags & torrent_handle::alert_when_available)
			{
				m_ses.alerts().post_alert(read_piece_alert(
					get_handle(), piece, error_code(boost::system::errc::operation_canceled, system_category())));
			}
			return;
		}

		time_point deadline = aux::time_now() + milliseconds(t);

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
			// this gives the client a chance to specify multiple time critical
			// pieces before libtorrent cancels requests
			m_ses.get_io_service().post(boost::bind(&torrent::cancel_non_critical, this));
		}

		for (std::vector<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			if (i->piece != piece) continue;
			i->deadline = deadline;
			i->flags = flags;

			// resort i since deadline might have changed
			while (boost::next(i) != m_time_critical_pieces.end() && i->deadline > boost::next(i)->deadline)
			{
				std::iter_swap(i, boost::next(i));
				++i;
			}
			while (i != m_time_critical_pieces.begin() && i->deadline < boost::prior(i)->deadline)
			{
				std::iter_swap(i, boost::prior(i));
				--i;
			}
			// just in case this piece had priority 0
			int prev_prio = m_picker->piece_priority(piece);
			m_picker->set_piece_priority(piece, 7);
			if (prev_prio == 0) update_gauge();
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
		std::vector<time_critical_piece>::iterator i = std::upper_bound(m_time_critical_pieces.begin()
			, m_time_critical_pieces.end(), p);
		m_time_critical_pieces.insert(i, p);

		// just in case this piece had priority 0
		int prev_prio = m_picker->piece_priority(piece);
		m_picker->set_piece_priority(piece, 7);
		if (prev_prio == 0) update_gauge();

		piece_picker::downloading_piece pi;
		m_picker->piece_info(piece, pi);
		if (pi.requested == 0) return;
		// this means we have outstanding requests (or queued
		// up requests that haven't been sent yet). Promote them
		// to deadline pieces immediately
		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, piece);

		int block = 0;
		for (std::vector<void*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i, ++block)
		{
			torrent_peer* p = (torrent_peer*)*i;
			if (p == 0 || p->connection == 0) continue;
			peer_connection* peer = static_cast<peer_connection*>(p->connection);
			peer->make_time_critical(piece_block(piece, block));
		}
	}

	void torrent::reset_piece_deadline(int piece)
	{
		remove_time_critical_piece(piece);
	}

	void torrent::remove_time_critical_piece(int piece, bool finished)
	{
		for (std::vector<time_critical_piece>::iterator i
			= m_time_critical_pieces.begin(), end(m_time_critical_pieces.end());
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
					int dl_time = total_milliseconds(aux::time_now() - i->first_requested);
   
					if (m_average_piece_time == 0)
					{
						m_average_piece_time = dl_time;
					}
					else
					{
						int diff = abs(int(dl_time - m_average_piece_time));
						if (m_piece_time_deviation == 0) m_piece_time_deviation = diff;
						else m_piece_time_deviation = (m_piece_time_deviation * 9 + diff) / 10;
   
						m_average_piece_time = (m_average_piece_time * 9 + dl_time) / 10;
					}
				}
			}
			else if (i->flags & torrent_handle::alert_when_available)
			{
				// post an empty read_piece_alert to indicate it failed
				alerts().post_alert(read_piece_alert(
					get_handle(), piece, error_code(boost::system::errc::operation_canceled, system_category())));
			}
			if (has_picker()) m_picker->set_piece_priority(piece, 1);
			m_time_critical_pieces.erase(i);
			return;
		}
	}

	void torrent::clear_time_critical()
	{
		for (std::vector<time_critical_piece>::iterator i = m_time_critical_pieces.begin();
			i != m_time_critical_pieces.end();)
		{
			if (i->flags & torrent_handle::alert_when_available)
			{
				// post an empty read_piece_alert to indicate it failed
				m_ses.alerts().post_alert(read_piece_alert(
					get_handle(), i->piece, error_code(boost::system::errc::operation_canceled, system_category())));
			}
			if (has_picker()) m_picker->set_piece_priority(i->piece, 1);
			i = m_time_critical_pieces.erase(i);
		}
	}

	// remove time critical pieces where priority is 0
	void torrent::remove_time_critical_pieces(std::vector<int> const& priority)
	{
		for (std::vector<time_critical_piece>::iterator i = m_time_critical_pieces.begin();
			i != m_time_critical_pieces.end();)
		{
			if (priority[i->piece] == 0)
			{
				if (i->flags & torrent_handle::alert_when_available)
				{
					// post an empty read_piece_alert to indicate it failed
					alerts().post_alert(read_piece_alert(
						get_handle(), i->piece, error_code(boost::system::errc::operation_canceled, system_category())));
				}
				i = m_time_critical_pieces.erase(i);
				continue;
			}
			++i;
		}
	}

	void torrent::piece_availability(std::vector<int>& avail) const
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

	void torrent::set_piece_priority(int index, int priority)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());
		if (index < 0 || index >= m_torrent_file->num_pieces()) return;

		need_picker();

		bool was_finished = is_finished();
		bool filter_updated = m_picker->set_piece_priority(index, priority);
		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());

		update_gauge();
	
		if (filter_updated)
		{
			update_peer_interest(was_finished);
			if (priority == 0) remove_time_critical_piece(index);
		}

	}

	int torrent::piece_priority(int index) const
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (!has_picker()) return 1;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());
		if (index < 0 || index >= m_torrent_file->num_pieces()) return 0;

		return m_picker->piece_priority(index);
	}

	void torrent::prioritize_piece_list(std::vector<std::pair<int, int> > const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		need_picker();

		bool filter_updated = false;
		bool was_finished = is_finished();
		for (std::vector<std::pair<int, int> >::const_iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->second >= 0);
			TORRENT_ASSERT(i->second <= 7);
			TORRENT_ASSERT(i->first >= 0);
			TORRENT_ASSERT(i->first < m_torrent_file->num_pieces());

			if (i->first < 0 || i->first >= m_torrent_file->num_pieces() || i->second < 0 || i->second > 7)
				continue;

			filter_updated |= m_picker->set_piece_priority(i->first, i->second);
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		}
		update_gauge();
		if (filter_updated)
		{
			// we need to save this new state
			m_need_save_resume_data = true;

			update_peer_interest(was_finished);
		}

		state_updated();
	}

	void torrent::prioritize_pieces(std::vector<int> const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		need_picker();

		int index = 0;
		bool filter_updated = false;
		bool was_finished = is_finished();
		for (std::vector<int>::const_iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i, ++index)
		{
			TORRENT_ASSERT(*i >= 0);
			TORRENT_ASSERT(*i <= 7);
			filter_updated |= m_picker->set_piece_priority(index, *i);
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		}
		update_gauge();
		if (filter_updated)
		{
			// we need to save this new state
			m_need_save_resume_data = true;

			update_peer_interest(was_finished);
			remove_time_critical_pieces(pieces);
		}

		state_updated();
	}

	void torrent::piece_priorities(std::vector<int>* pieces) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (!has_picker())
		{
			pieces->clear();
			pieces->resize(m_torrent_file->num_pieces(), 1);
			return;
		}

		TORRENT_ASSERT(m_picker.get());
		m_picker->piece_priorities(*pieces);
	}

	namespace
	{
		void set_if_greater(int& piece_prio, int file_prio)
		{
			if (file_prio > piece_prio) piece_prio = file_prio;
		}
	}

	void torrent::on_file_priority()
	{
		dec_refcount("file_priority");
	}

	void torrent::prioritize_files(std::vector<int> const& files)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the vector need to have exactly one element for every file
		// in the torrent
		TORRENT_ASSERT(int(files.size()) == m_torrent_file->num_files());
		
		int limit = int(files.size());
		if (valid_metadata() && limit > m_torrent_file->num_files())
			limit = m_torrent_file->num_files();

		if (int(m_file_priority.size()) < limit)
			m_file_priority.resize(limit, 1);

		std::copy(files.begin(), files.begin() + limit, m_file_priority.begin());

		if (valid_metadata() && m_torrent_file->num_files() > int(m_file_priority.size()))
			m_file_priority.resize(m_torrent_file->num_files(), 1);

		// initialize pad files to priority 0
		file_storage const& fs = m_torrent_file->files();
		for (int i = 0; i < (std::min)(fs.num_files(), limit); ++i)
		{
			if (!fs.pad_file_at(i)) continue;
			m_file_priority[i] = 0;
		}

		// storage may be NULL during shutdown
		if (m_torrent_file->num_pieces() > 0 && m_storage)
		{
			inc_refcount("file_priority");
			m_ses.disk_thread().async_set_file_priority(m_storage.get()
				, m_file_priority, boost::bind(&torrent::on_file_priority, this));
		}

		update_piece_priorities();
	}

	void torrent::set_file_priority(int index, int prio)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		if (index < 0 || index >= m_torrent_file->num_files()) return;
		if (prio < 0) prio = 0;
		else if (prio > 7) prio = 7;
		if (int(m_file_priority.size()) <= index)
		{
			// any unallocated slot is assumed to be 1
			if (prio == 1) return;
			m_file_priority.resize(index+1, 1);

			// initialize pad files to priority 0
			file_storage const& fs = m_torrent_file->files();
			for (int i = 0; i < (std::min)(fs.num_files(), index+1); ++i)
			{
				if (!fs.pad_file_at(i)) continue;
				m_file_priority[i] = 0;
			}

		}

		if (m_file_priority[index] == prio) return;
		m_file_priority[index] = prio;

		// stoage may be NULL during shutdown
		if (m_storage)
		{
			inc_refcount("file_priority");
			m_ses.disk_thread().async_set_file_priority(m_storage.get()
				, m_file_priority, boost::bind(&torrent::on_file_priority, this));
		}
		update_piece_priorities();
	}
	
	int torrent::file_priority(int index) const
	{
		// this call is only valid on torrents with metadata
		if (!valid_metadata()) return 1;

		if (index < 0 || index >= m_torrent_file->num_files()) return 0;

		// any unallocated slot is assumed to be 1
		// unless it's a pad file
		if (int(m_file_priority.size()) <= index)
			return m_torrent_file->files().pad_file_at(index) ? 0 : 1;

		return m_file_priority[index];
	}

	void torrent::file_priorities(std::vector<int>* files) const
	{
		INVARIANT_CHECK;

		if (!valid_metadata())
		{
			files->resize(m_file_priority.size());
			std::copy(m_file_priority.begin(), m_file_priority.end(), files->begin());
			return;
		}

		files->clear();
		files->resize(m_torrent_file->num_files(), 1);
		TORRENT_ASSERT(int(m_file_priority.size()) <= m_torrent_file->num_files());
		std::copy(m_file_priority.begin(), m_file_priority.end(), files->begin());
	}

	void torrent::update_piece_priorities()
	{
		INVARIANT_CHECK;

		if (m_torrent_file->num_pieces() == 0) return;

		bool need_update = false;
		boost::int64_t position = 0;
		int piece_length = m_torrent_file->piece_length();
		// initialize the piece priorities to 0, then only allow
		// setting higher priorities
		std::vector<int> pieces(m_torrent_file->num_pieces(), 0);
		file_storage const& fs = m_torrent_file->files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			if (i >= fs.num_files()) break;

			boost::int64_t start = position;
			boost::int64_t size = m_torrent_file->files().file_size(i);
			if (size == 0) continue;
			position += size;
			int file_prio;
			if (m_file_priority.size() <= i)
				file_prio = 1;
			else
				file_prio = m_file_priority[i];

			if (file_prio == 0)
			{
				need_update = true;
				continue;
			}

			// mark all pieces of the file with this file's priority
			// but only if the priority is higher than the pieces
			// already set (to avoid problems with overlapping pieces)
			int start_piece = int(start / piece_length);
			int last_piece = int((position - 1) / piece_length);
			TORRENT_ASSERT(last_piece < int(pieces.size()));
			// if one piece spans several files, we might
			// come here several times with the same start_piece, end_piece
			std::for_each(pieces.begin() + start_piece
				, pieces.begin() + last_piece + 1
				, boost::bind(&set_if_greater, _1, file_prio));

			if (has_picker() || file_prio != 1)
				need_update = true;
		}
		if (need_update) prioritize_pieces(pieces);
	}

	// this is called when piece priorities have been updated
	// updates the interested flag in peers
	void torrent::update_peer_interest(bool was_finished)
	{
		for (peer_iterator i = begin(); i != end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			p->update_interest();
		}

#if defined TORRENT_LOGGING
		debug_log("*** UPDATE_PEER_INTEREST [ finished: %d was_finished %d ]"
			, is_finished(), was_finished);
#endif

		// the torrent just became finished
		if (is_finished() && !was_finished)
		{
			finished();
		}
		else if (!is_finished() && was_finished)
		{
			// if we used to be finished, but we aren't anymore
			// we may need to connect to peers again
			resume_download();
		}
	}

	void torrent::filter_piece(int index, bool filter)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;
		need_picker();

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		if (index < 0 || index >= m_torrent_file->num_pieces()) return;

		bool was_finished = is_finished();
		m_picker->set_piece_priority(index, filter ? 1 : 0);
		update_peer_interest(was_finished);
		update_gauge();
	}

	void torrent::filter_pieces(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		need_picker();

		bool was_finished = is_finished();
		int index = 0;
		for (std::vector<bool>::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if ((m_picker->piece_priority(index) == 0) == *i) continue;
			if (*i)
				m_picker->set_piece_priority(index, 0);
			else
				m_picker->set_piece_priority(index, 1);
		}
		update_peer_interest(was_finished);
		update_gauge();
	}

	bool torrent::is_piece_filtered(int index) const
	{
		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (!has_picker()) return false;
		
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		if (index < 0 || index >= m_torrent_file->num_pieces()) return true;

		return m_picker->piece_priority(index) == 0;
	}

	void torrent::filtered_pieces(std::vector<bool>& bitmask) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (!has_picker())
		{
			bitmask.clear();
			bitmask.resize(m_torrent_file->num_pieces(), false);
			return;
		}

		TORRENT_ASSERT(m_picker.get());
		m_picker->filtered_pieces(bitmask);
	}

	void torrent::filter_files(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the bitmask need to have exactly one bit for every file
		// in the torrent
		TORRENT_ASSERT((int)bitmask.size() == m_torrent_file->num_files());

		if (int(bitmask.size()) != m_torrent_file->num_files()) return;
		
		boost::int64_t position = 0;

		if (m_torrent_file->num_pieces())
		{
			int piece_length = m_torrent_file->piece_length();
			// mark all pieces as filtered, then clear the bits for files
			// that should be downloaded
			std::vector<bool> piece_filter(m_torrent_file->num_pieces(), true);
			for (int i = 0; i < (int)bitmask.size(); ++i)
			{
				boost::int64_t start = position;
				position += m_torrent_file->files().file_size(i);
				// is the file selected for download?
				if (!bitmask[i])
				{           
					// mark all pieces of the file as downloadable
					int start_piece = int(start / piece_length);
					int last_piece = int(position / piece_length);
					// if one piece spans several files, we might
					// come here several times with the same start_piece, end_piece
					std::fill(piece_filter.begin() + start_piece, piece_filter.begin()
						+ last_piece + 1, false);
				}
			}
			filter_pieces(piece_filter);
		}
	}

	bool has_empty_url(announce_entry const& e) { return e.url.empty(); }

	void torrent::replace_trackers(std::vector<announce_entry> const& urls)
	{
		m_trackers.clear();
		std::remove_copy_if(urls.begin(), urls.end(), back_inserter(m_trackers)
			, &has_empty_url);

		m_last_working_tracker = -1;
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (i->source == 0) i->source = announce_entry::source_client;
			i->complete_sent = is_seed();
		}

		if (settings().get_bool(settings_pack::prefer_udp_trackers))
			prioritize_udp_trackers();

		if (!m_trackers.empty()) announce_with_tracker();

		m_need_save_resume_data = true;
	}

	void torrent::prioritize_udp_trackers()
	{
		// look for udp-trackers
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (i->url.substr(0, 6) != "udp://") continue;
			// now, look for trackers with the same hostname
			// that is has higher priority than this one
			// if we find one, swap with the udp-tracker
			error_code ec;
			std::string udp_hostname;
			using boost::tuples::ignore;
			boost::tie(ignore, ignore, udp_hostname, ignore, ignore)
				= parse_url_components(i->url, ec);
			for (std::vector<announce_entry>::iterator j = m_trackers.begin();
				j != i; ++j)
			{
				std::string hostname;
				boost::tie(ignore, ignore, hostname, ignore, ignore)
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
		std::vector<announce_entry>::iterator k = std::find_if(m_trackers.begin()
			, m_trackers.end(), boost::bind(&announce_entry::url, _1) == url.url);
		if (k != m_trackers.end()) 
		{
			k->source |= url.source;
			return false;
		}
		k = std::upper_bound(m_trackers.begin(), m_trackers.end(), url
			, boost::bind(&announce_entry::tier, _1) < boost::bind(&announce_entry::tier, _2));
		if (k - m_trackers.begin() < m_last_working_tracker) ++m_last_working_tracker;
		k = m_trackers.insert(k, url);
		if (k->source == 0) k->source = announce_entry::source_client;
		if (m_allow_peers && !m_trackers.empty()) announce_with_tracker();
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

	void torrent::trigger_unchoke()
	{
		m_ses.get_io_service().dispatch(boost::bind(
			&aux::session_interface::trigger_unchoke, boost::ref(m_ses)));
	}

	void torrent::trigger_optimistic_unchoke()
	{
		m_ses.get_io_service().dispatch(boost::bind(
			&aux::session_interface::trigger_optimistic_unchoke, boost::ref(m_ses)));
	}

	void torrent::cancel_block(piece_block block)
	{
		INVARIANT_CHECK;

		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			(*i)->cancel_request(block);
		}
	}

#ifdef TORRENT_USE_OPENSSL
	std::string password_callback(int length, boost::asio::ssl::context::password_purpose p
		, std::string pw)
	{
		if (p != boost::asio::ssl::context::for_reading) return "";
		return pw;
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
				alerts().post_alert(torrent_error_alert(get_handle()
					, error_code(errors::not_an_ssl_torrent), ""));
			return;
		}

		using boost::asio::ssl::context;
		error_code ec;
		m_ssl_ctx->set_password_callback(boost::bind(&password_callback, _1, _2, passphrase), ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, ""));
		}
		m_ssl_ctx->use_certificate_file(certificate, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, certificate));
		}
		m_ssl_ctx->use_private_key_file(private_key, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, private_key));
		}
		m_ssl_ctx->use_tmp_dh_file(dh_params, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, dh_params));
		}
	}

	void torrent::set_ssl_cert_buffer(std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params)
	{
		if (!m_ssl_ctx) return;

#if BOOST_VERSION < 105400
		if (alerts().should_post<torrent_error_alert>())
			alerts().post_alert(torrent_error_alert(get_handle()
				, error_code(boost::system::errc::not_supported, system_category()), "[certificate]"));
#else
		boost::asio::const_buffer certificate_buf(certificate.c_str(), certificate.size());

		using boost::asio::ssl::context;
		error_code ec;
		m_ssl_ctx->use_certificate(certificate_buf, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, "[certificate]"));
		}

		boost::asio::const_buffer private_key_buf(private_key.c_str(), private_key.size());
		m_ssl_ctx->use_private_key(private_key_buf, context::pem, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, "[private key]"));
		}

		boost::asio::const_buffer dh_params_buf(dh_params.c_str(), dh_params.size());
		m_ssl_ctx->use_tmp_dh(dh_params_buf, ec);
		if (ec)
		{
			if (alerts().should_post<torrent_error_alert>())
				alerts().post_alert(torrent_error_alert(get_handle(), ec, "[dh params]"));
		}
#endif // BOOST_VERSION
	}

#endif

	void torrent::remove_peer(peer_connection* p)
	{
		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(is_single_thread());

		peer_iterator i = sorted_find(m_connections, p);
		if (i == m_connections.end())
		{
			TORRENT_ASSERT(false);
			return;
		}

		if (ready_for_connections())
		{
			TORRENT_ASSERT(p->associated_torrent().lock().get() == NULL
				|| p->associated_torrent().lock().get() == this);

			if (p->is_seed())
			{
				if (has_picker())
				{
					m_picker->dec_refcount_all(p);
				}
			}
			else
			{
				if (has_picker())
				{
					bitfield const& pieces = p->get_bitfield();
					TORRENT_ASSERT(pieces.count() <= int(pieces.size()));
					m_picker->dec_refcount(pieces, p);
				}
			}
		}

		if (!p->is_choked() && !p->ignore_unchoke_slots())
		{
			--m_num_uploads;
			trigger_unchoke();
		}

		torrent_peer* pp = p->peer_info_struct();
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
			pp->prev_amount_download += p->statistics().total_payload_download() >> 10;
			pp->prev_amount_upload += p->statistics().total_payload_upload() >> 10;

			if (pp->seed)
			{
				TORRENT_ASSERT(m_num_seeds > 0);
				--m_num_seeds;
			}
		}

		torrent_state st = get_policy_state();
		if (m_peer_list) m_peer_list->connection_closed(*p, m_ses.session_time(), &st);
		peers_erased(st.erased);

		p->set_peer_info(0);
		TORRENT_ASSERT(i != m_connections.end());
		m_connections.erase(i);
		update_want_peers();
		update_want_tick();
	}

	void torrent::remove_web_seed(std::list<web_seed_t>::iterator web)
	{
		if (web->resolving)
		{
			web->removed = true;
			return;
		}
		peer_connection* peer = static_cast<peer_connection*>(web->peer_info.connection);
		if (peer)
		{
			// if we have a connection for this web seed, we also need to
			// disconnect it and clear its reference to the peer_info object
			// that's part of the web_seed_t we're about to remove
			TORRENT_ASSERT(peer->m_in_use == 1337);
			peer->disconnect(boost::asio::error::operation_aborted, op_bittorrent);
			peer->set_peer_info(0);
		}
		if (has_picker()) picker().clear_peer(&web->peer_info);

		m_web_seeds.erase(web);
		update_want_tick();
	}

	void torrent::connect_to_url_seed(std::list<web_seed_t>::iterator web)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(!web->resolving);
		if (web->resolving) return;

		if (int(m_connections.size()) >= m_max_connections
			|| m_ses.num_connections() >= m_ses.settings().get_int(settings_pack::connections_limit))
			return;

		std::string protocol;
		std::string auth;
		std::string hostname;
		int port;
		std::string path;
		error_code ec;
		boost::tie(protocol, auth, hostname, port, path)
			= parse_url_components(web->url, ec);
		if (port == -1)
		{
			port = protocol == "http" ? 80 : 443;
		}

		if (ec)
		{
#if defined TORRENT_LOGGING
			debug_log("failed to parse web seed url: %s", ec.message().c_str());
#endif
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, ec));
			}
			// never try it again
			remove_web_seed(web);
			return;
		}

		if (web->peer_info.banned)
		{
#if defined TORRENT_LOGGING
			debug_log("banned web seed: %s", web->url.c_str());
#endif
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url
						, error_code(libtorrent::errors::peer_banned, get_libtorrent_category())));
			}
			// never try it again
			remove_web_seed(web);
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
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, errors::unsupported_url_protocol));
			}
			// never try it again
			remove_web_seed(web);
			return;
		}

		if (hostname.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, errors::invalid_hostname));
			}
			// never try it again
			remove_web_seed(web);
			return;
		}

		if (port == 0)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, errors::invalid_port));
			}
			// never try it again
			remove_web_seed(web);
			return;
		}

		if (m_ses.get_port_filter().access(port) & port_filter::blocked)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
				url_seed_alert(get_handle(), web->url, errors::port_blocked));
			}
			// never try it again
			remove_web_seed(web);
			return;
		}

		if (!web->endpoints.empty())
		{
			connect_web_seed(web, web->endpoints.front());
			return;
		}

#if defined TORRENT_LOGGING
		debug_log("resolving web seed: %s", web->url.c_str());
#endif

		proxy_settings const& ps = m_ses.proxy();
		if (ps.type == settings_pack::http
			|| ps.type == settings_pack::http_pw)
		{
#if defined TORRENT_LOGGING
			debug_log("resolving proxy for web seed: %s", web->url.c_str());
#endif

			// use proxy
			web->resolving = true;
			m_ses.async_resolve(ps.hostname, resolver_interface::abort_on_shutdown
				, boost::bind(&torrent::on_proxy_name_lookup, shared_from_this()
					, _1, _2, web, ps.port));
		}
		else if (ps.proxy_hostnames
			&& (ps.type == settings_pack::socks5
				|| ps.type == settings_pack::socks5_pw))
		{
			connect_web_seed(web, tcp::endpoint(address(), port));
		}
		else
		{
#if defined TORRENT_LOGGING
			debug_log("resolving web seed: %s", web->url.c_str());
#endif

			web->resolving = true;
			m_ses.async_resolve(hostname, resolver_interface::abort_on_shutdown
				, boost::bind(&torrent::on_name_lookup, shared_from_this(), _1, _2
				, port, web, tcp::endpoint()));
		}
	}

	void torrent::on_proxy_name_lookup(error_code const& e
		, std::vector<address> const& addrs
		, std::list<web_seed_t>::iterator web, int port)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		TORRENT_ASSERT(web->resolving == true);
#if defined TORRENT_LOGGING
		debug_log("completed resolve proxy hostname for: %s", web->url.c_str());
		if (e)
			debug_log("proxy name lookup error: %s", e.message().c_str());
#endif
		web->resolving = false;

		if (web->removed)
		{
#if defined TORRENT_LOGGING
			debug_log("removed web seed");
#endif
			remove_web_seed(web);
			return;
		}

		if (m_abort) return;

		if (e || addrs.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, e));
			}

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_web_seed(web);
			return;
		}

		if (m_ses.is_aborted()) return;

		if (int(m_connections.size()) >= m_max_connections
			|| m_ses.num_connections() >= m_ses.settings().get_int(settings_pack::connections_limit))
			return;

		tcp::endpoint a(addrs[0], port);

		using boost::tuples::ignore;
		std::string hostname;
		error_code ec;
		std::string protocol;
		boost::tie(protocol, ignore, hostname, port, ignore)
			= parse_url_components(web->url, ec);
		if (port == -1) port = protocol == "http" ? 80 : 443;

		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
			{
				m_ses.alerts().post_alert(
					url_seed_alert(get_handle(), web->url, ec));
			}
			remove_web_seed(web);
			return;
		}

		if (m_apply_ip_filter
			&& m_ses.get_ip_filter().access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().post_alert(peer_blocked_alert(get_handle()
					, a.address(), peer_blocked_alert::ip_filter));
			return;
		}

		web->resolving = true;
		m_ses.async_resolve(hostname, resolver_interface::abort_on_shutdown
			, boost::bind(&torrent::on_name_lookup, shared_from_this(), _1, _2
			, port, web, a));
	}

	void torrent::on_name_lookup(error_code const& e
		, std::vector<address> const& addrs
		, int port
		, std::list<web_seed_t>::iterator web
		, tcp::endpoint proxy)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		TORRENT_ASSERT(web->resolving == true);
#if defined TORRENT_LOGGING
		debug_log("completed resolve: %s", web->url.c_str());
#endif
		web->resolving = false;
		if (web->removed)
		{
#if defined TORRENT_LOGGING
			debug_log("removed web seed");
#endif
			remove_web_seed(web);
			return;
		}

		if (m_abort) return;

		if (e || addrs.empty())
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().post_alert(url_seed_alert(get_handle(), web->url, e));
#if defined TORRENT_LOGGING
			debug_log("*** HOSTNAME LOOKUP FAILED: %s: (%d) %s"
				, web->url.c_str(), e.value(), e.message().c_str());
#endif

			// unavailable, retry in 30 minutes
			web->retry = aux::time_now() + minutes(30);
			return;
		}

		for (std::vector<address>::const_iterator i = addrs.begin()
			, end(addrs.end()); i != end; ++i)
		{
			// fill in the peer struct's address field
			web->endpoints.push_back(tcp::endpoint(*i, port));

#if defined TORRENT_LOGGING
			debug_log("  -> %s", print_endpoint(tcp::endpoint(*i, port)).c_str());
#endif
		}

		if (int(m_connections.size()) >= m_max_connections
			|| m_ses.num_connections() >= m_ses.settings().get_int(settings_pack::connections_limit))
			return;

		connect_web_seed(web, web->endpoints.front());
	}

	void torrent::connect_web_seed(std::list<web_seed_t>::iterator web, tcp::endpoint a)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());
		if (m_abort) return;

		if (m_apply_ip_filter
			&& m_ses.get_ip_filter().access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().post_alert(peer_blocked_alert(get_handle()
					, a.address(), peer_blocked_alert::ip_filter));
			return;
		}
		
		TORRENT_ASSERT(web->resolving == false);
		TORRENT_ASSERT(web->peer_info.connection == 0);

		if (a.address().is_v4())
		{
			web->peer_info.addr = a.address().to_v4();
			web->peer_info.port = a.port();
		}

		if (is_paused()) return;
		if (m_ses.is_aborted()) return;

		boost::shared_ptr<socket_type> s
			= boost::make_shared<socket_type>(boost::ref(m_ses.get_io_service()));
		if (!s) return;
	
		void* userdata = 0;
#ifdef TORRENT_USE_OPENSSL
		bool ssl = string_begins_no_case("https://", web->url.c_str());
		if (ssl)
		{
			userdata = m_ssl_ctx.get();
			if (!userdata) userdata = m_ses.ssl_ctx();
		}
#endif
		bool ret = instantiate_connection(m_ses.get_io_service(), m_ses.proxy(), *s, userdata, 0, true);
		(void)ret;
		TORRENT_ASSERT(ret);

		if (s->get<http_stream>())
		{
			// the web seed connection will talk immediately to
			// the proxy, without requiring CONNECT support
			s->get<http_stream>()->set_no_connect(true);
		}

		using boost::tuples::ignore;
		std::string hostname;
		error_code ec;
		boost::tie(ignore, ignore, hostname, ignore, ignore)
			= parse_url_components(web->url, ec);
		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().post_alert(url_seed_alert(get_handle(), web->url, ec));
			return;
		}

		bool proxy_hostnames = m_ses.settings().get_bool(settings_pack::proxy_hostnames);
		int proxy_type = m_ses.settings().get_int(settings_pack::proxy_type);

		if (proxy_hostnames
			&& (proxy_type == settings_pack::socks5
				|| proxy_type == settings_pack::socks5_pw))
		{
			// we're using a socks proxy and we're resolving
			// hostnames through it
			socks5_stream* str =
#ifdef TORRENT_USE_OPENSSL
				ssl ? &s->get<ssl_stream<socks5_stream> >()->next_layer() :
#endif
				s->get<socks5_stream>();
			TORRENT_ASSERT(str);

			str->set_dst_name(hostname);
		}

		setup_ssl_hostname(*s, hostname, ec);
		if (ec)
		{
			if (m_ses.alerts().should_post<url_seed_alert>())
				m_ses.alerts().post_alert(url_seed_alert(get_handle(), web->url, ec));
			return;
		}

		boost::shared_ptr<peer_connection> c;
		peer_connection_args pack;
		pack.ses = &m_ses;
		pack.sett = &m_ses.settings();
		pack.stats_counters = &m_ses.stats_counters();
		pack.allocator = &m_ses;
		pack.disk_thread = &m_ses.disk_thread();
		pack.ios = &m_ses.get_io_service();
		pack.tor = shared_from_this();
		pack.s = s;
		pack.endp = a;
		pack.peerinfo = &web->peer_info;
		if (web->type == web_seed_entry::url_seed)
		{
			c = boost::make_shared<web_peer_connection>(
				boost::cref(pack), boost::ref(*web));
		}
		else if (web->type == web_seed_entry::http_seed)
		{
			c = boost::make_shared<http_seed_connection>(
				boost::cref(pack), boost::ref(*web));
		}
		if (!c) return;

#if TORRENT_USE_ASSERTS
		c->m_in_constructor = false;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<peer_plugin>
				pp((*i)->new_connection(c.get()));
			if (pp) c->add_extension(pp);
		}
#endif

		TORRENT_TRY
		{
			TORRENT_ASSERT(!c->m_in_constructor);
			// add the newly connected peer to this torrent's peer list
			sorted_insert(m_connections, boost::get_pointer(c));
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

			c->add_stat(boost::int64_t(web->peer_info.prev_amount_download) << 10
				, boost::int64_t(web->peer_info.prev_amount_upload) << 10);
			web->peer_info.prev_amount_download = 0;
			web->peer_info.prev_amount_upload = 0;
#if defined TORRENT_LOGGING 
			debug_log("web seed connection started: [%s] %s"
				, print_endpoint(a).c_str(), web->url.c_str());
#endif

			c->start();

			if (c->is_disconnecting()) return;

#if defined TORRENT_LOGGING
			debug_log("START queue peer [%p] (%d)", c.get(), num_peers());
#endif
		}
		TORRENT_CATCH (std::exception& e)
		{
			TORRENT_DECLARE_DUMMY(std::exception, e);
			(void)e;
#if defined TORRENT_LOGGING
			debug_log("*** PEER_ERROR: %s", e.what());
#endif
			c->disconnect(errors::no_error, op_bittorrent, 1);
		}
	}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	namespace
	{
		unsigned long swap_bytes(unsigned long a)
		{
			return (a >> 24) | ((a & 0xff0000) >> 8) | ((a & 0xff00) << 8) | ((a & 0xff) << 24);
		}
	}

	void torrent::resolve_countries(bool r)
	{ m_resolve_countries = r; }

	bool torrent::resolving_countries() const
	{
		return m_resolve_countries && !m_ses.settings().get_bool(settings_pack::force_proxy);
	}
	
	void torrent::resolve_peer_country(boost::shared_ptr<peer_connection> const& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_resolving_country
			|| is_local(p->remote().address())
			|| p->has_country()
			|| p->is_connecting()
			|| p->in_handshake()
			|| p->remote().address().is_v6()) return;

		asio::ip::address_v4 reversed(swap_bytes(p->remote().address().to_v4().to_ulong()));
		error_code ec;
		std::string hostname = reversed.to_string(ec) + ".zz.countries.nerd.dk";
		if (ec)
		{
			p->set_country("!!");
			return;
		}
		m_resolving_country = true;
		m_ses.async_resolve(hostname, resolver_interface::abort_on_shutdown
			, boost::bind(&torrent::on_country_lookup, shared_from_this(), _1, _2, p));
	}

	namespace
	{
		struct country_entry
		{
			int code;
			char const* name;
		};
	}

	void torrent::on_country_lookup(error_code const& error
		, std::vector<address> const& host_list
		, boost::shared_ptr<peer_connection> p) const
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;
		
		m_resolving_country = false;

		if (m_abort) return;

		// must be ordered in increasing order
		static const country_entry country_map[] =
		{
			  {  4,  "AF"}, {  8,  "AL"}, { 10,  "AQ"}, { 12,  "DZ"}, { 16,  "AS"}
			, { 20,  "AD"}, { 24,  "AO"}, { 28,  "AG"}, { 31,  "AZ"}, { 32,  "AR"}
			, { 36,  "AU"}, { 40,  "AT"}, { 44,  "BS"}, { 48,  "BH"}, { 50,  "BD"}
			, { 51,  "AM"}, { 52,  "BB"}, { 56,  "BE"}, { 60,  "BM"}, { 64,  "BT"}
			, { 68,  "BO"}, { 70,  "BA"}, { 72,  "BW"}, { 74,  "BV"}, { 76,  "BR"}
			, { 84,  "BZ"}, { 86,  "IO"}, { 90,  "SB"}, { 92,  "VG"}, { 96,  "BN"}
			, {100,  "BG"}, {104,  "MM"}, {108,  "BI"}, {112,  "BY"}, {116,  "KH"}
			, {120,  "CM"}, {124,  "CA"}, {132,  "CV"}, {136,  "KY"}, {140,  "CF"}
			, {144,  "LK"}, {148,  "TD"}, {152,  "CL"}, {156,  "CN"}, {158,  "TW"}
			, {162,  "CX"}, {166,  "CC"}, {170,  "CO"}, {174,  "KM"}, {175,  "YT"}
			, {178,  "CG"}, {180,  "CD"}, {184,  "CK"}, {188,  "CR"}, {191,  "HR"}
			, {192,  "CU"}, {203,  "CZ"}, {204,  "BJ"}, {208,  "DK"}, {212,  "DM"}
			, {214,  "DO"}, {218,  "EC"}, {222,  "SV"}, {226,  "GQ"}, {231,  "ET"}
			, {232,  "ER"}, {233,  "EE"}, {234,  "FO"}, {238,  "FK"}, {239,  "GS"}
			, {242,  "FJ"}, {246,  "FI"}, {248,  "AX"}, {250,  "FR"}, {254,  "GF"}
			, {258,  "PF"}, {260,  "TF"}, {262,  "DJ"}, {266,  "GA"}, {268,  "GE"}
			, {270,  "GM"}, {275,  "PS"}, {276,  "DE"}, {288,  "GH"}, {292,  "GI"}
			, {296,  "KI"}, {300,  "GR"}, {304,  "GL"}, {308,  "GD"}, {312,  "GP"}
			, {316,  "GU"}, {320,  "GT"}, {324,  "GN"}, {328,  "GY"}, {332,  "HT"}
			, {334,  "HM"}, {336,  "VA"}, {340,  "HN"}, {344,  "HK"}, {348,  "HU"}
			, {352,  "IS"}, {356,  "IN"}, {360,  "ID"}, {364,  "IR"}, {368,  "IQ"}
			, {372,  "IE"}, {376,  "IL"}, {380,  "IT"}, {384,  "CI"}, {388,  "JM"}
			, {392,  "JP"}, {398,  "KZ"}, {400,  "JO"}, {404,  "KE"}, {408,  "KP"}
			, {410,  "KR"}, {414,  "KW"}, {417,  "KG"}, {418,  "LA"}, {422,  "LB"}
			, {426,  "LS"}, {428,  "LV"}, {430,  "LR"}, {434,  "LY"}, {438,  "LI"}
			, {440,  "LT"}, {442,  "LU"}, {446,  "MO"}, {450,  "MG"}, {454,  "MW"}
			, {458,  "MY"}, {462,  "MV"}, {466,  "ML"}, {470,  "MT"}, {474,  "MQ"}
			, {478,  "MR"}, {480,  "MU"}, {484,  "MX"}, {492,  "MC"}, {496,  "MN"}
			, {498,  "MD"}, {500,  "MS"}, {504,  "MA"}, {508,  "MZ"}, {512,  "OM"}
			, {516,  "NA"}, {520,  "NR"}, {524,  "NP"}, {528,  "NL"}, {530,  "AN"}
			, {533,  "AW"}, {540,  "NC"}, {548,  "VU"}, {554,  "NZ"}, {558,  "NI"}
			, {562,  "NE"}, {566,  "NG"}, {570,  "NU"}, {574,  "NF"}, {578,  "NO"}
			, {580,  "MP"}, {581,  "UM"}, {583,  "FM"}, {584,  "MH"}, {585,  "PW"}
			, {586,  "PK"}, {591,  "PA"}, {598,  "PG"}, {600,  "PY"}, {604,  "PE"}
			, {608,  "PH"}, {612,  "PN"}, {616,  "PL"}, {620,  "PT"}, {624,  "GW"}
			, {626,  "TL"}, {630,  "PR"}, {634,  "QA"}, {634,  "QA"}, {638,  "RE"}
			, {642,  "RO"}, {643,  "RU"}, {646,  "RW"}, {654,  "SH"}, {659,  "KN"}
			, {660,  "AI"}, {662,  "LC"}, {666,  "PM"}, {670,  "VC"}, {674,  "SM"}
			, {678,  "ST"}, {682,  "SA"}, {686,  "SN"}, {690,  "SC"}, {694,  "SL"}
			, {702,  "SG"}, {703,  "SK"}, {704,  "VN"}, {705,  "SI"}, {706,  "SO"}
			, {710,  "ZA"}, {716,  "ZW"}, {724,  "ES"}, {732,  "EH"}, {736,  "SD"}
			, {740,  "SR"}, {744,  "SJ"}, {748,  "SZ"}, {752,  "SE"}, {756,  "CH"}
			, {760,  "SY"}, {762,  "TJ"}, {764,  "TH"}, {768,  "TG"}, {772,  "TK"}
			, {776,  "TO"}, {780,  "TT"}, {784,  "AE"}, {788,  "TN"}, {792,  "TR"}
			, {795,  "TM"}, {796,  "TC"}, {798,  "TV"}, {800,  "UG"}, {804,  "UA"}
			, {807,  "MK"}, {818,  "EG"}, {826,  "GB"}, {834,  "TZ"}, {840,  "US"}
			, {850,  "VI"}, {854,  "BF"}, {858,  "UY"}, {860,  "UZ"}, {862,  "VE"}
			, {876,  "WF"}, {882,  "WS"}, {887,  "YE"}, {891,  "CS"}, {894,  "ZM"}
		};

		if (error || host_list.empty())
		{
			// this is used to indicate that we shouldn't
			// try to resolve it again
			p->set_country("--");
			return;
		}

		int idx = 0;
		while (idx < host_list.size() && !host_list[idx].is_v4())
			++idx;
			
		if (idx >= host_list.size())
		{
			p->set_country("--");
			return;
		}

		// country is an ISO 3166 country code
		int country = host_list[idx].to_v4().to_ulong() & 0xffff;
			
		// look up the country code in the map
		const int size = sizeof(country_map)/sizeof(country_map[0]);
		country_entry tmp = {country, ""};
		country_entry const* j =
			std::lower_bound(country_map, country_map + size, tmp
				, boost::bind(&country_entry::code, _1) < boost::bind(&country_entry::code, _2));
		if (j == country_map + size
			|| j->code != country)
		{
			// unknown country!
			p->set_country("!!");
#if defined TORRENT_LOGGING
			debug_log("IP \"%s\" was mapped to unknown country: %d"
				, print_address(p->remote().address()).c_str(), country);
#endif
			return;
		}
			
		p->set_country(j->name);
	}
#endif

	void torrent::read_resume_data(bdecode_node const& rd)
	{
		m_total_uploaded = rd.dict_find_int_value("total_uploaded");
		m_total_downloaded = rd.dict_find_int_value("total_downloaded");
		m_active_time = rd.dict_find_int_value("active_time");
		m_finished_time = rd.dict_find_int_value("finished_time");
		m_seeding_time = rd.dict_find_int_value("seeding_time");
		m_last_seen_complete = rd.dict_find_int_value("last_seen_complete");
		m_complete = rd.dict_find_int_value("num_complete", 0xffffff);
		m_incomplete = rd.dict_find_int_value("num_incomplete", 0xffffff);
		m_downloaded = rd.dict_find_int_value("num_downloaded", 0xffffff);

		if (!m_override_resume_data)
		{
			int up_limit_ = rd.dict_find_int_value("upload_rate_limit", -1);
			if (up_limit_ != -1) set_upload_limit(up_limit_);

			int down_limit_ = rd.dict_find_int_value("download_rate_limit", -1);
			if (down_limit_ != -1) set_download_limit(down_limit_);

			int max_connections_ = rd.dict_find_int_value("max_connections", -1);
			if (max_connections_ != -1) set_max_connections(max_connections_);

			int max_uploads_ = rd.dict_find_int_value("max_uploads", -1);
			if (max_uploads_ != -1) set_max_uploads(max_uploads_);

			int seed_mode_ = rd.dict_find_int_value("seed_mode", -1);
			if (seed_mode_ != -1) m_seed_mode = seed_mode_ && m_torrent_file->is_valid();

			int super_seeding_ = rd.dict_find_int_value("super_seeding", -1);
			if (super_seeding_ != -1) super_seeding(super_seeding_);

			int auto_managed_ = rd.dict_find_int_value("auto_managed", -1);
			if (auto_managed_ != -1) m_auto_managed = auto_managed_;

			int sequential_ = rd.dict_find_int_value("sequential_download", -1);
			if (sequential_ != -1) set_sequential_download(sequential_);

			int paused_ = rd.dict_find_int_value("paused", -1);
			if (paused_ != -1)
			{
				set_allow_peers(!paused_);
				m_announce_to_dht = !paused_;
				m_announce_to_trackers = !paused_;
				m_announce_to_lsd = !paused_;

				update_gauge();
				update_want_peers();
				update_want_scrape();
			}
			int dht_ = rd.dict_find_int_value("announce_to_dht", -1);
			if (dht_ != -1) m_announce_to_dht = dht_;
			int lsd_ = rd.dict_find_int_value("announce_to_lsd", -1);
			if (lsd_ != -1) m_announce_to_lsd = lsd_;
			int track_ = rd.dict_find_int_value("announce_to_trackers", -1);
			if (track_ != -1) m_announce_to_trackers = track_;
		}

		if (m_seed_mode)
			m_verified.resize(m_torrent_file->num_pieces(), false);

		int now = m_ses.session_time();
		int tmp = rd.dict_find_int_value("last_scrape", -1);
		m_last_scrape = tmp == -1 ? (std::numeric_limits<boost::int16_t>::min)() : now - tmp;
		tmp = rd.dict_find_int_value("last_download", -1);
		m_last_download = tmp == -1 ? (std::numeric_limits<boost::int16_t>::min)() : now - tmp;
		tmp = rd.dict_find_int_value("last_upload", -1);
		m_last_upload = tmp == -1 ? (std::numeric_limits<boost::int16_t>::min)() : now - tmp;

		if (m_use_resume_save_path)
		{
			std::string p = rd.dict_find_string_value("save_path");
			if (!p.empty()) m_save_path = p;
		}

		m_url = rd.dict_find_string_value("url");
		m_uuid = rd.dict_find_string_value("uuid");
		m_source_feed_url = rd.dict_find_string_value("feed");

		if (!m_uuid.empty() || !m_url.empty())
		{
			boost::shared_ptr<torrent> me(shared_from_this());

			// insert this torrent in the uuid index
			m_ses.insert_uuid_torrent(m_uuid.empty() ? m_url : m_uuid, me);
		}

		// TODO: make this more generic to not just work if files have been
		// renamed, but also if they have been merged into a single file for instance
		// maybe use the same format as .torrent files and reuse some code from torrent_info
		// The mapped_files needs to be read both in the network thread
		// and in the disk thread, since they both have their own mapped files structures
		// which are kept in sync
		bdecode_node mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files.list_size() == m_torrent_file->num_files())
		{
			for (int i = 0; i < m_torrent_file->num_files(); ++i)
			{
				std::string new_filename = mapped_files.list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_torrent_file->rename_file(i, new_filename);
			}
		}
		
		m_added_time = rd.dict_find_int_value("added_time", m_added_time);
		m_completed_time = rd.dict_find_int_value("completed_time", m_completed_time);
		if (m_completed_time != 0 && m_completed_time < m_added_time)
			m_completed_time = m_added_time;

		if (!m_seed_mode && !m_override_resume_data)
		{
			bdecode_node file_priority = rd.dict_find_list("file_priority");
			if (file_priority && file_priority.list_size()
				== m_torrent_file->num_files())
			{
				int num_files = m_torrent_file->num_files();
				m_file_priority.resize(num_files);
				for (int i = 0; i < num_files; ++i)
					m_file_priority[i] = file_priority.list_int_value_at(i, 1);
				// unallocated slots are assumed to be priority 1, so cut off any
				// trailing ones
				int end_range = num_files - 1;
				for (; end_range >= 0; --end_range) if (m_file_priority[end_range] != 1) break;
				m_file_priority.resize(end_range + 1);
         
				// initialize pad files to priority 0
				file_storage const& fs = m_torrent_file->files();
				for (int i = 0; i < (std::min)(fs.num_files(), end_range + 1); ++i)
				{
					if (!fs.pad_file_at(i)) continue;
					m_file_priority[i] = 0;
				}
			}

			update_piece_priorities();
		}

		bdecode_node trackers = rd.dict_find_list("trackers");
		if (trackers)
		{
			if (!m_merge_resume_trackers) m_trackers.clear();
			int tier = 0;
			for (int i = 0; i < trackers.list_size(); ++i)
			{
				bdecode_node tier_list = trackers.list_at(i);
				if (!tier_list || tier_list.type() != bdecode_node::list_t)
					continue;
				for (int j = 0; j < tier_list.list_size(); ++j)
				{
					announce_entry e(tier_list.list_string_value_at(j));
					if (std::find_if(m_trackers.begin(), m_trackers.end()
						, boost::bind(&announce_entry::url, _1) == e.url) != m_trackers.end())
						continue;
					e.tier = tier;
					e.fail_limit = 0;
					m_trackers.push_back(e);
				}
				++tier;
			}
			std::sort(m_trackers.begin(), m_trackers.end(), boost::bind(&announce_entry::tier, _1)
				< boost::bind(&announce_entry::tier, _2));

			if (settings().get_bool(settings_pack::prefer_udp_trackers))
				prioritize_udp_trackers();
		}

		bdecode_node url_list = rd.dict_find_list("url-list");
		if (url_list)
		{
			for (int i = 0; i < url_list.list_size(); ++i)
			{
				std::string url = url_list.list_string_value_at(i);
				if (url.empty()) continue;
				if (m_torrent_file->num_files() > 1 && url[url.size()-1] != '/') url += '/';
				add_web_seed(url, web_seed_entry::url_seed);
			}
		}

		bdecode_node httpseeds = rd.dict_find_list("httpseeds");
		if (httpseeds)
		{
			for (int i = 0; i < httpseeds.list_size(); ++i)
			{
				std::string url = httpseeds.list_string_value_at(i);
				if (url.empty()) continue;
				add_web_seed(url, web_seed_entry::http_seed);
			}
		}

		if (m_torrent_file->is_merkle_torrent())
		{
			bdecode_node mt = rd.dict_find_string("merkle tree");
			if (mt)
			{
				std::vector<sha1_hash> tree;
				tree.resize(m_torrent_file->merkle_tree().size());
				std::memcpy(&tree[0], mt.string_ptr()
					, (std::min)(mt.string_length(), int(tree.size()) * 20));
				if (mt.string_length() < int(tree.size()) * 20)
					std::memset(&tree[0] + mt.string_length() / 20, 0
						, tree.size() - mt.string_length() / 20);
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
				TORRENT_ASSERT(false);
			}
		}

		// updating some of the torrent state may have set need_save_resume_data.
		// clear it here since we've just restored the resume data we already
		// have. Nothing has changed from that state yet.
		m_need_save_resume_data = false;
	}

	boost::shared_ptr<const torrent_info> torrent::get_torrent_copy()
	{
		if (!m_torrent_file->is_valid()) return boost::shared_ptr<const torrent_info>();
		if (!need_loaded()) return boost::shared_ptr<const torrent_info>();

		return m_torrent_file;
	}
	
	void torrent::write_resume_data(entry& ret) const
	{
		using namespace libtorrent::detail; // for write_*_endpoint()
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;
		ret["libtorrent-version"] = LIBTORRENT_VERSION;

		ret["total_uploaded"] = m_total_uploaded;
		ret["total_downloaded"] = m_total_downloaded;

		ret["active_time"] = active_time();
		ret["finished_time"] = finished_time();
		ret["seeding_time"] = seeding_time();
		ret["last_seen_complete"] = m_last_seen_complete;

		ret["num_complete"] = m_complete;
		ret["num_incomplete"] = m_incomplete;
		ret["num_downloaded"] = m_downloaded;

		ret["sequential_download"] = m_sequential_download;

		ret["seed_mode"] = m_seed_mode;
		ret["super_seeding"] = m_super_seeding;

		ret["added_time"] = m_added_time;
		ret["completed_time"] = m_completed_time;

		ret["save_path"] = m_save_path;

		if (!m_url.empty()) ret["url"] = m_url;
		if (!m_uuid.empty()) ret["uuid"] = m_uuid;
		if (!m_source_feed_url.empty()) ret["feed"] = m_source_feed_url;
		
		const sha1_hash& info_hash = torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

		if (valid_metadata())
		{
			if (m_magnet_link || (m_save_resume_flags & torrent_handle::save_info_dict))
				ret["info"] = bdecode(&torrent_file().metadata()[0]
					, &torrent_file().metadata()[0] + torrent_file().metadata_size());
		}

		// blocks per piece
		int num_blocks_per_piece =
			static_cast<int>(torrent_file().piece_length()) / block_size();
		ret["blocks per piece"] = num_blocks_per_piece;

		if (m_torrent_file->is_merkle_torrent())
		{
			// we need to save the whole merkle hash tree
			// in order to resume
			std::string& tree_str = ret["merkle tree"].string();
			std::vector<sha1_hash> const& tree = m_torrent_file->merkle_tree();
			tree_str.resize(tree.size() * 20);
			std::memcpy(&tree_str[0], &tree[0], tree.size() * 20);
		}

		// if this torrent is a seed, we won't have a piece picker
		// if we don't have anything, we may also not have a picker
		// in either case; there will be no half-finished pieces.
		if (has_picker())
		{
			std::vector<piece_picker::downloading_piece> q
				= m_picker->get_download_queue();

			// unfinished pieces
			ret["unfinished"] = entry::list_type();
			entry::list_type& up = ret["unfinished"].list();

			// info for each unfinished piece
			for (std::vector<piece_picker::downloading_piece>::const_iterator i
				= q.begin(); i != q.end(); ++i)
			{
				if (i->finished == 0) continue;

				entry piece_struct(entry::dictionary_t);

				// the unfinished piece's index
				piece_struct["piece"] = i->index;

				std::string bitmask;
				const int num_bitmask_bytes
					= (std::max)(num_blocks_per_piece / 8, 1);

				piece_picker::block_info const* info = m_picker->blocks_for_piece(*i);
				for (int j = 0; j < num_bitmask_bytes; ++j)
				{
					unsigned char v = 0;
					int bits = (std::min)(num_blocks_per_piece - j*8, 8);
					for (int k = 0; k < bits; ++k)
						v |= (info[j*8+k].state == piece_picker::block_info::state_finished)
						? (1 << k) : 0;
					bitmask.append(1, v);
					TORRENT_ASSERT(bits == 8 || j == num_bitmask_bytes - 1);
				}
				piece_struct["bitmask"] = bitmask;
				// push the struct onto the unfinished-piece list
				up.push_back(piece_struct);
			}
		}

		// save trackers
		entry::list_type& tr_list = ret["trackers"].list();
		tr_list.push_back(entry::list_type());
		int tier = 0;
		for (std::vector<announce_entry>::const_iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			// don't save trackers we can't trust
			// TODO: 1 save the send_stats state instead of throwing them away
			// it may pose an issue when downgrading though
			if (i->send_stats == false) continue;
			if (i->tier == tier)
			{
				tr_list.back().list().push_back(i->url);
			}
			else
			{
				tr_list.push_back(entry::list_t);
				tr_list.back().list().push_back(i->url);
				tier = i->tier;
			}
		}

		// save web seeds
		if (!m_web_seeds.empty())
		{
			entry::list_type& url_list = ret["url-list"].list();
			entry::list_type& httpseed_list = ret["httpseeds"].list();
			for (std::list<web_seed_t>::const_iterator i = m_web_seeds.begin()
				, end(m_web_seeds.end()); i != end; ++i)
			{
				if (i->type == web_seed_entry::url_seed)
					url_list.push_back(i->url);
				else if (i->type == web_seed_entry::http_seed)
					httpseed_list.push_back(i->url);
			}
		}

		// write have bitmask
		// the pieces string has one byte per piece. Each
		// byte is a bitmask representing different properties
		// for the piece
		// bit 0: set if we have the piece
		// bit 1: set if we have verified the piece (in seed mode)
		entry::string_type& pieces = ret["pieces"].string();
		pieces.resize(m_torrent_file->num_pieces());
		if (!has_picker())
		{
			std::memset(&pieces[0], m_have_all, pieces.size());
		}
		else if (has_picker())
		{
			for (int i = 0, end(pieces.size()); i < end; ++i)
				pieces[i] = m_picker->have_piece(i) ? 1 : 0;
		}

		if (m_seed_mode)
		{
			TORRENT_ASSERT(m_verified.size() == pieces.size());
			TORRENT_ASSERT(m_verifying.size() == pieces.size());
			for (int i = 0, end(pieces.size()); i < end; ++i)
				pieces[i] |= m_verified[i] ? 2 : 0;
		}

		// write renamed files
		// TODO: 0 make this more generic to not just work if files have been
		// renamed, but also if they have been merged into a single file for instance.
		// using file_base
		if (&m_torrent_file->files() != &m_torrent_file->orig_files()
			&& m_torrent_file->files().num_files() == m_torrent_file->orig_files().num_files())
		{
			entry::list_type& fl = ret["mapped_files"].list();
			file_storage const& fs = m_torrent_file->files();
			for (int i = 0; i < fs.num_files(); ++i)
			{
				fl.push_back(fs.file_path(i));
			}
		}

		// write local peers

		std::back_insert_iterator<entry::string_type> peers(ret["peers"].string());
		std::back_insert_iterator<entry::string_type> banned_peers(ret["banned_peers"].string());
#if TORRENT_USE_IPV6
		std::back_insert_iterator<entry::string_type> peers6(ret["peers6"].string());
		std::back_insert_iterator<entry::string_type> banned_peers6(ret["banned_peers6"].string());
#endif

		int num_saved_peers = 0;

		std::vector<torrent_peer const*> deferred_peers;

		if (m_peer_list)
		{
			for (peer_list::const_iterator i = m_peer_list->begin_peer()
				, end(m_peer_list->end_peer()); i != end; ++i)
			{
				error_code ec;
				torrent_peer const* p = *i;
				address addr = p->address();
				if (p->banned)
				{
#if TORRENT_USE_IPV6
					if (addr.is_v6())
					{
						write_address(addr, banned_peers6);
						write_uint16(p->port, banned_peers6);
					}
					else
#endif
					{
						write_address(addr, banned_peers);
						write_uint16(p->port, banned_peers);
					}
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
					deferred_peers.push_back(p);
					continue;
				}

#if TORRENT_USE_IPV6
				if (addr.is_v6())
				{
					write_address(addr, peers6);
					write_uint16(p->port, peers6);
				}
				else
#endif
				{
					write_address(addr, peers);
					write_uint16(p->port, peers);
				}
				++num_saved_peers;
			}
		}

		// if we didn't save 100 peers, fill in with second choice peers
		if (num_saved_peers < 100)
		{
			std::random_shuffle(deferred_peers.begin(), deferred_peers.end());
			for (std::vector<torrent_peer const*>::const_iterator i = deferred_peers.begin()
				, end(deferred_peers.end()); i != end && num_saved_peers < 100; ++i)
			{
				torrent_peer const* p = *i;
				address addr = p->address();

#if TORRENT_USE_IPV6
				if (addr.is_v6())
				{
					write_address(addr, peers6);
					write_uint16(p->port, peers6);
				}
				else
#endif
				{
					write_address(addr, peers);
					write_uint16(p->port, peers);
				}
				++num_saved_peers;
			}
		}

		ret["upload_rate_limit"] = upload_limit();
		ret["download_rate_limit"] = download_limit();
		ret["max_connections"] = max_connections();
		ret["max_uploads"] = max_uploads();
		ret["paused"] = is_torrent_paused();
		ret["announce_to_dht"] = m_announce_to_dht;
		ret["announce_to_trackers"] = m_announce_to_trackers;
		ret["announce_to_lsd"] = m_announce_to_lsd;
		ret["auto_managed"] = m_auto_managed;

		// write piece priorities
		// but only if they are not set to the default
		if (has_picker())
		{
			bool default_prio = true;
			for (int i = 0, end(m_torrent_file->num_pieces()); i < end; ++i)
			{
				if (m_picker->piece_priority(i) == 1) continue;
				default_prio = false;
				break;
			}

			if (!default_prio)
			{
				entry::string_type& piece_priority = ret["piece_priority"].string();
				piece_priority.resize(m_torrent_file->num_pieces());

				for (int i = 0, end(piece_priority.size()); i < end; ++i)
					piece_priority[i] = m_picker->piece_priority(i);
			}
		}

		// write file priorities
		entry::list_type& file_priority = ret["file_priority"].list();
		file_priority.clear();
		for (int i = 0, end(m_file_priority.size()); i < end; ++i)
			file_priority.push_back(m_file_priority[i]);
	}

	void torrent::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		v.clear();
		if (!m_peer_list) return;

		v.reserve(m_peer_list->num_peers());
		for (peer_list::const_iterator i = m_peer_list->begin_peer();
			i != m_peer_list->end_peer(); ++i)
		{
			peer_list_entry e;
			e.ip = (*i)->ip();
			e.flags = (*i)->banned ? peer_list_entry::banned : 0;
			e.failcount = (*i)->failcount;
			e.source = (*i)->source;
			v.push_back(e);
		}
	}

	void torrent::get_peer_info(std::vector<peer_info>& v)
	{
		v.clear();
		for (peer_iterator i = begin();
			i != end(); ++i)
		{
			peer_connection* peer = *i;
			TORRENT_ASSERT(peer->m_in_use == 1337);

			// incoming peers that haven't finished the handshake should
			// not be included in this list
			if (peer->associated_torrent().expired()) continue;

			v.push_back(peer_info());
			peer_info& p = v.back();
			
			peer->get_peer_info(p);
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
			if (resolving_countries())
				resolve_peer_country(peer->self());
#endif
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

		const int blocks_per_piece = m_picker->blocks_in_piece(0);
		blk.resize(q.size() * blocks_per_piece);
		// for some weird reason valgrind claims these are uninitialized
		// unless it's zeroed out here (block_info has a construct that's
		// supposed to initialize it)
		if (!blk.empty())
			memset(&blk[0], 0, sizeof(blk[0]) * blk.size());

		int counter = 0;
		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin(); i != q.end(); ++i, ++counter)
		{
			partial_piece_info pi;
			pi.blocks_in_piece = p.blocks_in_piece(i->index);
			pi.finished = (int)i->finished;
			pi.writing = (int)i->writing;
			pi.requested = (int)i->requested;
			TORRENT_ASSERT(counter * blocks_per_piece + pi.blocks_in_piece <= int(blk.size()));
			pi.blocks = &blk[counter * blocks_per_piece];
			int piece_size = int(torrent_file().piece_size(i->index));
			piece_picker::block_info const* info = m_picker->blocks_for_piece(*i);
			for (int j = 0; j < pi.blocks_in_piece; ++j)
			{
				block_info& bi = pi.blocks[j];
				bi.state = info[j].state;
				bi.block_size = j < pi.blocks_in_piece - 1 ? block_size()
					: piece_size - (j * block_size());
				bool complete = bi.state == block_info::writing
					|| bi.state == block_info::finished;
				if (info[j].peer == 0)
				{
					bi.set_peer(tcp::endpoint());
					bi.bytes_progress = complete ? bi.block_size : 0;
				}
				else
				{
					torrent_peer* p = static_cast<torrent_peer*>(info[j].peer);
					TORRENT_ASSERT(p->in_use);
					if (p->connection)
					{
						peer_connection* peer = static_cast<peer_connection*>(p->connection);
						TORRENT_ASSERT(peer->m_in_use);
						bi.set_peer(peer->remote());
						if (bi.state == block_info::requested)
						{
							boost::optional<piece_block_progress> pbp
								= peer->downloading_piece_progress();
							if (pbp && pbp->piece_index == i->index && pbp->block_index == j)
							{
								bi.bytes_progress = pbp->bytes_downloaded;
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
						bi.set_peer(p->ip());
						bi.bytes_progress = complete ? bi.block_size : 0;
					}
				}

				pi.blocks[j].num_peers = info[j].num_peers;
			}
			pi.piece_index = i->index;
			queue->push_back(pi);
		}
	
	}
	
	bool torrent::connect_to_peer(torrent_peer* peerinfo, bool ignore_limit)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(peerinfo);
		TORRENT_ASSERT(peerinfo->connection == 0);

		if (m_abort) return false;

		peerinfo->last_connected = m_ses.session_time();
#ifdef TORRENT_DEBUG
		if (!settings().get_bool(settings_pack::allow_multiple_connections_per_ip))
		{
			// this asserts that we don't have duplicates in the peer_list's peer list
			peer_iterator i_ = std::find_if(m_connections.begin(), m_connections.end()
				, boost::bind(&peer_connection::remote, _1) == peerinfo->ip());
#if TORRENT_USE_I2P
			TORRENT_ASSERT(i_ == m_connections.end()
				|| (*i_)->type() != peer_connection::bittorrent_connection
				|| peerinfo->is_i2p_addr);
#else
			TORRENT_ASSERT(i_ == m_connections.end()
				|| (*i_)->type() != peer_connection::bittorrent_connection);
#endif
		}
#endif

		TORRENT_ASSERT(want_peers() || ignore_limit);
		TORRENT_ASSERT(m_ses.num_connections()
			< m_ses.settings().get_int(settings_pack::connections_limit) || ignore_limit);

		tcp::endpoint a(peerinfo->ip());
		TORRENT_ASSERT(!m_apply_ip_filter
			|| (m_ses.get_ip_filter().access(peerinfo->address()) & ip_filter::blocked) == 0);

		boost::shared_ptr<socket_type> s(new socket_type(m_ses.get_io_service()));

#if TORRENT_USE_I2P
		bool i2p = peerinfo->is_i2p_addr;
		if (i2p)
		{
			if (m_ses.i2p_proxy().hostname.empty())
			{
				// we have an i2p torrent, but we're not connected to an i2p
				// SAM proxy.
				if (alerts().should_post<i2p_alert>())
					alerts().post_alert(i2p_alert(error_code(errors::no_i2p_router
						, get_libtorrent_category())));
				return false;
			}

			bool ret = instantiate_connection(m_ses.get_io_service(), m_ses.i2p_proxy(), *s);
			(void)ret;
			TORRENT_ASSERT(ret);
			s->get<i2p_stream>()->set_destination(static_cast<i2p_peer*>(peerinfo)->destination);
			s->get<i2p_stream>()->set_command(i2p_stream::cmd_connect);
			s->get<i2p_stream>()->set_session_id(m_ses.i2p_session());
		}
		else
#endif
		{
			// this is where we determine if we open a regular TCP connection
			// or a uTP connection. If the utp_socket_manager pointer is not passed in
			// we'll instantiate a TCP connection
			utp_socket_manager* sm = 0;

			if (m_ses.settings().get_bool(settings_pack::enable_outgoing_utp)
				&& (!m_ses.settings().get_bool(settings_pack::enable_outgoing_tcp)
					|| peerinfo->supports_utp
					|| peerinfo->confirmed_supports_utp))
				sm = m_ses.utp_socket_manager();

			// don't make a TCP connection if it's disabled
			if (sm == 0 && !m_ses.settings().get_bool(settings_pack::enable_outgoing_tcp)) return false;

			void* userdata = 0;
#ifdef TORRENT_USE_OPENSSL
			if (is_ssl_torrent() && m_ses.settings().get_int(settings_pack::ssl_listen) != 0)
			{
				userdata = m_ssl_ctx.get();
			}
#endif

			bool ret = instantiate_connection(m_ses.get_io_service()
				, m_ses.proxy(), *s, userdata, sm, true);
			(void)ret;
			TORRENT_ASSERT(ret);

#if defined TORRENT_USE_OPENSSL && BOOST_VERSION >= 104700
			if (is_ssl_torrent())
			{
				// for ssl sockets, set the hostname
				std::string host_name = to_hex(m_torrent_file->info_hash().to_string());

#define CASE(t) case socket_type_int_impl<ssl_stream<t> >::value: \
	s->get<ssl_stream<t> >()->set_host_name(host_name); break;

				switch (s->type())
				{
					CASE(stream_socket)
					CASE(socks5_stream)
					CASE(http_stream)
					CASE(utp_stream)
					default: break;
				};
			}
#undef CASE
#endif
		}

		m_ses.setup_socket_buffers(*s);

		peer_connection_args pack;
		pack.ses = &m_ses;
		pack.sett = &m_ses.settings();
		pack.stats_counters = &m_ses.stats_counters();
		pack.allocator = &m_ses;
		pack.disk_thread = &m_ses.disk_thread();
		pack.ios = &m_ses.get_io_service();
		pack.tor = shared_from_this();
		pack.s = s;
		pack.endp = a;
		pack.peerinfo = peerinfo;

		boost::shared_ptr<peer_connection> c = boost::make_shared<bt_peer_connection>(
			boost::cref(pack), m_ses.get_peer_id());

		TORRENT_TRY
		{
#if TORRENT_USE_ASSERTS
			c->m_in_constructor = false;
#endif

	 		c->add_stat(boost::int64_t(peerinfo->prev_amount_download) << 10
				, boost::int64_t(peerinfo->prev_amount_upload) << 10);
	 		peerinfo->prev_amount_download = 0;
	 		peerinfo->prev_amount_upload = 0;

#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				TORRENT_TRY {
					boost::shared_ptr<peer_plugin> pp((*i)->new_connection(c.get()));
					if (pp) c->add_extension(pp);
				} TORRENT_CATCH (std::exception&) {}
			}
#endif

			// add the newly connected peer to this torrent's peer list
			sorted_insert(m_connections, boost::get_pointer(c));
			m_ses.insert_peer(c);
			need_policy();
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
		TORRENT_CATCH (std::exception&)
		{
			peer_iterator i = sorted_find(m_connections, boost::get_pointer(c));
			if (i != m_connections.end())
			{
				m_connections.erase(i);
				update_want_peers();
				update_want_tick();
			}
			c->disconnect(errors::no_error, op_bittorrent, 1);
			return false;
		}

		if (m_share_mode)
			recalc_share_mode();

		return peerinfo->connection;
	}

	bool torrent::set_metadata(char const* metadata_buf, int metadata_size)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_torrent_file->is_valid()) return false;

		hasher h;
		h.update(metadata_buf, metadata_size);
		sha1_hash info_hash = h.final();

		if (info_hash != m_torrent_file->info_hash())
		{
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().post_alert(metadata_failed_alert(get_handle()
					, error_code(errors::mismatching_info_hash, get_libtorrent_category())));
			}
			return false;
		}

		bdecode_node metadata;
		error_code ec;
		int ret = bdecode(metadata_buf, metadata_buf + metadata_size, metadata, ec);
		if (ret != 0 || !m_torrent_file->parse_info_section(metadata, ec, 0))
		{
			update_gauge();
			// this means the metadata is correct, since we
			// verified it against the info-hash, but we
			// failed to parse it. Pause the torrent
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().post_alert(metadata_failed_alert(get_handle(), ec));
			}
			set_error(errors::invalid_swarm_metadata, error_file_none);
			pause();
			return false;
		}

		update_gauge();

		if (m_ses.alerts().should_post<metadata_received_alert>())
		{
			m_ses.alerts().post_alert(metadata_received_alert(
				get_handle()));
		}

		// this makes the resume data "paused" and
		// "auto_managed" fields be ignored. If the paused
		// field is not ignored, the invariant check will fail
		// since we will be paused but without having disconnected
		// any of the peers.
		m_override_resume_data = true;

		// we have to initialize the torrent before we start
		// disconnecting redundant peers, otherwise we'll think
		// we're a seed, because we have all 0 pieces
		init();

		inc_stats_counter(counters::num_total_pieces_added
			, m_torrent_file->num_pieces());

		// disconnect redundant peers
		int idx = 0;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++idx)
		{
			if ((*i)->disconnect_if_redundant())
			{
				i = m_connections.begin() + idx;
				--idx;
			}
			else
			{
				++i;
			}
		}

		m_need_save_resume_data = true;

		return true;
	}

	bool connecting_time_compare(peer_connection const* lhs, peer_connection const* rhs)
	{
		bool lhs_connecting = lhs->is_connecting() && !lhs->is_disconnecting();
		bool rhs_connecting = rhs->is_connecting() && !rhs->is_disconnecting();
		if (lhs_connecting > rhs_connecting) return false;
		if (lhs_connecting < rhs_connecting) return true;

		// a lower value of connected_time means it's been waiting
		// longer. This is a less-than comparison, so if lhs has
		// waited longer than rhs, we should return false.
		return lhs->connected_time() > rhs->connected_time();
	}

	bool torrent::attach_peer(peer_connection* p)
	{
//		INVARIANT_CHECK;

#ifdef TORRENT_USE_OPENSSL
#if BOOST_VERSION >= 104700
		if (is_ssl_torrent())
		{
			// if this is an SSL torrent, don't allow non SSL peers on it
			boost::shared_ptr<socket_type> s = p->get_socket();

			//
#define SSL(t) socket_type_int_impl<ssl_stream<t> >::value: \
			ssl_conn = s->get<ssl_stream<t> >()->native_handle(); \
			break;

			SSL* ssl_conn = 0;

			switch (s->type())
			{
				case SSL(stream_socket)
				case SSL(socks5_stream)
				case SSL(http_stream)
				case SSL(utp_stream)
			};

#undef SSL

			if (ssl_conn == 0)
			{
				// don't allow non SSL peers on SSL torrents
				p->disconnect(errors::requires_ssl_connection, op_bittorrent);
				return false;
			}

			if (!m_ssl_ctx)
			{
				// we don't have a valid cert, don't accept any connection!
				p->disconnect(errors::invalid_ssl_cert, op_ssl_handshake);
				return false;
			}

			if (SSL_get_SSL_CTX(ssl_conn) != m_ssl_ctx->native_handle())
			{
				// if the SSL_CTX associated with this connection is
				// not the one belonging to this torrent, the SSL handshake
				// connected to one torrent, and the BitTorrent protocol
				// to a different one. This is probably an attempt to circumvent
				// access control. Don't allow it.
				p->disconnect(errors::invalid_ssl_cert, op_bittorrent);
				return false;
			}
		}
#else // BOOST_VERSION
		if (is_ssl_torrent())
		{
			p->disconnect(asio::error::operation_not_supported, op_bittorrent);
			return false;
		}
#endif
#else // TORRENT_USE_OPENSSL
		if (is_ssl_torrent())
		{
			// Don't accidentally allow seeding of SSL torrents, just
			// because libtorrent wasn't built with SSL support
			p->disconnect(errors::requires_ssl_connection, op_ssl_handshake);
			return false;
		}
#endif // TORRENT_USE_OPENSSL

		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(!p->is_outgoing());

		m_has_incoming = true;

		if (m_apply_ip_filter
			&& m_ses.get_ip_filter().access(p->remote().address()) & ip_filter::blocked)
		{
			if (m_ses.alerts().should_post<peer_blocked_alert>())
				m_ses.alerts().post_alert(peer_blocked_alert(get_handle()
					, p->remote().address(), peer_blocked_alert::ip_filter));
			p->disconnect(errors::banned_by_ip_filter, op_bittorrent);
			return false;
		}

		if ((m_state == torrent_status::checking_files
			|| m_state == torrent_status::checking_resume_data)
			&& valid_metadata())
		{
			p->disconnect(errors::torrent_not_ready, op_bittorrent);
			return false;
		}
		
		if (!m_ses.has_connection(p))
		{
			p->disconnect(errors::peer_not_constructed, op_bittorrent);
			return false;
		}

		if (m_ses.is_aborted())
		{
			p->disconnect(errors::session_closing, op_bittorrent);
			return false;
		}

		int connection_limit_factor = 0;
		for (int i = 0; i < p->num_classes(); ++i)
		{
			int pc = p->class_at(i);
			if (m_ses.peer_classes().at(pc) == NULL) continue;
			int f = m_ses.peer_classes().at(pc)->connection_limit_factor;
			if (connection_limit_factor < f) connection_limit_factor = f;
		}
		if (connection_limit_factor == 0) connection_limit_factor = 100;

		boost::uint64_t limit = boost::uint64_t(m_max_connections) * 100 / connection_limit_factor;

		bool maybe_replace_peer = false;

		if (m_connections.size() >= limit)
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
				// disconnect the peer that's been wating to establish a connection
				// the longest
				std::vector<peer_connection*>::iterator i = std::max_element(begin(), end()
					, &connecting_time_compare);

				if (i == end() || !(*i)->is_connecting() || (*i)->is_disconnecting())
				{
					// this seems odd, but we might as well handle it
					p->disconnect(errors::too_many_connections, op_bittorrent);
					return false;
				}
				(*i)->disconnect(errors::too_many_connections, op_bittorrent);
            
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

		TORRENT_TRY
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				boost::shared_ptr<peer_plugin> pp((*i)->new_connection(p));
				if (pp) p->add_extension(pp);
			}
#endif
			torrent_state st = get_policy_state();
			need_policy();
			if (!m_peer_list->new_connection(*p, m_ses.session_time(), &st))
			{
				peers_erased(st.erased);
#if defined TORRENT_LOGGING
				debug_log("CLOSING CONNECTION \"%s\" peer list full"
					, print_endpoint(p->remote()).c_str());
#endif
				p->disconnect(errors::too_many_connections, op_bittorrent);
				return false;
			}
			peers_erased(st.erased);
			update_want_peers();
		}
		TORRENT_CATCH (std::exception& e)
		{
			TORRENT_DECLARE_DUMMY(std::exception, e);
			(void)e;
#if defined TORRENT_LOGGING
			debug_log("CLOSING CONNECTION \"%s\" caught exception: %s"
				, print_endpoint(p->remote()).c_str(), e.what());
#endif
			p->disconnect(errors::no_error, op_bittorrent);
			return false;
		}
		TORRENT_ASSERT(sorted_find(m_connections, p) == m_connections.end());
		sorted_insert(m_connections, p);
		update_want_peers();
		update_want_tick();

		if (p->peer_info_struct() && p->peer_info_struct()->seed)
		{
			TORRENT_ASSERT(m_num_seeds < 0xffff);
			++m_num_seeds;
		}

#if defined TORRENT_LOGGING
		debug_log("incoming peer (%d)", int(m_connections.size()));
#endif

#ifdef TORRENT_DEBUG
		error_code ec;
		TORRENT_ASSERT(p->remote() == p->get_socket()->remote_endpoint(ec) || ec);
#endif

		TORRENT_ASSERT(p->peer_info_struct() != NULL);

		// we need to do this after we've added the peer to the peer_list
		// since that's when the peer is assigned its peer_info object,
		// which holds the rank
		if (maybe_replace_peer)
		{
			// now, find the lowest rank peer and disconnect that
			// if it's lower rank than the incoming connection
			peer_connection* peer = find_lowest_ranking_peer();

			// TODO: 2 if peer is a really good peer, maybe we shouldn't disconnect it
			if (peer && peer->peer_rank() < p->peer_rank())
			{
				peer->disconnect(errors::too_many_connections, op_bittorrent);
				p->peer_disconnected_other();
			}
			else
			{
				p->disconnect(errors::too_many_connections, op_bittorrent);
				// we have to do this here because from the peer's point of
				// it wasn't really attached to the torrent, but we do need
				// to let peer_list know we're removing it
				remove_peer(p);
				return false;
			}
		}

#if TORRENT_USE_INVARIANT_CHECKS
		if (m_peer_list) m_peer_list->check_invariant();
#endif

		if (m_share_mode)
			recalc_share_mode();

		return true;
	}

	bool torrent::want_tick() const
	{
		if (m_abort) return false;

		if (!m_connections.empty()) return true;

		// there's a deferred storage tick waiting
		// to happen
		if (m_storage_tick) return true;

		// we might want to connect web seeds
		if (!is_finished() && !m_web_seeds.empty() && m_files_checked)
			return true;

		if (m_stat.low_pass_upload_rate() > 0 || m_stat.low_pass_download_rate() > 0)
			return true;

		return false;
	}

	void torrent::update_want_tick()
	{
		update_list(aux::session_interface::torrent_want_tick, want_tick());
	}

	// returns true if this torrent is interested in connecting to more peers
	bool torrent::want_peers() const
	{
		// if all our connection slots are taken, we can't connect to more
		if (m_connections.size() >= m_max_connections) return false;

		// if we're paused, obviously we're not connecting to peers
		if (is_paused() || m_abort) return false;

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
		if (!m_ses.settings().get_bool(settings_pack::seeding_outgoing_connections)
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
			, !m_allow_peers && m_auto_managed && !m_abort);
	}

	void torrent::update_list(int list, bool in)
	{
		link& l = m_links[list];
		std::vector<torrent*>& v = m_ses.torrent_list(list);
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
	}

	void torrent::disconnect_all(error_code const& ec, operation_t op)
	{
// doesn't work with the !m_allow_peers -> m_num_peers == 0 condition
//		INVARIANT_CHECK;

		while (!m_connections.empty())
		{
			peer_connection* p = *m_connections.begin();
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

#if TORRENT_USE_ASSERTS
			std::size_t size = m_connections.size();
#endif
			if (p->is_disconnecting())
				m_connections.erase(m_connections.begin());
			else
				p->disconnect(ec, op);
			TORRENT_ASSERT(m_connections.size() <= size);
		}

		update_want_peers();
		update_want_tick();
	}

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
		boost::int64_t lhs_transferred = lhs->statistics().total_payload_download();
		boost::int64_t rhs_transferred = rhs->statistics().total_payload_download();

		time_point now = aux::time_now();
		boost::int64_t lhs_time_connected = total_seconds(now - lhs->connected_time());
		boost::int64_t rhs_time_connected = total_seconds(now - rhs->connected_time());

		lhs_transferred /= lhs_time_connected + 1;
		rhs_transferred /= (rhs_time_connected + 1);
		if (lhs_transferred != rhs_transferred)	
			return lhs_transferred < rhs_transferred;

		// prefer to disconnect peers that chokes us
		if (lhs->is_choked() != rhs->is_choked())
			return lhs->is_choked();

		return lhs->last_received() < rhs->last_received();
	}

	int torrent::disconnect_peers(int num, error_code const& ec)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
		}
#endif
		int ret = 0;
		while (ret < num && !m_connections.empty())
		{
			peer_iterator i = std::min_element(
				m_connections.begin(), m_connections.end(), compare_disconnect_peer);

			peer_connection* p = *i;
			++ret;
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
#if TORRENT_USE_ASSERTS
			int num_conns = m_connections.size();
#endif
			p->disconnect(ec, op_bittorrent);
			TORRENT_ASSERT(int(m_connections.size()) == num_conns - 1);
		}

		return ret;
	}

	// called when torrent is finished (all interesting
	// pieces have been downloaded)
	void torrent::finished()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_finished());

		set_state(torrent_status::finished);
		set_queue_position(-1);

		m_became_finished = m_ses.session_time();

		// we have to call completed() before we start
		// disconnecting peers, since there's an assert
		// to make sure we're cleared the piece picker
		if (is_seed()) completed();

		send_upload_only();

		state_updated();

		if (m_completed_time == 0)
			m_completed_time = time(0);

		// disconnect all seeds
		if (settings().get_bool(settings_pack::close_redundant_connections))
		{
			// TODO: 1 should disconnect all peers that have the pieces we have
			// not just seeds. It would be pretty expensive to check all pieces
			// for all peers though
			std::vector<peer_connection*> seeds;
			for (peer_iterator i = m_connections.begin();
				i != m_connections.end(); ++i)
			{
				peer_connection* p = *i;
				TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
				if (p->upload_only())
				{
#if defined TORRENT_LOGGING
					p->peer_log("*** SEED, CLOSING CONNECTION");
#endif
					seeds.push_back(p);
				}
			}
			std::for_each(seeds.begin(), seeds.end()
				, boost::bind(&peer_connection::disconnect, _1, errors::torrent_finished
				, op_bittorrent, 0));
		}

		if (m_abort) return;

		update_want_peers();

		TORRENT_ASSERT(m_storage);

		// we need to keep the object alive during this operation
		inc_refcount("release_files");
		m_ses.disk_thread().async_release_files(m_storage.get()
			, boost::bind(&torrent::on_cache_flushed, shared_from_this(), _1));
		
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
	
		if (m_state == torrent_status::checking_resume_data
			|| m_state == torrent_status::checking_files
			|| m_state == torrent_status::allocating)
		{
#if defined TORRENT_LOGGING
		debug_log("*** RESUME_DOWNLOAD [ skipping, state: %d ]"
			, int(m_state));
#endif
			return;
		}

		TORRENT_ASSERT(!is_finished());
		set_state(torrent_status::downloading);
		set_queue_position((std::numeric_limits<int>::max)());

		m_completed_time = 0;

#if defined TORRENT_LOGGING
		debug_log("*** RESUME_DOWNLOAD");
#endif
		send_upload_only();
		update_want_tick();
	}

	void torrent::maybe_done_flushing()
	{
		if (!has_picker()) return;

		// when we're suggesting read cache pieces, we
		// still need the piece picker, to keep track
		// of availability counts for pieces
		if (m_picker->is_seeding()
			&& settings().get_int(settings_pack::suggest_mode) != settings_pack::suggest_read_cache)
		{
			// no need for the piece picker anymore
			m_picker.reset();
			m_have_all = true;
			update_gauge();
		}
	}

	// called when torrent is complete. i.e. all pieces downloaded
	// not necessarily flushed to disk
	void torrent::completed()
	{
		maybe_done_flushing();

		set_state(torrent_status::seeding);
		m_became_seed = m_ses.session_time();

		// no need for this anymore
		std::vector<boost::uint64_t>().swap(m_file_progress);
		if (!m_announcing) return;

		time_point now = aux::time_now();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (i->complete_sent) continue;
			i->next_announce = now;
			i->min_announce = now;
		}
		announce_with_tracker();
	}

	// this will move the tracker with the given index
	// to a prioritized position in the list (move it towards
	// the begining) and return the new index to the tracker.
	int torrent::prioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_trackers.size()));
		if (index >= (int)m_trackers.size()) return -1;

		while (index > 0 && m_trackers[index].tier == m_trackers[index-1].tier)
		{
			using std::swap;
			swap(m_trackers[index], m_trackers[index-1]);
			if (m_last_working_tracker == index) --m_last_working_tracker;
			else if (m_last_working_tracker == index - 1) ++m_last_working_tracker;
			--index;
		}
		return index;
	}

	int torrent::deprioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_trackers.size()));
		if (index >= (int)m_trackers.size()) return -1;

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
#if defined TORRENT_LOGGING
			debug_log("files_checked(), paused");
#endif
			return;
		}

		// we might be finished already, in which case we should
		// not switch to downloading mode. If all files are
		// filtered, we're finished when we start.
		if (m_state != torrent_status::finished
			&& m_state != torrent_status::seeding)
			set_state(torrent_status::downloading);

		INVARIANT_CHECK;

		if (m_ses.alerts().should_post<torrent_checked_alert>())
		{
			m_ses.alerts().post_alert(torrent_checked_alert(
				get_handle()));
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
			// turn off super seeding if we're not a seed
			if (m_super_seeding)
			{
				m_super_seeding = false;
				m_need_save_resume_data = true;
			}

			// if we just finished checking and we're not a seed, we are
			// likely to be unpaused
			m_ses.trigger_auto_manage();

			if (is_finished() && m_state != torrent_status::finished)
				finished();
		}
		else
		{
			for (std::vector<announce_entry>::iterator i = m_trackers.begin()
				, end(m_trackers.end()); i != end; ++i)
				i->complete_sent = true;

			if (m_state != torrent_status::finished
				&& m_state != torrent_status::seeding)
				finished();
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_files_checked();
			} TORRENT_CATCH (std::exception&) {}
		}
#endif

		m_connections_initialized = true;
		m_files_checked = true;

		update_want_tick();

		for (torrent::peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* pc = *i;
			++i;

			// all peer connections have to initialize themselves now that the metadata
			// is available
			if (!m_connections_initialized)
			{
				if (pc->is_disconnecting()) continue;
				pc->on_metadata_impl();
				if (pc->is_disconnecting()) continue;
				pc->init();
			}

#if defined TORRENT_LOGGING
			pc->peer_log("*** ON_FILES_CHECKED");
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

	std::string torrent::save_path() const
	{
		return m_save_path;
	}

	void torrent::rename_file(int index, std::string const& name)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_files());

		// stoage may be NULL during shutdown
		if (!m_storage.get())
		{
			if (alerts().should_post<file_rename_failed_alert>())
				alerts().post_alert(file_rename_failed_alert(get_handle()
					, index, error_code(errors::session_is_closing
						, get_libtorrent_category())));
			return;
		}

		inc_refcount("rename_file");
		m_ses.disk_thread().async_rename_file(m_storage.get(), index, name
			, boost::bind(&torrent::on_file_renamed, shared_from_this(), _1));
		return;
	}

	void torrent::move_storage(std::string const& save_path, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_abort)
		{
			if (alerts().should_post<storage_moved_failed_alert>())
				alerts().post_alert(storage_moved_failed_alert(get_handle(), boost::asio::error::operation_aborted
					, "", ""));
			return;
		}

		// storage may be NULL during shutdown
		if (m_storage.get())
		{
#if TORRENT_USE_UNC_PATHS
			std::string path = canonicalize_path(save_path);
#else
			std::string const& path = save_path;
#endif
			inc_refcount("move_storage");
			m_ses.disk_thread().async_move_storage(m_storage.get(), path, flags
				, boost::bind(&torrent::on_storage_moved, shared_from_this(), _1));
			m_moving_storage = true;
		}
		else
		{
#if TORRENT_USE_UNC_PATHS
			m_save_path = canonicalize_path(save_path);
#else

			m_save_path = save_path;
#endif
			m_need_save_resume_data = true;

			if (alerts().should_post<storage_moved_alert>())
			{
				alerts().post_alert(storage_moved_alert(get_handle(), m_save_path));
			}
		}
	}

	void torrent::on_storage_moved(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());

		m_moving_storage = false;
		dec_refcount("move_storage");
		if (j->ret == piece_manager::no_error || j->ret == piece_manager::need_full_check)
		{
			if (alerts().should_post<storage_moved_alert>())
				alerts().post_alert(storage_moved_alert(get_handle(), j->buffer));
			m_save_path = j->buffer;
			m_need_save_resume_data = true;
			if (j->ret == piece_manager::need_full_check)
				force_recheck();
		}
		else
		{
			if (alerts().should_post<storage_moved_failed_alert>())
				alerts().post_alert(storage_moved_failed_alert(get_handle(), j->error.ec
					, resolve_filename(j->error.file), j->error.operation_str()));
		}
	}

	piece_manager& torrent::storage()
	{
		TORRENT_ASSERT(m_storage.get());
		return *m_storage;
	}


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
		TORRENT_ASSERT(current_stats_state() == m_current_gauge_state + counters::num_checking_torrents
			|| m_current_gauge_state == no_gauge_state);

		for (std::vector<time_critical_piece>::const_iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			TORRENT_ASSERT(!is_seed());
			TORRENT_ASSERT(!has_picker() || !m_picker->have_piece(i->piece));
		}

		switch (current_stats_state())
		{
			case counters::num_error_torrents: TORRENT_ASSERT(has_error()); break;
			case counters::num_checking_torrents: 
#ifdef TORRENT_NO_DEPRECATE
				TORRENT_ASSERT(state() == torrent_status::checking_files);
#else
				TORRENT_ASSERT(state() == torrent_status::checking_files
					|| state() == torrent_status::queued_for_checking);
#endif
				break;
			case counters::num_seeding_torrents: TORRENT_ASSERT(is_seed()); break;
			case counters::num_upload_only_torrents: TORRENT_ASSERT(is_upload_only()); break;
			case counters::num_stopped_torrents: TORRENT_ASSERT(!is_auto_managed()
				&& (!m_allow_peers || m_graceful_pause_mode));
				break;
			case counters::num_queued_seeding_torrents:
				TORRENT_ASSERT((!m_allow_peers || m_graceful_pause_mode) && is_seed()); break;
		}

		if (m_torrent_file)
		{
			TORRENT_ASSERT(m_info_hash == m_torrent_file->info_hash());
		}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		for (int i = 0; i < aux::session_interface::num_torrent_lists; ++i)
		{
			if (!m_links[i].in_list()) continue;
			int index = m_links[i].index;

			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_ses.torrent_list(i).size()));
		}
#endif

		if (!is_loaded()) return;

		TORRENT_ASSERT(want_peers_download() == m_links[aux::session_interface::torrent_want_peers_download].in_list());
		TORRENT_ASSERT(want_peers_finished() == m_links[aux::session_interface::torrent_want_peers_finished].in_list());
		TORRENT_ASSERT(want_tick() == m_links[aux::session_interface::torrent_want_tick].in_list());
		TORRENT_ASSERT((!m_allow_peers && m_auto_managed) == m_links[aux::session_interface::torrent_want_scrape].in_list());

		TORRENT_ASSERT(is_single_thread());
		// this fires during disconnecting peers
//		if (is_paused()) TORRENT_ASSERT(num_peers() == 0 || m_graceful_pause_mode);

		TORRENT_ASSERT(!m_resume_data || m_resume_data->node.type() == bdecode_node::dict_t
			|| m_resume_data->node.type() == bdecode_node::none_t);

		int seeds = 0;
		int num_uploads = 0;
		std::map<piece_block, int> num_requests;
		for (const_peer_iterator i = this->begin(); i != this->end(); ++i)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
#endif
			peer_connection const& p = *(*i);

			if (p.peer_info_struct() && p.peer_info_struct()->seed)
				++seeds;

			for (std::vector<pending_block>::const_iterator i = p.request_queue().begin()
				, end(p.request_queue().end()); i != end; ++i)
				if (!i->not_wanted && !i->timed_out) ++num_requests[i->block];
			for (std::vector<pending_block>::const_iterator i = p.download_queue().begin()
				, end(p.download_queue().end()); i != end; ++i)
				if (!i->not_wanted && !i->timed_out) ++num_requests[i->block];
			if (!p.is_choked() && !p.ignore_unchoke_slots()) ++num_uploads;
			torrent* associated_torrent = p.associated_torrent().lock().get();
			if (associated_torrent != this && associated_torrent != 0)
				TORRENT_ASSERT(false);
		}
		TORRENT_ASSERT(num_uploads == int(m_num_uploads));
		TORRENT_ASSERT(seeds == int(m_num_seeds));

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
						fprintf(stderr, "picker count discrepancy: "
							"picker: %d != peerlist: %d\n", picker_count, count);

						for (const_peer_iterator i = this->begin(); i != this->end(); ++i)
						{
							peer_connection const& p = *(*i);
							fprintf(stderr, "peer: %s\n", print_endpoint(p.remote()).c_str());
							for (std::vector<pending_block>::const_iterator i = p.request_queue().begin()
								, end(p.request_queue().end()); i != end; ++i)
							{
								fprintf(stderr, "  rq: (%d, %d) %s %s %s\n", i->block.piece_index
									, i->block.block_index, i->not_wanted ? "not-wanted" : ""
									, i->timed_out ? "timed-out" : "", i->busy ? "busy": "");
							}
							for (std::vector<pending_block>::const_iterator i = p.download_queue().begin()
								, end(p.download_queue().end()); i != end; ++i)
							{
								fprintf(stderr, "  dq: (%d, %d) %s %s %s\n", i->block.piece_index
									, i->block.block_index, i->not_wanted ? "not-wanted" : ""
									, i->timed_out ? "timed-out" : "", i->busy ? "busy": "");
							}
						}
						TORRENT_ASSERT(false);
					}
				}
			}
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
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
		if (m_peer_list && m_peer_list->begin_peer() != m_peer_list->end_peer())
		{
			peer_list::const_iterator i = m_peer_list->begin_peer();
			peer_list::const_iterator prev = i++;
			peer_list::const_iterator end(m_peer_list->end_peer());
			peer_address_compare cmp;
			for (; i != end; ++i, ++prev)
			{
				TORRENT_ASSERT(!cmp(*i, *prev));
			}
		}
#endif

		boost::int64_t total_done = quantized_bytes_done();
		if (m_torrent_file->is_valid())
		{
			if (is_seed())
				TORRENT_ASSERT(total_done == m_torrent_file->total_size());
			else
				TORRENT_ASSERT(total_done != m_torrent_file->total_size() || !m_files_checked);

			TORRENT_ASSERT(block_size() <= m_torrent_file->piece_length());
		}
		else
		{
			TORRENT_ASSERT(total_done == 0);
		}
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

		if (!m_file_progress.empty())
		{
			for (std::vector<boost::uint64_t>::const_iterator i = m_file_progress.begin()
				, end(m_file_progress.end()); i != end; ++i)
			{
				int index = i - m_file_progress.begin();
				TORRENT_ASSERT(*i <= m_torrent_file->files().file_size(index));
			}
		}
	}
#endif

	void torrent::set_sequential_download(bool sd)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_sequential_download == sd) return;
		m_sequential_download = sd;

		m_need_save_resume_data = true;

		state_updated();
	}

	void torrent::queue_up()
	{
		set_queue_position(queue_position() == 0
			? queue_position() : queue_position() - 1);
	}

	void torrent::queue_down()
	{
		set_queue_position(queue_position() + 1);
	}

	void torrent::set_queue_position(int p)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT((p == -1) == is_finished()
			|| (!m_auto_managed && p == -1)
			|| (m_abort && p == -1));
		if (is_finished() && p != -1) return;
		if (p == m_sequence_number) return;

		TORRENT_ASSERT(p >= -1);

		state_updated();

		m_ses.set_queue_position(this, p);
	}

	void torrent::set_max_uploads(int limit, bool state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (1<<24)-1;
		if (m_max_uploads != limit && state_update) state_updated();
		m_max_uploads = limit;

		if (state_update)
			m_need_save_resume_data = true;
	}

	void torrent::set_max_connections(int limit, bool state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (1<<24)-1;
		if (m_max_connections != limit && state_update) state_updated();
		m_max_connections = limit;
		update_want_peers();

		if (num_peers() > int(m_max_connections))
		{
			disconnect_peers(num_peers() - m_max_connections
				, error_code(errors::too_many_connections, get_libtorrent_category()));
		}

		if (state_update)
			m_need_save_resume_data = true;
	}

	void torrent::set_upload_limit(int limit)
	{
		set_limit_impl(limit, peer_connection::upload_channel);
		m_need_save_resume_data = true;
	}

	void torrent::set_download_limit(int limit)
	{
		set_limit_impl(limit, peer_connection::download_channel);
		m_need_save_resume_data = true;
	}

	void torrent::set_limit_impl(int limit, int channel, bool state_update)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = 0;

		if (m_peer_class == 0 && limit == 0) return;

		if (m_peer_class == 0)
			setup_peer_class();

		struct peer_class* tpc = m_ses.peer_classes().at(m_peer_class);
		TORRENT_ASSERT(tpc);
		if (tpc->channel[channel].throttle() != limit && state_update)
			state_updated();
		tpc->channel[channel].throttle(limit);
	}

	void torrent::setup_peer_class()
	{
		TORRENT_ASSERT(m_peer_class == 0);
		m_peer_class = m_ses.peer_classes().new_peer_class(name());
		add_class(m_ses.peer_classes(), m_peer_class);
	}

	int torrent::limit_impl(int channel) const
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_peer_class == 0) return -1;
		int limit = m_ses.peer_classes().at(m_peer_class)->channel[channel].throttle();
		if (limit == (std::numeric_limits<int>::max)()) limit = -1;
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

	bool torrent::delete_files()
	{
		TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_LOGGING
		log_to_all_peers("DELETING FILES IN TORRENT");
#endif

		disconnect_all(errors::torrent_removed, op_bittorrent);
		stop_announcing();

		// storage may be NULL during shutdown
		if (m_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			inc_refcount("delete_files");
			m_ses.disk_thread().async_delete_files(m_storage.get()
				, boost::bind(&torrent::on_files_deleted, shared_from_this(), _1));
			m_deleted = true;
			return true;
		}
		return false;
	}

	void torrent::clear_error()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_error) return;
		bool checking_files = should_check_files();
		m_ses.trigger_auto_manage();
		m_error = error_code();
		m_error_file = error_file_none;

		update_gauge();
		state_updated();

		// if we haven't downloaded the metadata from m_url, try again
		if (!m_url.empty() && !m_torrent_file->is_valid())
		{
			start_download_url();
			return;
		}
		// if the error happened during initialization, try again now
		if (!m_connections_initialized && valid_metadata()) init();
		if (!checking_files && should_check_files())
			start_checking();
	}
	std::string torrent::resolve_filename(int file) const
	{
		if (file == error_file_none) return "";
		if (file == error_file_url) return m_url;
		if (file == error_file_ssl_ctx) return "SSL Context";
		if (file == error_file_metadata) return "metadata (from user load function)";

		if (m_storage && file >= 0)
		{
			file_storage const& st = m_torrent_file->files();
			return combine_path(m_save_path, st.file_path(file));
		}
		else
		{
			return m_save_path;
		}
	}

	void torrent::set_error(error_code const& ec, int error_file)
	{
		TORRENT_ASSERT(is_single_thread());
		m_error = ec;
		m_error_file = error_file;

		update_gauge();

		if (alerts().should_post<torrent_error_alert>())
			alerts().post_alert(torrent_error_alert(get_handle(), ec, resolve_filename(error_file)));

#if defined TORRENT_LOGGING
		if (ec)
		{
			char buf[1024];
			snprintf(buf, sizeof(buf), "TORRENT ERROR: %s: %s", ec.message().c_str()
				, resolve_filename(error_file).c_str());
			log_to_all_peers(buf);
		}
#endif

		state_updated();
	}

	void torrent::auto_managed(bool a)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_auto_managed == a) return;
		bool checking_files = should_check_files();
		m_auto_managed = a;
		update_gauge();
		update_want_scrape();

		state_updated();

		// we need to save this new state as well
		m_need_save_resume_data = true;

		// recalculate which torrents should be
		// paused
		m_ses.trigger_auto_manage();

		if (!checking_files && should_check_files())
		{
			start_checking();
		}
	}

	int clamped_subtract(int a, int b)
	{
		if (a < b) return 0;
		return a - b;
	}

	// this is called every time the session timer takes a step back. Since the
	// session time is meant to fit in 16 bits, it only covers a range of
	// about 18 hours. This means every few hours the whole epoch of this
	// clock is shifted forward. All timestamp in this clock must then be
	// shifted backwards to remain the same. Anything that's shifted back
	// beyond the new epoch is clamped to 0 (to represent the oldest timestamp
	// currently representable by the session_time)
	void torrent::step_session_time(int seconds)
	{
		if (m_peer_list)
		{
			for (peer_list::iterator j = m_peer_list->begin_peer()
				, end(m_peer_list->end_peer()); j != end; ++j)
			{
				torrent_peer* pe = *j;

				pe->last_optimistically_unchoked
					= clamped_subtract(pe->last_optimistically_unchoked, seconds);
				pe->last_connected = clamped_subtract(pe->last_connected, seconds);
			}
		}

		if (m_started < seconds)
		{
			// the started time just got shifted out of the valid window of
			// session time. Record this "lost time" by incrementing the
			// counters that are supposed to keep track of the total time we've
			// been in certain states
			int lost_seconds = m_started - seconds;
			if (!is_paused())
				m_active_time += lost_seconds;

			if (is_seed())
				m_seeding_time += lost_seconds;

			if (is_finished())
				m_finished_time += lost_seconds;
		}

		m_started = clamped_subtract(m_started, seconds);

		m_last_upload = clamped_subtract(m_last_upload, seconds);
		m_last_download = clamped_subtract(m_last_download, seconds);
		m_last_scrape = clamped_subtract(m_last_scrape, seconds);
		m_last_saved_resume = clamped_subtract(m_last_saved_resume, seconds);
		m_upload_mode_time = clamped_subtract(m_upload_mode_time, seconds);
	}

	// the higher seed rank, the more important to seed
	int torrent::seed_rank(aux::session_settings const& s) const
	{
		TORRENT_ASSERT(is_single_thread());
		enum flags
		{
			seed_ratio_not_met = 0x40000000,
			no_seeds = 0x20000000,
			recently_started = 0x10000000,
			prio_mask = 0x0fffffff
		};

		if (!is_finished()) return 0;

		int scale = 1000;
		if (!is_seed()) scale = 500;

		int ret = 0;

		boost::int64_t fin_time = finished_time();
		boost::int64_t download_time = int(active_time()) - fin_time;

		// if we haven't yet met the seed limits, set the seed_ratio_not_met
		// flag. That will make this seed prioritized
		// downloaded may be 0 if the torrent is 0-sized
		boost::int64_t downloaded = (std::max)(m_total_downloaded, m_torrent_file->total_size());
		if (fin_time < s.get_int(settings_pack::seed_time_limit)
			&& (download_time > 1
				&& fin_time * 100 / download_time < s.get_int(settings_pack::seed_time_ratio_limit))
			&& downloaded > 0
			&& m_total_uploaded * 100 / downloaded < s.get_int(settings_pack::share_ratio_limit))
			ret |= seed_ratio_not_met;

		// if this torrent is running, and it was started less
		// than 30 minutes ago, give it priority, to avoid oscillation
		if (!is_paused() && (m_ses.session_time() - m_started) < 30 * 60)
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
	void torrent::save_resume_data(int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
	
		if (!valid_metadata())
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle()
				, errors::no_metadata));
			return;
		}

		if (!m_storage.get())
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle()
				, errors::destructing_torrent));
			return;
		}

		if ((flags & torrent_handle::only_if_modified) && !m_need_save_resume_data)
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle()
				, errors::resume_data_not_modified));
			return;
		}

		m_need_save_resume_data = false;
		m_last_saved_resume = m_ses.session_time();
		m_save_resume_flags = boost::uint8_t(flags);
		state_updated();

		TORRENT_ASSERT(m_storage);
		if (m_state == torrent_status::checking_files
			|| m_state == torrent_status::checking_resume_data)
		{
			if (!need_loaded())
			{
				alerts().post_alert(save_resume_data_failed_alert(get_handle()
						, m_error));
				return;
			}

			boost::shared_ptr<entry> rd(new entry);
			write_resume_data(*rd);
			alerts().post_alert(save_resume_data_alert(rd, get_handle()));
			return;
		}

		// storage may be NULL during shutdown
		if ((flags & torrent_handle::flush_disk_cache) && m_storage)
			m_ses.disk_thread().async_release_files(m_storage.get());

		m_ses.queue_async_resume_data(shared_from_this());
	}

	bool torrent::do_async_save_resume_data()
	{
		if (!need_loaded())
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle(), m_error));
			return false;
		}
		// storage may be NULL during shutdown
		if (!m_storage)
		{
			TORRENT_ASSERT(m_abort);
			alerts().post_alert(save_resume_data_failed_alert(get_handle(), boost::asio::error::operation_aborted));
			return false;
		}
		inc_refcount("save_resume");
		m_ses.disk_thread().async_save_resume_data(m_storage.get()
			, boost::bind(&torrent::on_save_resume_data, shared_from_this(), _1));
		return true;
	}
	
	bool torrent::should_check_files() const
	{
		TORRENT_ASSERT(is_single_thread());
		// #error should m_allow_peers really affect checking?
		return m_state == torrent_status::checking_files
			&& m_allow_peers
			&& !has_error()
			&& !m_abort
			&& !m_graceful_pause_mode
			&& !m_ses.is_paused();
	}

	void torrent::flush_cache()
	{
		TORRENT_ASSERT(is_single_thread());

		// storage may be NULL during shutdown
		if (!m_storage)
		{
			TORRENT_ASSERT(m_abort);
			return;
		}
		inc_refcount("release_files");
		m_ses.disk_thread().async_release_files(m_storage.get()
			, boost::bind(&torrent::on_cache_flushed, shared_from_this(), _1));
	}

	void torrent::on_cache_flushed(disk_io_job const* j)
	{
		dec_refcount("release_files");
		TORRENT_ASSERT(is_single_thread());

		if (m_ses.is_aborted()) return;

		if (alerts().should_post<cache_flushed_alert>())
			alerts().post_alert(cache_flushed_alert(get_handle()));
	}

	bool torrent::is_paused() const
	{
		return !m_allow_peers || m_ses.is_paused() || m_graceful_pause_mode;
	}

	void torrent::pause(bool graceful)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (!m_allow_peers) return;
		if (!graceful) set_allow_peers(false);

		m_announce_to_dht = false;
		m_announce_to_trackers = false;
		m_announce_to_lsd = false;
		update_gauge();

		update_want_peers();
		update_want_scrape();

		// we need to save this new state
		m_need_save_resume_data = true;
		state_updated();

		bool prev_graceful = m_graceful_pause_mode;
		m_graceful_pause_mode = graceful;
		update_gauge();

		if (!m_ses.is_paused() || (prev_graceful && !m_graceful_pause_mode))
		{
			do_pause();
			// if this torrent was just paused
			// we might have to resume some other auto-managed torrent
			m_ses.trigger_auto_manage();
		}
	}

	void torrent::do_pause()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!is_paused()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				if ((*i)->on_pause()) return;
			} TORRENT_CATCH (std::exception&) {}
		}
#endif

		m_need_connect_boost = true;
		m_inactive = false;

		m_active_time += m_ses.session_time() - m_started;

		if (is_seed())
			m_seeding_time += m_ses.session_time() - m_became_seed;

		if (is_finished())
			m_finished_time += m_ses.session_time() - m_became_finished;

		state_updated();
		update_want_peers();
		update_want_scrape();

#if defined TORRENT_LOGGING
		log_to_all_peers("PAUSING TORRENT");
#endif

		// when checking and being paused in graceful pause mode, we
		// post the paused alert when the last outstanding disk job completes
		if (m_state == torrent_status::checking_files)
		{
			if (m_checking_piece == m_num_checked_pieces)
			{
				if (alerts().should_post<torrent_paused_alert>())
					alerts().post_alert(torrent_paused_alert(get_handle()));
			}
			disconnect_all(errors::torrent_paused, op_bittorrent);
			return;
		}

		if (!m_graceful_pause_mode)
		{
			// this will make the storage close all
			// files and flush all cached data
			if (m_storage.get())
			{
				TORRENT_ASSERT(m_storage);
				m_ses.disk_thread().async_stop_torrent(m_storage.get()
					, boost::bind(&torrent::on_torrent_paused, shared_from_this(), _1));
			}
			else
			{
				if (alerts().should_post<torrent_paused_alert>())
					alerts().post_alert(torrent_paused_alert(get_handle()));
			}

			disconnect_all(errors::torrent_paused, op_bittorrent);
		}
		else
		{
			// disconnect all peers with no outstanding data to receive
			// and choke all remaining peers to prevent responding to new
			// requests
			bool update_ticks = false;
			for (peer_iterator i = m_connections.begin();
				i != m_connections.end();)
			{
				peer_iterator j = i++;
				boost::shared_ptr<peer_connection> p = (*j)->self();
				TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

				if (p->is_disconnecting())
				{
					i = m_connections.erase(j);
					update_ticks = true;
					continue;
				}

				if (p->outstanding_bytes() > 0)
				{
#if defined TORRENT_LOGGING
					p->peer_log("*** CHOKING PEER: torrent graceful paused");
#endif
					// remove any un-sent requests from the queue
					p->clear_request_queue();
					// don't accept new requests from the peer
					p->choke_this_peer();
					continue;
				}

#if defined TORRENT_LOGGING
				p->peer_log("*** CLOSING CONNECTION: torrent_paused");
#endif
				p->disconnect(errors::torrent_paused, op_bittorrent);
				i = j;
			}
			if (update_ticks)
			{
				update_want_peers();
				update_want_tick();
			}
		}

		stop_announcing();

		// if the torrent is pinned, we should not unload it
		if (!is_pinned())
		{
			m_ses.evict_torrent(this);
		}
	}

#if defined TORRENT_LOGGING
	void torrent::log_to_all_peers(char const* message)
	{
		TORRENT_ASSERT(is_single_thread());
#if defined TORRENT_LOGGING
		for (peer_iterator i = m_connections.begin();
				i != m_connections.end(); ++i)
		{
			(*i)->peer_log("*** %s", message);
		}
#endif

		debug_log("%s", message);
	}
#endif

	// add or remove a url that will be attempted for
	// finding the file(s) in this torrent.
	void torrent::add_web_seed(std::string const& url, web_seed_entry::type_t type)
	{
		web_seed_t ent(url, type);
		// don't add duplicates
		if (std::find(m_web_seeds.begin(), m_web_seeds.end(), ent) != m_web_seeds.end()) return;
		m_web_seeds.push_back(ent);
		m_need_save_resume_data = true;
	}

	void torrent::add_web_seed(std::string const& url, web_seed_entry::type_t type
		, std::string const& auth, web_seed_entry::headers_t const& extra_headers)
	{
		web_seed_t ent(url, type, auth, extra_headers);
		// don't add duplicates
		if (std::find(m_web_seeds.begin(), m_web_seeds.end(), ent) != m_web_seeds.end()) return;
		m_web_seeds.push_back(ent);
		m_need_save_resume_data = true;
	}
	
	void torrent::set_allow_peers(bool b, bool graceful)
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_allow_peers == b
			&& m_graceful_pause_mode == graceful) return;

		m_allow_peers = b;
		if (!m_ses.is_paused())
			m_graceful_pause_mode = graceful;

		update_gauge();
		update_want_scrape();

		if (!b)
		{
			m_announce_to_dht = false;
			m_announce_to_trackers = false;
			m_announce_to_lsd = false;
			do_pause();
		}
		else
		{
			do_resume();
		}
	}

	void torrent::resume()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_allow_peers
			&& m_announce_to_dht
			&& m_announce_to_trackers
			&& m_announce_to_lsd) return;

		m_announce_to_dht = true;
		m_announce_to_trackers = true;
		m_announce_to_lsd = true;
		m_allow_peers = true;
		if (!m_ses.is_paused()) m_graceful_pause_mode = false;

		update_gauge();

		// we need to save this new state
		m_need_save_resume_data = true;

		update_want_scrape();

		do_resume();
	}

	void torrent::do_resume()
	{
		TORRENT_ASSERT(is_single_thread());
		if (is_paused()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				if ((*i)->on_resume()) return;
			} TORRENT_CATCH (std::exception&) {}
		}
#endif

		if (alerts().should_post<torrent_resumed_alert>())
			alerts().post_alert(torrent_resumed_alert(get_handle()));

		m_started = m_ses.session_time();
		if (is_seed()) m_became_seed = m_started;
		if (is_finished()) m_became_finished = m_started;

		clear_error();

		state_updated();
		update_want_peers();
		update_want_tick();
		update_want_scrape();

		start_announcing();

		do_connect_boost();
	}

	void torrent::update_tracker_timer(time_point now)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_announcing)
		{
#if defined TORRENT_LOGGING
			debug_log("*** update tracker timer: not announcing");
#endif
			return;
		}

		time_point next_announce = max_time();
		int tier = INT_MAX;

		bool found_working = false;

		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
#if defined TORRENT_LOGGING
			char msg[1000];
			snprintf(msg, sizeof(msg), "*** update tracker timer: considering \"%s\" "
				"[ announce_to_all_tiers: %d announce_to_all_trackers: %d"
				" found_working: %d i->tier: %d tier: %d "
				" is_working: %d fails: %d fail_limit: %d updating: %d ]"
				, i->url.c_str(), settings().get_bool(settings_pack::announce_to_all_tiers)
				, settings().get_bool(settings_pack::announce_to_all_trackers), found_working
				, i->tier, tier, i->is_working(), i->fails, i->fail_limit
				, i->updating);
			debug_log(msg);
#endif
			if (settings().get_bool(settings_pack::announce_to_all_tiers)
				&& found_working
				&& i->tier <= tier
				&& tier != INT_MAX)
				continue;

			if (i->tier > tier && !settings().get_bool(settings_pack::announce_to_all_tiers)) break;
			if (i->is_working()) { tier = i->tier; found_working = false; }
			if (i->fails >= i->fail_limit && i->fail_limit != 0) continue;
			if (i->updating)
			{
				found_working = true;
			}
			else
			{
				time_point next_tracker_announce = (std::max)(i->next_announce, i->min_announce);
				if (next_tracker_announce < next_announce
					&& (!found_working || i->is_working()))
					next_announce = next_tracker_announce;
			}
			if (i->is_working()) found_working = true;
			if (found_working
				&& !settings().get_bool(settings_pack::announce_to_all_trackers)
				&& !settings().get_bool(settings_pack::announce_to_all_tiers)) break;
		}

#if defined TORRENT_LOGGING
		char msg[200];
		snprintf(msg, sizeof(msg), "*** update tracker timer: next_announce < now %d"
			" m_waiting_tracker: %d next_announce_in: %d"
			, next_announce <= now, m_waiting_tracker
			, int(total_seconds(now - next_announce)));
		debug_log(msg);
#endif
		if (next_announce <= now) next_announce = now;

		// don't re-issue the timer if it's the same expiration time as last time
		// if m_waiting_tracker is false, expires_at() is undefined
		if (m_waiting_tracker && m_tracker_timer.expires_at() == next_announce) return;

		m_waiting_tracker = true;
		error_code ec;
		boost::weak_ptr<torrent> self(shared_from_this());

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("tracker::on_tracker_announce_disp");
#endif
		m_tracker_timer.expires_at(next_announce, ec);
		m_tracker_timer.async_wait(boost::bind(&torrent::on_tracker_announce_disp, self, _1));
	}

	void torrent::start_announcing()
	{
		TORRENT_ASSERT(is_single_thread());
		if (is_paused())
		{
#if defined TORRENT_LOGGING
			debug_log("start_announcing(), paused");
#endif
			return;
		}
		// if we don't have metadata, we need to announce
		// before checking files, to get peers to
		// request the metadata from
		if (!m_files_checked && valid_metadata())
		{
#if defined TORRENT_LOGGING
			debug_log("start_announcing(), files not checked (with valid metadata)");
#endif
			return;
		}
		if (!m_torrent_file->is_valid() && !m_url.empty())
		{
#if defined TORRENT_LOGGING
			debug_log("start_announcing(), downloading URL");
#endif
			return;
		}
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
			std::for_each(m_trackers.begin(), m_trackers.end()
				, boost::bind(&announce_entry::reset, _1));
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

		time_point now = aux::time_now();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			i->next_announce = now;
			i->min_announce = now;
		}
		announce_with_tracker(tracker_request::stopped);
	}

	int torrent::finished_time() const
	{
		return m_finished_time + ((!is_finished() || is_paused()) ? 0
			: (m_ses.session_time() - m_became_finished));
	}

	int torrent::active_time() const
	{
		return m_active_time + (is_paused() ? 0
			: m_ses.session_time() - m_started);
	}

	int torrent::seeding_time() const
	{
		return m_seeding_time + ((!is_seed() || is_paused()) ? 0
			: m_ses.session_time() - m_became_seed);
	}

	void torrent::second_tick(int tick_interval_ms, int /* residual */)
	{
		TORRENT_ASSERT(want_tick());
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::weak_ptr<torrent> self(shared_from_this());

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->tick();
			} TORRENT_CATCH (std::exception&) {}
		}

		if (m_abort) return;
#endif

		// if we're in upload only mode and we're auto-managed
		// leave upload mode every 10 minutes hoping that the error
		// condition has been fixed
		if (m_upload_mode && m_auto_managed
			&& int(m_ses.session_time() - m_upload_mode_time)
			>= settings().get_int(settings_pack::optimistic_disk_retry))
		{
			set_upload_mode(false);
		}

		if (m_storage_tick > 0 && is_loaded())
		{
			--m_storage_tick;
			if (m_storage_tick == 0 && m_storage)
			{
				m_ses.disk_thread().async_tick_torrent(&storage()
					, boost::bind(&torrent::on_disk_tick_done
						, shared_from_this(), _1));
				update_want_tick();
			}
		}

		if (is_paused() && !m_graceful_pause_mode)
		{
			// let the stats fade out to 0
 			m_stat.second_tick(tick_interval_ms);
			// if the rate is 0, there's no update because of network transfers
			if (m_stat.low_pass_upload_rate() > 0 || m_stat.low_pass_download_rate() > 0)
				state_updated();
			else
				update_want_tick();

			return;
		}
		if (m_need_suggest_pieces_refresh)
			do_refresh_suggest_pieces();

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;

			if (settings().get_int(settings_pack::max_sparse_regions) > 0
				&& has_picker()
				&& m_picker->sparse_regions() > settings().get_int(settings_pack::max_sparse_regions))
			{
				// we have too many sparse regions. Prioritize pieces
				// that won't introduce new sparse regions
				// prioritize pieces that will reduce the number of sparse
				// regions even higher
				int start = m_picker->cursor();
				int end = m_picker->reverse_cursor();
				for (int i = start; i < end; ++i)
					update_sparse_piece_prio(i, start, end);
			}
		}

		if (settings().get_bool(settings_pack::rate_limit_ip_overhead))
		{
			int up_limit = upload_limit();
			int down_limit = download_limit();

			if (down_limit > 0
				&& m_stat.download_ip_overhead() >= down_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().post_alert(performance_alert(get_handle()
					, performance_alert::download_limit_too_low));
			}

			if (up_limit > 0
				&& m_stat.upload_ip_overhead() >= up_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().post_alert(performance_alert(get_handle()
					, performance_alert::upload_limit_too_low));
			}
		}

		// ---- TIME CRITICAL PIECES ----

#if TORRENT_DEBUG_STREAMING > 0
		std::vector<partial_piece_info> queue;
		get_download_queue(&queue);

		std::vector<peer_info> peer_list;
		get_peer_info(peer_list);

		std::sort(queue.begin(), queue.end(), boost::bind(&partial_piece_info::piece_index, _1)
			< boost::bind(&partial_piece_info::piece_index, _2));

		printf("average piece download time: %.2f s (+/- %.2f s)\n"
			, m_average_piece_time / 1000.f
			, m_piece_time_deviation / 1000.f);
		for (std::vector<partial_piece_info>::iterator i = queue.begin()
			, end(queue.end()); i != end; ++i)
		{
			extern void print_piece(libtorrent::partial_piece_info* pp
				, std::vector<libtorrent::peer_info> const& peers
				, std::vector<time_critical_piece> const& time_critical);

			print_piece(&*i, peer_list, m_time_critical_pieces);
		}
#endif // TORRENT_DEBUG_STREAMING

		if (!m_time_critical_pieces.empty() && !upload_mode())
		{
			request_time_critical_pieces();
		}

		// ---- WEB SEEDS ----

		maybe_connect_web_seeds();
		
		m_swarm_last_seen_complete = m_last_seen_complete;
		int idx = 0;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++idx)
		{
			// keep the peer object alive while we're
			// inspecting it
			boost::shared_ptr<peer_connection> p = (*i)->self();
			++i;

			// look for the peer that saw a seed most recently
			m_swarm_last_seen_complete = (std::max)(p->last_seen_complete(), m_swarm_last_seen_complete);

			// updates the peer connection's ul/dl bandwidth
			// resource requests
			TORRENT_TRY {
				p->second_tick(tick_interval_ms);
			}
			TORRENT_CATCH (std::exception& e)
			{
				TORRENT_DECLARE_DUMMY(std::exception, e);
				(void)e;
#if defined TORRENT_LOGGING
				p->peer_log("*** ERROR %s", e.what());
#endif
				p->disconnect(errors::no_error, op_bittorrent, 1);
			}

			if (p->is_disconnecting())
			{
				i = m_connections.begin() + idx;
				--idx;
			}
		}
		if (m_ses.alerts().should_post<stats_alert>())
			m_ses.alerts().post_alert(stats_alert(get_handle(), tick_interval_ms, m_stat));

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

		if (is_inactive != m_inactive
			&& m_ses.settings().get_bool(settings_pack::dont_count_slow_torrents))
		{
			m_last_active_change = m_ses.session_time();
			int delay = m_ses.settings().get_int(settings_pack::auto_manage_startup);
			m_inactivity_timer.expires_from_now(seconds(delay));
			m_inactivity_timer.async_wait(boost::bind(&torrent::on_inactivity_tick
				, shared_from_this(), _1));
		}

		update_want_tick();
	}

	bool torrent::is_inactive_internal() const
	{
		if (is_finished())
			return m_stat.upload_payload_rate()
				< m_ses.settings().get_int(settings_pack::inactive_up_rate);
		else
			return m_stat.download_payload_rate()
				< m_ses.settings().get_int(settings_pack::inactive_down_rate);
	}

	void torrent::on_inactivity_tick(error_code const& ec)
	{
		if (ec) return;

		int now = m_ses.session_time();
		int delay = m_ses.settings().get_int(settings_pack::auto_manage_startup);
		if (now - m_last_active_change < delay) return;

		bool is_inactive = is_inactive_internal();
		if (is_inactive == m_inactive) return;

		m_inactive = is_inactive;

		if (m_ses.settings().get_bool(settings_pack::dont_count_slow_torrents))
			m_ses.trigger_auto_manage();
	}

	void torrent::maybe_connect_web_seeds()
	{
		if (m_abort) return;

		// if we have everything we want we don't need to connect to any web-seed
		if (!is_finished() && !m_web_seeds.empty() && m_files_checked
			&& int(m_connections.size()) < m_max_connections
			&& m_ses.num_connections() < m_ses.settings().get_int(settings_pack::connections_limit))
		{
			// keep trying web-seeds if there are any
			// first find out which web seeds we are connected to
			for (std::list<web_seed_t>::iterator i = m_web_seeds.begin();
				i != m_web_seeds.end();)
			{
				std::list<web_seed_t>::iterator w = i++;
				if (w->peer_info.connection) continue;
				if (w->retry > aux::time_now()) continue;
				if (w->resolving) continue;

				connect_to_url_seed(w);
			}
		}
	}

	void torrent::recalc_share_mode()
	{
		TORRENT_ASSERT(share_mode());
		if (is_seed()) return;

		int pieces_in_torrent = m_torrent_file->num_pieces();
		int num_seeds = 0;
		int num_peers = 0;
		int num_downloaders = 0;
		int missing_pieces = 0;
		int num_interested = 0;
		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			peer_connection* p = *i;
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

			if ((*i)->is_peer_interested()) ++num_interested;

			++num_downloaders;
			missing_pieces += pieces_in_torrent - p->num_have_pieces();
		}

		if (num_peers == 0) return;

		if (num_seeds * 100 / num_peers > 50
			&& (num_peers * 100 / m_max_connections > 90
				|| num_peers > 20))
		{
			// we are connected to more than 90% seeds (and we're beyond
			// 90% of the max number of connections). That will
			// limit our ability to upload. We need more downloaders.
			// disconnect some seeds so that we don't have more than 50%
			int to_disconnect = num_seeds - num_peers / 2;
			std::vector<peer_connection*> seeds;
			seeds.reserve(num_seeds);
			for (peer_iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = *i;
				if (p->is_seed()) seeds.push_back(p);
			}

			std::random_shuffle(seeds.begin(), seeds.end());
			TORRENT_ASSERT(to_disconnect <= int(seeds.size()));
			for (int i = 0; i < to_disconnect; ++i)
				seeds[i]->disconnect(errors::upload_upload_connection
					, op_bittorrent);
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
		int num_downloaded_pieces = (std::max)(m_picker->num_have()
			, pieces_in_torrent - m_picker->num_filtered());

		if (num_downloaded_pieces * m_torrent_file->piece_length()
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

		std::vector<int> rarest_pieces;

		int num_pieces = m_torrent_file->num_pieces();
		int rarest_rarity = INT_MAX;
		bool prio_updated = false;
		for (int i = 0; i < num_pieces; ++i)
		{
			piece_picker::piece_stats_t ps = m_picker->piece_stats(i);
			if (ps.peer_count == 0) continue;
			if (ps.priority == 0 && (ps.have || ps.downloading))
			{
				m_picker->set_piece_priority(i, 1);
				prio_updated = true;
				continue;
			}
			// don't count pieces we already have or are trying to download
			if (ps.priority > 0 || ps.have) continue;
			if (int(ps.peer_count) > rarest_rarity) continue;
			if (int(ps.peer_count) == rarest_rarity)
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
		int pick = random() % rarest_pieces.size();
		bool was_finished = is_finished();
		m_picker->set_piece_priority(rarest_pieces[pick], 1);
		update_gauge();
		update_peer_interest(was_finished);
		update_want_peers();
	}

	void torrent::refresh_explicit_cache(int cache_size)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!ready_for_connections()) return;

		if (m_abort) return;
		TORRENT_ASSERT(m_storage);

		if (!is_loaded()) return;

		// rotate the cached pieces
		cache_status status;
		m_ses.disk_thread().get_cache_info(&status, false, m_storage.get());

		// add blocks_per_piece / 2 in order to round to closest whole piece
		int blocks_per_piece = m_torrent_file->piece_length() / block_size();
		int num_cache_pieces = (cache_size + blocks_per_piece / 2) / blocks_per_piece;
		if (num_cache_pieces > m_torrent_file->num_pieces())
			num_cache_pieces = m_torrent_file->num_pieces();

		std::vector<int> avail_vec;
		if (has_picker())
		{
			m_picker->get_availability(avail_vec);
		}
		else
		{
			// we don't keep track of availability, do it the expensive way
			// do a linear search from the first piece
			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				int availability = 0;
				if (!have_piece(i))
				{
					avail_vec.push_back(INT_MAX);
					continue;
				}

				for (const_peer_iterator j = this->begin(); j != this->end(); ++j)
					if ((*j)->has_piece(i)) ++availability;
				avail_vec.push_back(availability);
			}
		}

		// now pick the num_cache_pieces rarest pieces from avail_vec
		std::vector<std::pair<int, int> > pieces(m_torrent_file->num_pieces());
		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
		{
			pieces[i].second = i;
			if (!have_piece(i)) pieces[i].first = INT_MAX;
			else pieces[i].first = avail_vec[i];
		}

		// remove write cache entries
		status.pieces.erase(std::remove_if(status.pieces.begin(), status.pieces.end()
			, boost::bind(&cached_piece_info::kind, _1) == cached_piece_info::write_cache)
			, status.pieces.end());

		// decrease the availability of the pieces that are
		// already in the read cache, to move them closer to
		// the beginning of the pieces list, and more likely
		// to be included in this round of cache pieces
		for (std::vector<cached_piece_info>::iterator i = status.pieces.begin()
			, end(status.pieces.end()); i != end; ++i)
		{
			--pieces[i->piece].first;
		}

		std::random_shuffle(pieces.begin(), pieces.end());
		std::stable_sort(pieces.begin(), pieces.end()
			, boost::bind(&std::pair<int, int>::first, _1) <
			boost::bind(&std::pair<int, int>::first, _2));
		avail_vec.clear();
		for (int i = 0; i < num_cache_pieces; ++i)
		{
			if (pieces[i].first == INT_MAX) break;
			avail_vec.push_back(pieces[i].second);
		}

		if (!avail_vec.empty())
		{
			// the number of pieces to cache for this torrent is proportional
			// the number of peers it has, divided by the total number of peers.
			// Each peer gets an equal share of the cache

			avail_vec.resize((std::min)(num_cache_pieces, int(avail_vec.size())));

			for (std::vector<int>::iterator i = avail_vec.begin()
				, end(avail_vec.end()); i != end; ++i)
			{
				inc_refcount("cache_piece");
				m_ses.disk_thread().async_cache_piece(m_storage.get(), *i
					, boost::bind(&torrent::on_disk_cache_complete
					, shared_from_this(), _1));
			}
		}
	}

	void torrent::sent_bytes(int bytes_payload, int bytes_protocol)
	{
		m_stat.sent_bytes(bytes_payload, bytes_protocol);
		m_ses.sent_bytes(bytes_payload, bytes_protocol);
	}

	void torrent::received_bytes(int bytes_payload, int bytes_protocol)
	{
		m_stat.received_bytes(bytes_payload, bytes_protocol);
		m_ses.received_bytes(bytes_payload, bytes_protocol);
	}

	void torrent::trancieve_ip_packet(int bytes, bool ipv6)
	{
		m_stat.trancieve_ip_packet(bytes, ipv6);
		m_ses.trancieve_ip_packet(bytes, ipv6);
	}

	void torrent::sent_syn(bool ipv6)
	{
		m_stat.sent_syn(ipv6);
		m_ses.sent_syn(ipv6);
	}

	void torrent::received_synack(bool ipv6)
	{
		m_stat.received_synack(ipv6);
		m_ses.received_synack(ipv6);
	}

#if TORRENT_DEBUG_STREAMING > 0
	char const* esc(char const* code)
	{
		// this is a silly optimization
		// to avoid copying of strings
		enum { num_strings = 200 };
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
		using namespace libtorrent;
		std::vector<peer_info>::const_iterator i = std::find_if(peers.begin()
			, peers.end(), boost::bind(&peer_info::ip, _1) == addr);
		if (i == peers.end()) return -1;

		return i - peers.begin();
	}

	void print_piece(libtorrent::partial_piece_info* pp
		, std::vector<libtorrent::peer_info> const& peers
		, std::vector<time_critical_piece> const& time_critical)
	{
		using namespace libtorrent;

		time_point now = clock_type::now();

		float deadline = 0.f;
		float last_request = 0.f;
		int timed_out = -1;

		int piece = pp->piece_index;
		std::vector<time_critical_piece>::const_iterator i
			= std::find_if(time_critical.begin(), time_critical.end()
				, boost::bind(&time_critical_piece::piece, _1) == piece);
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

		printf("%5d: [", piece);
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

			printf("%s%s%c%s", color, multi_req, chr, esc("0"));
		}
		printf("%s]", esc("0"));
		if (deadline != 0.f)
			printf(" deadline: %f last-req: %f timed_out: %d\n"
				, deadline, last_request, timed_out);
		else
			printf("\n");
	}
#endif // TORRENT_DEBUG_STREAMING

	struct busy_block_t
	{
		int peers;
		int index;
		bool operator<(busy_block_t rhs) const { return peers < rhs.peers; }
	};

	void pick_busy_blocks(piece_picker const* picker
		, int piece
		, int blocks_in_piece
		, int timed_out
		, std::vector<piece_block>& interesting_blocks
		, piece_picker::downloading_piece const& pi)
	{
		// if there aren't any free blocks in the piece, and the piece is
		// old enough, we may switch into busy mode for this piece. In this
		// case busy_blocks and busy_count are set to contain the eligible
		// busy blocks we may pick
		// first, figure out which blocks are eligible for picking
		// in "busy-mode"
		busy_block_t* busy_blocks
			= TORRENT_ALLOCA(busy_block_t, blocks_in_piece);
		int busy_count = 0;

		piece_picker::block_info const* info = picker->blocks_for_piece(pi);

		// pick busy blocks from the piece
		for (int k = 0; k < blocks_in_piece; ++k)
		{
			// only consider blocks that have been requested
			// and we're still waiting for them
			if (info[k].state != piece_picker::block_info::state_requested)
				continue;

			piece_block b(piece, k);

			// only allow a single additional request per block, in order
			// to spread it out evenly across all stalled blocks
			if (info[k].num_peers > timed_out)
				continue;

			busy_blocks[busy_count].peers = info[k].num_peers; 
			busy_blocks[busy_count].index = k;
			++busy_count;

#if TORRENT_DEBUG_STREAMING > 1
			printf(" [%d (%d)]", b.block_index, info[k].num_peers);
#endif
		}
#if TORRENT_DEBUG_STREAMING > 1
		printf("\n");
#endif

		// then sort blocks by the number of peers with requests
		// to the blocks (request the blocks with the fewest peers
		// first)
		std::sort(busy_blocks, busy_blocks + busy_count);

		// then insert them into the interesting_blocks vector
		for (int k = 0; k < busy_count; ++k)
		{
			interesting_blocks.push_back(
				piece_block(piece, busy_blocks[k].index));
		}
	}

	void pick_time_critical_block(std::vector<peer_connection*>& peers
		, std::vector<peer_connection*>& ignore_peers
		, std::set<peer_connection*>& peers_with_requests
		, piece_picker::downloading_piece const& pi
		, time_critical_piece* i
		, piece_picker const* picker
		, int blocks_in_piece
		, int timed_out)
	{
		std::vector<piece_block> interesting_blocks;
		std::vector<piece_block> backup1;
		std::vector<piece_block> backup2;
		std::vector<int> ignore;

		time_point now = aux::time_now();

		// loop until every block has been requested from this piece (i->piece)
		do
		{
			// if this peer's download time exceeds 2 seconds, we're done.
			// We don't want to build unreasonably long request queues
			if (!peers.empty() && peers[0]->download_queue_time() > milliseconds(2000))
			{
#if TORRENT_DEBUG_STREAMING > 1
				printf("queue time: %d ms, done\n"
					, int(total_milliseconds(peers[0]->download_queue_time())));
#endif
				break;
			}

			// pick the peer with the lowest download_queue_time that has i->piece
			std::vector<peer_connection*>::iterator p = std::find_if(peers.begin(), peers.end()
				, boost::bind(&peer_connection::has_piece, _1, i->piece));

			// obviously we'll have to skip it if we don't have a peer that has
			// this piece
			if (p == peers.end())
			{
#if TORRENT_DEBUG_STREAMING > 1
				printf("out of peers, done\n");
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
				, ignore, 0);

			interesting_blocks.insert(interesting_blocks.end()
				, backup1.begin(), backup1.end());
			interesting_blocks.insert(interesting_blocks.end()
				, backup2.begin(), backup2.end());

			bool busy_mode = false;

			if (interesting_blocks.empty())
			{
				busy_mode = true;

#if TORRENT_DEBUG_STREAMING > 1
				printf("interesting_blocks.empty()\n");
#endif

				// there aren't any free blocks to pick, and the piece isn't
				// old enough to pick busy blocks yet. break to continue to
				// the next piece.
				if (timed_out == 0)
				{
#if TORRENT_DEBUG_STREAMING > 1
					printf("not timed out, moving on to next piece\n");
#endif
					break;
				}

#if TORRENT_DEBUG_STREAMING > 1
				printf("pick busy blocks\n");
#endif

				pick_busy_blocks(picker, i->piece, blocks_in_piece, timed_out
					, interesting_blocks, pi);
			}

			// we can't pick anything from this piece, we're done with it.
			// move on to the next one
			if (interesting_blocks.empty()) break;

			piece_block b = interesting_blocks.front();

			// in busy mode we need to make sure we don't do silly
			// things like requesting the same block twice from the
			// same peer
			std::vector<pending_block> const& dq = c.download_queue();

			bool already_requested = std::find_if(dq.begin(), dq.end()
				, has_block(b)) != dq.end();

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
				printf("piece already requested by peer, try next peer\n");
#endif
				// try next peer
				continue;
			}

			std::vector<pending_block> const& rq = c.request_queue();

			bool already_in_queue = std::find_if(rq.begin(), rq.end()
				, has_block(b)) != rq.end();

			if (already_in_queue)
			{
				if (!c.make_time_critical(b))
				{
#if TORRENT_DEBUG_STREAMING > 1
					printf("piece already time-critical and in queue for peer, trying next peer\n");
#endif
					ignore_peers.push_back(*p);
					peers.erase(p);
					continue;
				}
				i->last_requested = now;

#if TORRENT_DEBUG_STREAMING > 1
				printf("piece already in queue for peer, making time-critical\n");
#endif

				// we inserted a new block in the request queue, this
				// makes us actually send it later
				peers_with_requests.insert(peers_with_requests.begin(), &c);
			}
			else
			{
				if (!c.add_request(b, peer_connection::req_time_critical
					| (busy_mode ? peer_connection::req_busy : 0)))
				{
#if TORRENT_DEBUG_STREAMING > 1
					printf("failed to request block [%d, %d]\n"
						, b.piece_index, b.block_index);
#endif
					ignore_peers.push_back(*p);
					peers.erase(p);
					continue;
				}

#if TORRENT_DEBUG_STREAMING > 1
				printf("requested block [%d, %d]\n"
					, b.piece_index, b.block_index);
#endif
				peers_with_requests.insert(peers_with_requests.begin(), &c);
			}

			if (!busy_mode) i->last_requested = now;

			if (i->first_requested == min_time()) i->first_requested = now;

			if (!c.can_request_time_critical())
			{
#if TORRENT_DEBUG_STREAMING > 1
				printf("peer cannot pick time critical pieces\n");
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

	void torrent::request_time_critical_pieces()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!upload_mode());

		// build a list of peers and sort it by download_queue_time
		// we use this sorted list to determine which peer we should
		// request a block from. The earlier a peer is in the list,
		// the sooner we will fully download the block we request.
		std::vector<peer_connection*> peers;
		peers.reserve(m_connections.size());

		// some peers are marked as not being able to request time critical
		// blocks from. For instance, peers that have choked us, peers that are
		// on parole (i.e. they are believed to have sent us bad data), peers
		// that are being disconnected, in upload mode etc.
		std::remove_copy_if(m_connections.begin(), m_connections.end()
			, std::back_inserter(peers), !boost::bind(&peer_connection::can_request_time_critical, _1));

		// sort by the time we believe it will take this peer to send us all
		// blocks we've requested from it. The shorter time, the better candidate
		// it is to request a time critical block from.
		std::sort(peers.begin(), peers.end()
			, boost::bind(&peer_connection::download_queue_time, _1, 16*1024)
			< boost::bind(&peer_connection::download_queue_time, _2, 16*1024));

		// remove the bottom 10% of peers from the candidate set.
		// this is just to remove outliers that might stall downloads
		int new_size = (peers.size() * 9 + 9) / 10;
		TORRENT_ASSERT(new_size <= int(peers.size()));
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

		time_point now = clock_type::now();

		// now, iterate over all time critical pieces, in order of importance, and
		// request them from the peers, in order of responsiveness. i.e. request
		// the most time critical pieces from the fastest peers.
		for (std::vector<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
#if TORRENT_DEBUG_STREAMING > 1
			printf("considering %d\n", i->piece);
#endif

			if (peers.empty())
			{
#if TORRENT_DEBUG_STREAMING > 1
				printf("out of peers, done\n");
#endif
				break;
			}

			// the +1000 is to compensate for the fact that we only call this
			// function once per second, so if we need to request it 500 ms from
			// now, we should request it right away
			if (i != m_time_critical_pieces.begin() && i->deadline > now
				+ milliseconds(m_average_piece_time + m_piece_time_deviation * 4 + 1000))
			{
				// don't request pieces whose deadline is too far in the future
				// this is one of the termination conditions. We don't want to
				// send requests for all pieces in the torrent right away
#if TORRENT_DEBUG_STREAMING > 0
				printf("reached deadline horizon [%f + %f * 4 + 1]\n"
					, m_average_piece_time / 1000.f
					, m_piece_time_deviation / 1000.f);
#endif
				break;
			}

			piece_picker::downloading_piece pi;
			m_picker->piece_info(i->piece, pi);

			// the number of "times" this piece has timed out.
			int timed_out = 0;

			int blocks_in_piece = m_picker->blocks_in_piece(i->piece);

#if TORRENT_DEBUG_STREAMING > 0
			i->timed_out = timed_out;
#endif
			int free_to_request = blocks_in_piece
				- pi.finished - pi.writing - pi.requested;

			if (free_to_request == 0)
			{
				if (i->last_requested == min_time())
					i->last_requested = now;

				// if it's been more than half of the typical download time
				// of a piece since we requested the last block, allow
				// one more request per block
				if (m_average_piece_time > 0)
					timed_out = total_milliseconds(now - i->last_requested)
						/ (std::max)(int(m_average_piece_time + m_piece_time_deviation / 2), 1);

#if TORRENT_DEBUG_STREAMING > 0
				i->timed_out = timed_out;
#endif
				// every block in this piece is already requested
				// there's no need to consider this piece, unless it
				// appears to be stalled.
				if (pi.requested == 0 || timed_out == 0)
				{
#if TORRENT_DEBUG_STREAMING > 1
					printf("skipping %d (full) [req: %d timed_out: %d ]\n"
						, i->piece, pi.requested
						, timed_out);
#endif

					// if requested is 0, it meants all blocks have been received, and
					// we're just waiting for it to flush them to disk.
					// if last_requested is recent enough, we should give it some
					// more time
					// skip to the next piece
					continue;
				}

				// it's been too long since we requested the last block from
				// this piece. Allow re-requesting blocks from this piece
#if TORRENT_DEBUG_STREAMING > 1
				printf("timed out [average-piece-time: %d ms ]\n"
					, m_average_piece_time);
#endif
			}

			// pick all blocks for this piece. the peers list is kept up to date
			// and sorted. when we issue a request to a peer, its download queue
			// time will increase and it may need to be bumped in the peers list,
			// since it's ordered by download queue time
			pick_time_critical_block(peers, ignore_peers
				, peers_with_requests
				, pi, &*i, m_picker.get()
				, blocks_in_piece, timed_out);

			// put back the peers we ignored into the peer list for the next piece
			if (!ignore_peers.empty())
			{
				peers.insert(peers.begin(), ignore_peers.begin(), ignore_peers.end());
				ignore_peers.clear();

				// TODO: instead of resorting the whole list, insert the peers
				// directly into the right place
				std::sort(peers.begin(), peers.end()
					, boost::bind(&peer_connection::download_queue_time, _1, 16*1024)
					< boost::bind(&peer_connection::download_queue_time, _2, 16*1024));
			}

			// if this peer's download time exceeds 2 seconds, we're done.
			// We don't want to build unreasonably long request queues
			if (!peers.empty() && peers[0]->download_queue_time() > milliseconds(2000))
				break;
		}

		// commit all the time critical requests
		for (std::set<peer_connection*>::iterator i = peers_with_requests.begin()
			, end(peers_with_requests.end()); i != end; ++i)
		{
			(*i)->send_block_requests();
		}
	}

	std::set<std::string> torrent::web_seeds(web_seed_entry::type_t type) const
	{
		TORRENT_ASSERT(is_single_thread());
		std::set<std::string> ret;
		for (std::list<web_seed_t>::const_iterator i = m_web_seeds.begin()
			, end(m_web_seeds.end()); i != end; ++i)
		{
			if (i->peer_info.banned) continue;
			if (i->type != type) continue;
			ret.insert(i->url);
		}
		return ret;
	}

	void torrent::remove_web_seed(std::string const& url, web_seed_entry::type_t type)
	{
		std::list<web_seed_t>::iterator i = std::find_if(m_web_seeds.begin()
			, m_web_seeds.end()
			, (boost::bind(&web_seed_t::url, _1)
				== url && boost::bind(&web_seed_t::type, _1) == type));
		if (i != m_web_seeds.end()) remove_web_seed(i);
	}

	void torrent::disconnect_web_seed(peer_connection* p)
	{
		std::list<web_seed_t>::iterator i
			= std::find_if(m_web_seeds.begin(), m_web_seeds.end()
			, (boost::bind(&torrent_peer::connection
				, boost::bind(&web_seed_t::peer_info, _1)) == p));

		// this happens if the web server responded with a redirect
		// or with something incorrect, so that we removed the web seed
		// immediately, before we disconnected
		if (i == m_web_seeds.end()) return;

		TORRENT_ASSERT(i->resolving == false);

#if defined TORRENT_LOGGING
		debug_log("disconnect web seed: \"%s\"", i->url.c_str());
#endif
		TORRENT_ASSERT(i->peer_info.connection);
		i->peer_info.connection = 0;
	}

	void torrent::remove_web_seed(peer_connection* p, error_code const& ec
		, operation_t op, int error)
	{
		std::list<web_seed_t>::iterator i = std::find_if(m_web_seeds.begin()
			, m_web_seeds.end()
			, boost::bind(&torrent_peer::connection
				, boost::bind(&web_seed_t::peer_info, _1)) == p);
		TORRENT_ASSERT(i != m_web_seeds.end());
		if (i == m_web_seeds.end()) return;
		if (i->peer_info.connection) i->peer_info.connection->disconnect(ec, op, error);
		if (has_picker()) picker().clear_peer(&i->peer_info);
		m_web_seeds.erase(i);
		update_want_tick();
		m_need_save_resume_data = true;
	}

	void torrent::retry_web_seed(peer_connection* p, int retry)
	{
		TORRENT_ASSERT(is_single_thread());
		std::list<web_seed_t>::iterator i = std::find_if(m_web_seeds.begin()
			, m_web_seeds.end()
			, boost::bind(&torrent_peer::connection
				, boost::bind(&web_seed_t::peer_info, _1)) == p);

		TORRENT_ASSERT(i != m_web_seeds.end());
		if (i == m_web_seeds.end()) return;
		if (retry == 0) retry = m_ses.settings().get_int(settings_pack::urlseed_wait_retry);
		i->retry = aux::time_now() + seconds(retry);
	}

	torrent_state torrent::get_policy_state()
	{
		torrent_state ret;
		ret.is_paused = is_paused();
		ret.is_finished = is_finished();
		ret.allow_multiple_connections_per_ip = settings().get_bool(settings_pack::allow_multiple_connections_per_ip);
		ret.max_peerlist_size = is_paused()
			? settings().get_int(settings_pack::max_paused_peerlist_size)
			: settings().get_int(settings_pack::max_peerlist_size);
		ret.min_reconnect_time = settings().get_int(settings_pack::min_reconnect_time);

		ret.peer_allocator = m_ses.get_peer_allocator();
		ret.ip = &m_ses.external_address();
		ret.port = m_ses.listen_port();
		ret.max_failcount = settings().get_int(settings_pack::max_failcount);
		return ret;
	}

	bool torrent::try_connect_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(want_peers());

		torrent_state st = get_policy_state();
		need_policy();
		torrent_peer* p = m_peer_list->connect_one_peer(m_ses.session_time(), &st);
		peers_erased(st.erased);
		inc_stats_counter(counters::connection_attempt_loops, st.loop_counter);

		if (p == NULL)
		{
			update_want_peers();
			return false;
		}

		if (!connect_to_peer(p))
		{
			m_peer_list->inc_failcount(p);
			update_want_peers();
			return false;
		}
		update_want_peers();

		return true;
	}

	torrent_peer* torrent::add_peer(tcp::endpoint const& adr, int source, int flags)
	{
		TORRENT_ASSERT(is_single_thread());

#if !TORRENT_USE_IPV6
		if (!adr.address().is_v4()) return NULL;
#endif

#ifndef TORRENT_DISABLE_DHT
		if (source != peer_info::resume_data)
		{
			// try to send a DHT ping to this peer
			// as well, to figure out if it supports
			// DHT (uTorrent and BitComet doesn't
			// advertise support)
			udp::endpoint node(adr.address(), adr.port());
			session().add_dht_node(node);
		}
#endif

		if (m_apply_ip_filter
			&& m_ses.get_ip_filter().access(adr.address()) & ip_filter::blocked)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().post_alert(peer_blocked_alert(get_handle()
					, adr.address(), peer_blocked_alert::ip_filter));

#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return NULL;
		}

		if (m_ses.get_port_filter().access(adr.port()) & port_filter::blocked)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().post_alert(peer_blocked_alert(get_handle()
					, adr.address(), peer_blocked_alert::port_filter));
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return NULL;
		}

#if TORRENT_USE_I2P
		// if this is an i2p torrent, and we don't allow mixed mode
		// no regular peers should ever be added!
		if (!settings().get_bool(settings_pack::allow_i2p_mixed) && is_i2p())
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().post_alert(peer_blocked_alert(get_handle()
					, adr.address(), peer_blocked_alert::i2p_mixed));
			return NULL;
		}
#endif

		if (settings().get_bool(settings_pack::no_connect_privileged_ports) && adr.port() < 1024)
		{
			if (alerts().should_post<peer_blocked_alert>())
				alerts().post_alert(peer_blocked_alert(get_handle()
					, adr.address(), peer_blocked_alert::privileged_ports));
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, torrent_plugin::filtered);
#endif
			return NULL;
		}

		need_policy();
		torrent_state st = get_policy_state();
		torrent_peer* p = m_peer_list->add_peer(adr, source, 0, &st);
		peers_erased(st.erased);
		if (p)
		{
			state_updated();
#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extension_add_peer(adr, source, st.first_time_seen ? torrent_plugin::first_time : 0);
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

		need_policy();
		if (!m_peer_list->ban_peer(tp)) return false;
		update_want_peers();

		inc_stats_counter(counters::num_banned_peers);
		return true;
	}

	void torrent::set_seed(torrent_peer* p, bool s)
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

		need_policy();
		m_peer_list->set_seed(p, s);
		update_auto_sequential();
	}

	void torrent::clear_failcount(torrent_peer* p)
	{
		need_policy();
		m_peer_list->set_failcount(p, 0);
		update_want_peers();
	}

	std::pair<peer_list::iterator, peer_list::iterator> torrent::find_peers(address const& a)
	{
		need_policy();
		return m_peer_list->find_peers(a);
	}

	void torrent::update_peer_port(int port, torrent_peer* p, int src)
	{
		need_policy();
		torrent_state st = get_policy_state();
		m_peer_list->update_peer_port(port, p, src, &st);
		peers_erased(st.erased);
		update_want_peers();
	}

	// verify piece is used when checking resume data or when the user
	// adds a piece
	void torrent::verify_piece(int piece)
	{
//		picker().mark_as_checking(piece);

		inc_refcount("verify_piece");
		m_ses.disk_thread().async_hash(m_storage.get(), piece, 0
			, boost::bind(&torrent::on_piece_verified, shared_from_this(), _1)
			, (void*)1);
	}

	announce_entry* torrent::find_tracker(tracker_request const& r)
	{
		std::vector<announce_entry>::iterator i = std::find_if(
			m_trackers.begin(), m_trackers.end()
			, boost::bind(&announce_entry::url, _1) == r.url);
		if (i == m_trackers.end()) return 0;
		return &*i;
	}

	void torrent::ip_filter_updated()
	{
		if (!m_apply_ip_filter) return;
		if (!m_peer_list) return;

		torrent_state st = get_policy_state();
		std::vector<address> banned;
		m_peer_list->apply_ip_filter(m_ses.get_ip_filter(), &st, banned);

		if (alerts().should_post<peer_blocked_alert>())
		{
			for (std::vector<address>::iterator i = banned.begin()
				, end(banned.end()); i != end; ++i)
				alerts().post_alert(peer_blocked_alert(get_handle(), *i
					, peer_blocked_alert::ip_filter));
		}

		peers_erased(st.erased);
	}

	void torrent::port_filter_updated()
	{
		if (!m_apply_ip_filter) return;
		if (!m_peer_list) return;

		torrent_state st = get_policy_state();
		std::vector<address> banned;
		m_peer_list->apply_port_filter(m_ses.get_port_filter(), &st, banned);

		if (alerts().should_post<peer_blocked_alert>())
		{
			for (std::vector<address>::iterator i = banned.begin()
				, end(banned.end()); i != end; ++i)
				alerts().post_alert(peer_blocked_alert(get_handle(), *i
					, peer_blocked_alert::port_filter));
		}

		peers_erased(st.erased);
	}

	// this is called when torrent_peers are removed from the peer_list
	// (peer-list). It removes any references we may have to those torrent_peers,
	// so we don't leave then dangling
	void torrent::peers_erased(std::vector<torrent_peer*> const& peers)
	{
		if (!has_picker()) return;

		for (std::vector<torrent_peer*>::const_iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			m_picker->clear_peer(*i);
		}
#if TORRENT_USE_INVARIANT_CHECKS
		m_picker->check_peers();
#endif
	}

#if !TORRENT_NO_FPU
	void torrent::file_progress(std::vector<float>& fp)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!valid_metadata())
		{
			fp.clear();
			return;
		}
	
		if (!need_loaded()) return;
		fp.resize(m_torrent_file->num_files(), 1.f);
		if (is_seed()) return;

		std::vector<boost::int64_t> progress;
		file_progress(progress);
		for (int i = 0; i < m_torrent_file->num_files(); ++i)
		{
			boost::int64_t file_size = m_torrent_file->files().file_size(i);
			if (file_size == 0) fp[i] = 1.f;
			else fp[i] = float(progress[i]) / file_size;
		}
	}
#endif

	void initialize_file_progress(std::vector<boost::uint64_t>& file_progress
		, piece_picker const& picker, file_storage const& fs)
	{
		int num_pieces = fs.num_pieces();
		int num_files = fs.num_files();

		file_progress.resize(num_files, 0);
		std::fill(file_progress.begin(), file_progress.end(), 0);

		// initialize the progress of each file

		const int piece_size = fs.piece_length();
		boost::uint64_t off = 0;
		boost::uint64_t total_size = fs.total_size();
		int file_index = 0;
		for (int piece = 0; piece < num_pieces; ++piece, off += piece_size)
		{
			TORRENT_ASSERT(file_index < fs.num_files());
			boost::int64_t file_offset = off - fs.file_offset(file_index);
			TORRENT_ASSERT(file_offset >= 0);
			while (file_offset >= fs.file_size(file_index))
			{
				++file_index;
				TORRENT_ASSERT(file_index < fs.num_files());
				file_offset = off - fs.file_offset(file_index);
				TORRENT_ASSERT(file_offset >= 0);
			}
			TORRENT_ASSERT(file_offset <= fs.file_size(file_index));

			if (!picker.have_piece(piece)) continue;

			int size = (std::min)(boost::uint64_t(piece_size), total_size - off);
			TORRENT_ASSERT(size >= 0);

			while (size)
			{
				int add = (std::min)(boost::int64_t(size), fs.file_size(file_index) - file_offset);
				TORRENT_ASSERT(add >= 0);
				file_progress[file_index] += add;

				TORRENT_ASSERT(file_progress[file_index]
					<= fs.file_size(file_index));

				size -= add;
				TORRENT_ASSERT(size >= 0);
				if (size > 0)
				{
					++file_index;
					TORRENT_ASSERT(file_index < fs.num_files());
					file_offset = 0;
				}
			}
		}
	}

	void torrent::file_progress(std::vector<boost::int64_t>& fp, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!valid_metadata())
		{
			fp.clear();
			return;
		}

		if (!need_loaded()) return;

		// if we're a seed, we don't have an m_file_progress anyway
		// since we don't need one. We know we have all files
		// just fill in the full file sizes as a shortcut
		if (is_seed())
		{
			fp.resize(m_torrent_file->num_files());
			file_storage const& fs = m_torrent_file->files();
			for (int i = 0; i < fs.num_files(); ++i)
				fp[i] = fs.file_size(i);
			return;
		}

		if (num_have() == 0)
		{
			// if we don't have any pieces, just return zeroes
			fp.clear();
			fp.resize(m_torrent_file->num_files(), 0);
			return;
		}
		
		int num_files = m_torrent_file->num_files();
		if (m_file_progress.empty())
		{
			// This is the first time the client asks for file progress.
			// allocate it and make sure it's up to date

			// we cover the case where we're a seed above
			TORRENT_ASSERT(has_picker());

			initialize_file_progress(m_file_progress
				, picker(), m_torrent_file->files());
		}

		fp.resize(num_files, 0);

		std::copy(m_file_progress.begin(), m_file_progress.end(), fp.begin());

		if (flags & torrent_handle::piece_granularity)
			return;

		TORRENT_ASSERT(has_picker());

		std::vector<piece_picker::downloading_piece> q = m_picker->get_download_queue();

		if (!q.empty())
		{
			if (!const_cast<torrent&>(*this).need_loaded()) return;
		}

		file_storage const& fs = m_torrent_file->files();
		for (std::vector<piece_picker::downloading_piece>::const_iterator
			i = q.begin(), end(q.end()); i != end; ++i)
		{
			boost::int64_t offset = boost::int64_t(i->index) * m_torrent_file->piece_length();
			int file = fs.file_index_at_offset(offset);
			int num_blocks = m_picker->blocks_in_piece(i->index);
			piece_picker::block_info const* info = m_picker->blocks_for_piece(*i);
			for (int k = 0; k < num_blocks; ++k)
			{
				TORRENT_ASSERT(file < fs.num_files());
				TORRENT_ASSERT(offset == boost::int64_t(i->index) * m_torrent_file->piece_length()
					+ k * block_size());
				TORRENT_ASSERT(offset < m_torrent_file->total_size());
				while (offset >= fs.file_offset(file) + fs.file_size(file))
				{
					++file;
				}
				TORRENT_ASSERT(file < fs.num_files());

				boost::int64_t block = block_size();

				if (info[k].state == piece_picker::block_info::state_none)
				{
					offset += block;
					continue;
				}

				if (info[k].state == piece_picker::block_info::state_requested)
				{
					block = 0;
					torrent_peer* p = static_cast<torrent_peer*>(info[k].peer);
					if (p && p->connection)
					{
						peer_connection* peer = static_cast<peer_connection*>(p->connection);
						boost::optional<piece_block_progress> pbp
							= peer->downloading_piece_progress();
						if (pbp && pbp->piece_index == i->index && pbp->block_index == k)
							block = pbp->bytes_downloaded;
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
					int left_over = int(block_size() - block);
					// split the block on multiple files
					while (block > 0)
					{
						TORRENT_ASSERT(offset <= fs.file_offset(file) + fs.file_size(file));
						boost::int64_t slice = (std::min)(fs.file_offset(file) + fs.file_size(file) - offset
							, block);
						fp[file] += slice;
						offset += slice;
						block -= slice;
						TORRENT_ASSERT(offset <= fs.file_offset(file) + fs.file_size(file));
						if (offset == fs.file_offset(file) + fs.file_size(file))
						{
							++file;
							if (file == fs.num_files())
							{
								offset += block;
								break;
							}
						}
					}
					offset += left_over;
					TORRENT_ASSERT(offset == boost::int64_t(i->index) * m_torrent_file->piece_length()
						+ (k+1) * block_size());
				}
				else
				{
					fp[file] += block;
					offset += block_size();
				}
				TORRENT_ASSERT(file <= m_torrent_file->num_files());
			}
		}
	}
	
	void torrent::new_external_ip()
	{
		if (m_peer_list) m_peer_list->clear_peer_prio();
	}

	void torrent::set_state(torrent_status::state_t s)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(s != 0); // this state isn't used anymore

#if TORRENT_USE_ASSERTS
		if (s == torrent_status::seeding)
			TORRENT_ASSERT(is_seed());

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
			m_ses.alerts().post_alert(state_changed_alert(get_handle()
				, s, (torrent_status::state_t)m_state));
		}

		if (s == torrent_status::finished
			&& alerts().should_post<torrent_finished_alert>())
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()));
		}

		m_state = s;

#if defined TORRENT_LOGGING
		debug_log("set_state() %d", m_state);
#endif

		update_want_peers();
		update_gauge();

		state_updated();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_state(m_state);
			} TORRENT_CATCH (std::exception&) {}
		}
#endif
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void torrent::notify_extension_add_peer(tcp::endpoint const& ip
		, int src, int flags)
	{
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_add_peer(ip, src, flags);
			} TORRENT_CATCH (std::exception&) {}
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

		std::vector<torrent*>& list = m_ses.torrent_list(aux::session_interface::torrent_state_updates);

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

	void torrent::status(torrent_status* st, boost::uint32_t flags)
	{
		INVARIANT_CHECK;

		time_point now = aux::time_now();

		st->handle = get_handle();
		st->info_hash = info_hash();
		st->is_loaded = is_loaded();

		if (flags & torrent_handle::query_name)
			st->name = name();

		if (flags & torrent_handle::query_save_path)
			st->save_path = save_path();

		if (flags & torrent_handle::query_torrent_file)
			st->torrent_file = m_torrent_file;

		st->has_incoming = m_has_incoming;
		if (m_error) st->error = convert_from_native(m_error.message())
			+ ": " + resolve_filename(m_error_file);
		st->seed_mode = m_seed_mode;
		st->moving_storage = m_moving_storage;

		st->added_time = m_added_time;
		st->completed_time = m_completed_time;

		st->last_scrape = m_last_scrape == (std::numeric_limits<boost::int16_t>::min)() ? -1
			: clamped_subtract(m_ses.session_time(), m_last_scrape);

		st->share_mode = m_share_mode;
		st->upload_mode = m_upload_mode;
		st->up_bandwidth_queue = 0;
		st->down_bandwidth_queue = 0;
		int priority = 0;
		for (int i = 0; i < num_classes(); ++i)
		{
			int const* prio = m_ses.peer_classes().at(class_at(i))->priority;
			if (priority < prio[peer_connection::upload_channel])
				priority = prio[peer_connection::upload_channel];
			if (priority < prio[peer_connection::download_channel])
				priority = prio[peer_connection::download_channel];
		}
		st->priority = priority;

		st->num_peers = int(m_connections.size()) - m_num_connecting;

		st->list_peers = m_peer_list ? m_peer_list->num_peers() : 0;
		st->list_seeds = m_peer_list ? m_peer_list->num_seeds() : 0;
		st->connect_candidates = m_peer_list ? m_peer_list->num_connect_candidates() : 0;
		st->seed_rank = seed_rank(settings());

		st->all_time_upload = m_total_uploaded;
		st->all_time_download = m_total_downloaded;

		// activity time
		st->finished_time = finished_time();
		st->active_time = active_time();
		st->seeding_time = seeding_time();
		st->time_since_upload = m_last_upload == (std::numeric_limits<boost::int16_t>::min)() ? -1
			: clamped_subtract(m_ses.session_time(), m_last_upload);
		st->time_since_download = m_last_download == (std::numeric_limits<boost::int16_t>::min)() ? -1
			: clamped_subtract(m_ses.session_time(), m_last_download);

		st->storage_mode = (storage_mode_t)m_storage_mode;

		st->num_complete = (m_complete == 0xffffff) ? -1 : m_complete;
		st->num_incomplete = (m_incomplete == 0xffffff) ? -1 : m_incomplete;
		st->paused = is_torrent_paused();
		st->auto_managed = m_auto_managed;
		st->sequential_download = m_sequential_download;
		st->is_seeding = is_seed();
		st->is_finished = is_finished();
		st->super_seeding = m_super_seeding;
		st->has_metadata = valid_metadata();
		bytes_done(*st, flags & torrent_handle::query_accurate_download_counters);
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

		if (m_waiting_tracker && !is_paused())
			st->next_announce = boost::posix_time::seconds(
				total_seconds(next_announce() - now));
		else
			st->next_announce = boost::posix_time::seconds(0);

		if (st->next_announce.is_negative())
			st->next_announce = boost::posix_time::seconds(0);

		st->announce_interval = boost::posix_time::seconds(0);

		st->current_tracker.clear();
		if (m_last_working_tracker >= 0)
		{
			TORRENT_ASSERT(m_last_working_tracker < int(m_trackers.size()));
			st->current_tracker = m_trackers[m_last_working_tracker].url;
		}
		else
		{
			std::vector<announce_entry>::const_iterator i;
			for (i = m_trackers.begin(); i != m_trackers.end(); ++i)
			{
				if (!i->updating) continue;
				st->current_tracker = i->url;
				break;
			}
		}

		if ((flags & torrent_handle::query_verified_pieces))
		{
			st->verified_pieces = m_verified;
		}

		st->num_uploads = m_num_uploads;
		st->uploads_limit = m_max_uploads == (1<<24)-1 ? -1 : m_max_uploads;
		st->num_connections = int(m_connections.size());
		st->connections_limit = m_max_connections == (1<<24)-1 ? -1 : m_max_connections;
		// if we don't have any metadata, stop here

		st->queue_position = queue_position();
		st->need_save_resume = need_save_resume_data();
		st->ip_filter_applies = m_apply_ip_filter;

		st->state = (torrent_status::state_t)m_state;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		if (st->state == torrent_status::finished
			|| st->state == torrent_status::seeding)
		{
			TORRENT_ASSERT(st->is_finished);
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
			st->progress_ppm = st->total_wanted_done * 1000000
				/ st->total_wanted;
#if !TORRENT_NO_FPU
			st->progress = st->progress_ppm / 1000000.f;
#endif
		}

		int num_pieces = m_torrent_file->num_pieces();
		if (has_picker() && (flags & torrent_handle::query_pieces))
		{
			st->sparse_regions = m_picker->sparse_regions();
			st->pieces.resize(num_pieces, false);
			for (int i = 0; i < num_pieces; ++i)
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
		st->num_pieces = num_have();
		st->num_seeds = num_seeds();
		if ((flags & torrent_handle::query_distributed_copies) && m_picker.get())
		{
			boost::tie(st->distributed_full_copies, st->distributed_fraction) =
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

	void torrent::add_redundant_bytes(int b, torrent::wasted_reason_t reason)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(b > 0);
		m_total_redundant_bytes += b;

		TORRENT_ASSERT(b > 0);
		TORRENT_ASSERT(reason >= 0);
		TORRENT_ASSERT(reason < waste_reason_max);
		m_stats_counters.inc_stats_counter(counters::recv_redundant_bytes, b);
		m_stats_counters.inc_stats_counter(counters::waste_piece_timed_out + reason, b);
	}

	void torrent::add_failed_bytes(int b)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(b > 0);
		m_total_failed_bytes += b;
		m_stats_counters.inc_stats_counter(counters::recv_failed_bytes, b);
	}

	int torrent::num_seeds() const
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		return m_num_seeds;
	}

	int torrent::num_downloaders() const
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		return (std::max)(0, int(m_connections.size()) - m_num_seeds - m_num_connecting);
	}

	void torrent::tracker_request_error(tracker_request const& r
		, int response_code, error_code const& ec, std::string const& msg
		, int retry_interval)
	{
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

#if defined TORRENT_LOGGING
		debug_log("*** tracker error: (%d) %s %s", ec.value()
			, ec.message().c_str(), msg.c_str());
#endif
		if (r.kind == tracker_request::announce_request)
		{
			announce_entry* ae = find_tracker(r);
			if (ae)
			{
				ae->failed(settings(), retry_interval);
				ae->last_error = ec;
				ae->message = msg;
				int tracker_index = ae - &m_trackers[0];
#if defined TORRENT_LOGGING
				debug_log("*** increment tracker fail count [%d]", ae->fails);
#endif
				// never talk to this tracker again
				if (response_code == 410) ae->fail_limit = 1;

				deprioritize_tracker(tracker_index);
			}
			if (m_ses.alerts().should_post<tracker_error_alert>())
			{
				m_ses.alerts().post_alert(tracker_error_alert(get_handle()
					, ae?ae->fails:0, response_code, r.url, ec, msg));
			}
		}
		else if (r.kind == tracker_request::scrape_request)
		{
			if (response_code == 410)
			{
				// never talk to this tracker again
				announce_entry* ae = find_tracker(r);
				if (ae) ae->fail_limit = 1;
			}

			if (m_ses.alerts().should_post<scrape_failed_alert>())
			{
				m_ses.alerts().post_alert(scrape_failed_alert(get_handle(), r.url, ec));
			}
		}
		// announce to the next working tracker
		if ((!m_abort && !is_paused()) || r.event == tracker_request::stopped)
			announce_with_tracker(r.event);
		update_tracker_timer(aux::time_now());
	}

#if defined TORRENT_LOGGING
	void torrent::debug_log(char const* fmt, ...) const
	{
		if (!alerts().should_post<torrent_log_alert>()) return;

		va_list v;	
		va_start(v, fmt);
	
		char buf[400];
		vsnprintf(buf, sizeof(buf), fmt, v);
		va_end(v);

		alerts().post_alert(torrent_log_alert(
			const_cast<torrent*>(this)->get_handle(), buf));
	}
#endif

}

