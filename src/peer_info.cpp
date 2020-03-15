/*

Copyright (c) 2017, Arvid Norberg
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

#include "libtorrent/peer_info.hpp"

namespace libtorrent {

	peer_info::peer_info() = default;
	peer_info::~peer_info() = default;
	peer_info::peer_info(peer_info const&) = default;
	peer_info::peer_info(peer_info&&) = default;
	peer_info& peer_info::operator=(peer_info const&) = default;

	// This will no longer be necessary with C++17
	constexpr peer_flags_t peer_info::interesting;
	constexpr peer_flags_t peer_info::choked;
	constexpr peer_flags_t peer_info::remote_interested;
	constexpr peer_flags_t peer_info::remote_choked;
	constexpr peer_flags_t peer_info::supports_extensions;
	constexpr peer_flags_t peer_info::local_connection;
	constexpr peer_flags_t peer_info::handshake;
	constexpr peer_flags_t peer_info::connecting;
#if TORRENT_ABI_VERSION == 1
	constexpr peer_flags_t peer_info::queued;
#endif
	constexpr peer_flags_t peer_info::on_parole;
	constexpr peer_flags_t peer_info::seed;
	constexpr peer_flags_t peer_info::optimistic_unchoke;
	constexpr peer_flags_t peer_info::snubbed;
	constexpr peer_flags_t peer_info::upload_only;
	constexpr peer_flags_t peer_info::endgame_mode;
	constexpr peer_flags_t peer_info::holepunched;
	constexpr peer_flags_t peer_info::i2p_socket;
	constexpr peer_flags_t peer_info::utp_socket;
	constexpr peer_flags_t peer_info::ssl_socket;
	constexpr peer_flags_t peer_info::rc4_encrypted;
	constexpr peer_flags_t peer_info::plaintext_encrypted;

	constexpr peer_source_flags_t peer_info::tracker;
	constexpr peer_source_flags_t peer_info::dht;
	constexpr peer_source_flags_t peer_info::pex;
	constexpr peer_source_flags_t peer_info::lsd;
	constexpr peer_source_flags_t peer_info::resume_data;
	constexpr peer_source_flags_t peer_info::incoming;

	constexpr bandwidth_state_flags_t peer_info::bw_idle;
	constexpr bandwidth_state_flags_t peer_info::bw_limit;
	constexpr bandwidth_state_flags_t peer_info::bw_network;
	constexpr bandwidth_state_flags_t peer_info::bw_disk;

#if TORRENT_ABI_VERSION == 1
	constexpr bandwidth_state_flags_t peer_info::bw_torrent;
	constexpr bandwidth_state_flags_t peer_info::bw_global;
#endif

}
