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
		enum state_t
		{
			queued_for_checking,
			checking_files,
			downloading,
			seeding
		};
		
		state_t state;
		float progress;
		boost::posix_time::time_duration next_announce;

		// transferred this session!
		std::size_t total_download;
		std::size_t total_upload;
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
		torrent_handle(): m_ses(0) {}

		void get_peer_info(std::vector<peer_info>& v);
		torrent_status status();
		void get_download_queue(std::vector<partial_piece_info>& queue);

		const torrent_info& get_torrent_info();
		bool is_valid();

		// TODO: add force reannounce

		// TODO: add a feature where the user can ask the torrent
		// to finish all pieces currently in the pipeline, and then
		// abort the torrent.

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
		sha1_hash m_info_hash; // should be replaced with a torrent*?

	};


}

#endif // TORRENT_TORRENT_HANDLE_HPP_INCLUDED
