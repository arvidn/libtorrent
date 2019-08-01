/*

Copyright (c) 2016-2018, Arvid Norberg
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

#ifndef TORRENT_WIN_UTIL_HPP
#define TORRENT_WIN_UTIL_HPP

#include "libtorrent/config.hpp"

namespace libtorrent { namespace aux {

	template <typename Library>
	HMODULE get_library_handle()
	{
		static bool handle_checked = false;
		static HMODULE handle = 0;

		if (!handle_checked)
		{
			handle = LoadLibraryA(Library::library_name);
			handle_checked = true;
		}
		return handle;
	}

	template <typename Library, typename Signature>
	Signature get_library_procedure(LPCSTR name)
	{
		static Signature proc = nullptr;
		static bool failed_proc = false;

		if ((proc == nullptr) && !failed_proc)
		{
			HMODULE const handle = get_library_handle<Library>();
			if (handle) proc = reinterpret_cast<Signature>(reinterpret_cast<void*>(GetProcAddress(handle, name)));
			failed_proc = (proc == nullptr);
		}
		return proc;
	}

	struct iphlpapi {
		static constexpr char const* library_name = "iphlpapi.dll";
	};

	struct kernel32 {
		static constexpr char const* library_name = "kernel32.dll";
	};

	struct advapi32 {
		static constexpr char const* library_name = "advapi32.dll";
	};

} // namespace aux
} // namespace libtorrent

#endif
