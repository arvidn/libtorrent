/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#include "libtorrent/add_torrent_params.hpp"

namespace libtorrent {

	add_torrent_params::add_torrent_params(storage_constructor_type sc)
		: storage(std::move(sc)) {}
	add_torrent_params::add_torrent_params(add_torrent_params&&) noexcept = default;
	add_torrent_params::add_torrent_params(add_torrent_params const&) = default;
	add_torrent_params& add_torrent_params::operator=(add_torrent_params const&) = default;

#if TORRENT_ABI_VERSION == 1
#define DECL_FLAG(name) \
	constexpr torrent_flags_t add_torrent_params::flag_##name

			DECL_FLAG(seed_mode);
			DECL_FLAG(upload_mode);
			DECL_FLAG(share_mode);
			DECL_FLAG(apply_ip_filter);
			DECL_FLAG(paused);
			DECL_FLAG(auto_managed);
			DECL_FLAG(duplicate_is_error);
			DECL_FLAG(update_subscribe);
			DECL_FLAG(super_seeding);
			DECL_FLAG(sequential_download);
			DECL_FLAG(pinned);
			DECL_FLAG(stop_when_ready);
			DECL_FLAG(override_trackers);
			DECL_FLAG(override_web_seeds);
			DECL_FLAG(need_save_resume);
			DECL_FLAG(override_resume_data);
			DECL_FLAG(merge_resume_trackers);
			DECL_FLAG(use_resume_save_path);
			DECL_FLAG(merge_resume_http_seeds);
			DECL_FLAG(default_flags);
#undef DECL_FLAG
#endif // TORRENT_ABI_VERSION

	static_assert(std::is_nothrow_move_constructible<add_torrent_params>::value
		, "should be nothrow move constructible");

	static_assert(std::is_nothrow_move_constructible<std::string>::value
		, "should be nothrow move constructible");

	// TODO: pre C++17, GCC and msvc does not make std::string nothrow move
	// assignable, which means no type containing a string will be nothrow move
	// assignable by default either
//	static_assert(std::is_nothrow_move_assignable<add_torrent_params>::value
//		, "should be nothrow move assignable");

	// TODO: it would be nice if this was nothrow default constructible
//	static_assert(std::is_nothrow_default_constructible<add_torrent_params>::value
//		, "should be nothrow default constructible");
}
