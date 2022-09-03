/*
Copyright (c) 2022, Arvid Norberg
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

#ifndef TORRENT_WIN_FILE_HANDLE_HPP_INCLUDED
#define TORRENT_WIN_FILE_HANDLE_HPP_INCLUDED

#if defined TORRENT_WINDOWS

#include "libtorrent/aux_/windows.hpp"

namespace libtorrent {
namespace aux {

struct win_file_handle
{
	win_file_handle(HANDLE h) : m_h(h) {}

	~win_file_handle()
	{
		if (m_h != INVALID_HANDLE_VALUE) ::CloseHandle(m_h);
	}

	win_file_handle(win_file_handle const&) = delete;
	win_file_handle(win_file_handle&& rhs)
		: m_h(rhs.m_h)
	{
		rhs.m_h = INVALID_HANDLE_VALUE;
	}
	HANDLE handle() const { return m_h; }
private:
	HANDLE m_h;
};

}
}

#endif

#endif
