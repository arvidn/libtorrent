/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_TORRENT_HANDLE_HPP_INCLUDED
#define TORRENT_TORRENT_HANDLE_HPP_INCLUDED

#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/torrent_info.hpp"

namespace libtorrent
{
	namespace detail
	{
		struct session_impl;
		struct checker_impl;
	}

	struct duplicate_torrent: std::exception
	{
		virtual const char* what() const throw()
		{ return "torrent already exists in session"; }
	};

	struct invalid_handle: std::exception
	{
		virtual const char* what() const throw()
		{ return "invalid torrent handle used"; }
	};

	struct torrent_status
	{
		torrent_status()
			: state(queued_for_checking)
			, progress(0.f)
			, total_download(0)
			, total_upload(0)
			, total_payload_download(0)
			, total_payload_upload(0)
			, download_rate(0)
			, upload_rate(0)
			, total_done(0)
		{}

		enum state_t
		{
			queued_for_checking,
			checking_files,
			connecting_to_tracker,
			downloading,
			seeding
		};
		
		state_t state;
		float progress;
		boost::posix_time::time_duration next_announce;

		// transferred this session!
		// total, payload plus protocol
		std::size_t total_download;
		std::size_t total_upload;

		// payload only
		std::size_t total_payload_download;
		std::size_t total_payload_upload;

		// current transfer rate
		// payload plus protocol
		float download_rate;
		float upload_rate;

		std::vector<bool> pieces;

		// the number of bytes of the file we have
		std::size_t total_done;
	};

	struct partial_piece_info
	{
		enum { max_blocks_per_piece = piece_picker::max_blocks_per_piece };
		int piece_index;
		int blocks_in_piece;
		std::bitset<max_blocks_per_piece> requested_blocks;
		std::bitset<max_blocks_per_piece> finished_blocks;
		peer_id peer[max_blocks_per_piece];
		int num_downloads[max_blocks_per_piece];
	};

	struct torrent_handle
	{
		friend class session;
		friend class torrent;

		torrent_handle(): m_ses(0) {}

		void get_peer_info(std::vector<peer_info>& v) const;
		torrent_status status() const;
		void get_download_queue(std::vector<partial_piece_info>& queue) const;

		const torrent_info& get_torrent_info() const;
		bool is_valid() const;

		entry write_resume_data();

		// forces this torrent to reannounce
		// (make a rerequest from the tracker)
		void force_reannounce() const;

		// TODO: add a feature where the user can ask the torrent
		// to finish all pieces currently in the pipeline, and then
		// abort the torrent.

		// manually connect a peer
		void connect_peer(const address& adr) const;

		// TODO: add finish_file_allocation, which will force the
		// torrent to allocate storage for all pieces.

		boost::filesystem::path save_path() const;

		// -1 means unlimited unchokes
		void set_max_uploads(int max_uploads);

		const sha1_hash& info_hash() const
		{ return m_info_hash; }

		bool operator==(const torrent_handle& h) const
		{ return m_info_hash == h.m_info_hash; }

		bool operator!=(const torrent_handle& h) const
		{ return m_info_hash != h.m_info_hash; }

		bool operator<(const torrent_handle& h) const
		{ return m_info_hash < h.m_info_hash; }

	private:

		torrent_handle(detail::session_impl* s,
			detail::checker_impl* c,
			const sha1_hash& h)
			: m_ses(s)
			, m_chk(c)
			, m_info_hash(h)
		{}

		detail::session_impl* m_ses;
		detail::checker_impl* m_chk;
		sha1_hash m_info_hash;

	};


}

#endif // TORRENT_TORRENT_HANDLE_HPP_INCLUDED
