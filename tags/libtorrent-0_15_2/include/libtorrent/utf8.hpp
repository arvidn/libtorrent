/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef TORRENT_UTF8_HPP_INCLUDED
#define TORRENT_UTF8_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if !defined BOOST_FILESYSTEM_NARROW_ONLY || defined TORRENT_WINDOWS

#include <string>
#include <cwchar>
#include "libtorrent/ConvertUTF.h"

namespace libtorrent
{

	inline int utf8_wchar(const std::string &utf8, std::wstring &wide)
	{
		// allocate space for worst-case
		wide.resize(utf8.size());
		wchar_t const* dst_start = wide.c_str();
		char const* src_start = utf8.c_str();
		ConversionResult ret;
		if (sizeof(wchar_t) == sizeof(UTF32))
		{
			ret = ConvertUTF8toUTF32((const UTF8**)&src_start, (const UTF8*)src_start
				+ utf8.size(), (UTF32**)&dst_start, (UTF32*)dst_start + wide.size()
				, lenientConversion);
			wide.resize(dst_start - wide.c_str());
			return ret;
		}
		else if (sizeof(wchar_t) == sizeof(UTF16))
		{
			ret = ConvertUTF8toUTF16((const UTF8**)&src_start, (const UTF8*)src_start
				+ utf8.size(), (UTF16**)&dst_start, (UTF16*)dst_start + wide.size()
				, lenientConversion);
			wide.resize(dst_start - wide.c_str());
			return ret;
		}
		else
		{
			return sourceIllegal;
		}
	}

	inline int wchar_utf8(const std::wstring &wide, std::string &utf8)
	{
		// allocate space for worst-case
		utf8.resize(wide.size() * 6);
		if (wide.empty()) return 0;
		char* dst_start = &utf8[0];
		wchar_t const* src_start = wide.c_str();
		ConversionResult ret;
		if (sizeof(wchar_t) == sizeof(UTF32))
		{
			ret = ConvertUTF32toUTF8((const UTF32**)&src_start, (const UTF32*)src_start
				+ wide.size(), (UTF8**)&dst_start, (UTF8*)dst_start + utf8.size()
				, lenientConversion);
			utf8.resize(dst_start - &utf8[0]);
			return ret;
		}
		else if (sizeof(wchar_t) == sizeof(UTF16))
		{
			ret = ConvertUTF16toUTF8((const UTF16**)&src_start, (const UTF16*)src_start
				+ wide.size(), (UTF8**)&dst_start, (UTF8*)dst_start + utf8.size()
				, lenientConversion);
			utf8.resize(dst_start - &utf8[0]);
			return ret;
		}
		else
		{
			return sourceIllegal;
		}
	}
}
#endif

#endif

