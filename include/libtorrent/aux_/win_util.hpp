/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Tiger Wang
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WIN_UTIL_HPP
#define TORRENT_WIN_UTIL_HPP

#include "libtorrent/config.hpp"

namespace lt::aux {

#ifdef TORRENT_WINRT
	using LoadLibraryASignature = HMODULE WINAPI(_In_ LPCSTR lpLibFileName);
#endif

	template <typename Library>
	HMODULE get_library_handle()
	{
		static bool handle_checked = false;
		static HMODULE handle = nullptr;

		if (!handle_checked)
		{
			handle_checked = true;

#ifdef TORRENT_WINRT
			MEMORY_BASIC_INFORMATION Information;

			if (::VirtualQuery(&VirtualQuery, &Information, sizeof(Information)) == 0)
			{
				return nullptr;
			}

			const auto SyscallBegin = static_cast<HMODULE>(Information.AllocationBase);
			const auto LoadLibraryA = reinterpret_cast<LoadLibraryASignature *>(::GetProcAddress(SyscallBegin, "LoadLibraryA"));

			if (LoadLibraryA == nullptr)
			{
				return nullptr;
			}
#endif

			handle = LoadLibraryA(Library::library_name);
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

} // namespace lt::aux

#endif
