/*

Copyright (c) 2016, Arvid Norberg
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

#ifndef TORRENT_SCOPE_END_HPP_INCLUDED
#define TORRENT_SCOPE_END_HPP_INCLUDED

#include <utility>

namespace libtorrent { namespace aux {

	template <typename Fun>
	struct scope_end_impl
	{
		explicit scope_end_impl(Fun f) : m_fun(std::move(f)) {}
		~scope_end_impl() { if (m_armed) m_fun(); }

		// movable
		scope_end_impl(scope_end_impl&&) noexcept = default;
		scope_end_impl& operator=(scope_end_impl&&) noexcept = default;

		// non-copyable
		scope_end_impl(scope_end_impl const&) = delete;
		scope_end_impl& operator=(scope_end_impl const&) = delete;
		void disarm() { m_armed = false; }
	private:
		Fun m_fun;
		bool m_armed = true;
	};

	template <typename Fun>
	scope_end_impl<Fun> scope_end(Fun f) { return scope_end_impl<Fun>(std::move(f)); }
}}

#endif

