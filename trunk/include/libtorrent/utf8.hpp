/*
	Copyright (C) 2004-2005 Cory Nelson

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
		claim that you wrote the original software. If you use this software
		in a product, an acknowledgment in the product documentation would be
		appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
		misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
*/

// namespaces added by Arvid Norberg

#ifndef __UTF8_H__
#define __UTF8_H__

#include <string>
#include <iterator>
#include <stdexcept>

namespace libtorrent {
namespace detail {

template<typename InputIterator>
static wchar_t decode_utf8_mb(InputIterator &iter, InputIterator last) {
	if(iter==last) throw std::runtime_error("incomplete UTF-8 sequence");
	if(((*iter)&0xC0)!=0x80) throw std::runtime_error("invalid UTF-8 sequence");

	return (wchar_t)((*iter++)&0x3F);
}

template<typename InputIterator>
static wchar_t decode_utf8(InputIterator &iter, InputIterator last) {
	wchar_t ret;

	if(((*iter)&0x80) == 0) {
		ret=*iter++;
	}
	else if(((*iter)&0x20) == 0) {
		ret=(
			(((wchar_t)((*iter++)&0x1F)) << 6) |
			decode_utf8_mb(iter, last)
		);
	}
	else if(((*iter)&0x10) == 0) {
		ret=(
			(((wchar_t)((*iter++)&0x0F)) << 12) |
			(decode_utf8_mb(iter, last) << 6) |
			decode_utf8_mb(iter, last)
		);
	}
	else throw std::runtime_error("UTF-8 not convertable to UTF-16");

	return ret;
}

template<typename InputIterator, typename OutputIterator>
static OutputIterator utf8_wchar(InputIterator first, InputIterator last, OutputIterator dest) {
	for(; first!=last; ++dest)
		*dest=decode_utf8(first, last);
	return dest;
}

template<typename InputIterator, typename OutputIterator>
static void encode_wchar(InputIterator iter, OutputIterator &dest) {
	if(*iter <= 0x007F) {
		*dest=(char)*iter;
		++dest;
	}
	else if(*iter <= 0x07FF) {
		*dest = (char)(
			0xC0 |
			((*iter & 0x07C0) >> 6)
		);
		++dest;

		*dest = (char)(
			0x80 |
			(*iter & 0x003F)
		);
		++dest;
	}
	else if(*iter <= 0xFFFF) {
		*dest = (char)(
			0xE0 |
			((*iter & 0xF000) >> 12)
		);
		++dest;

		*dest = (char)(
			0x80 |
			((*iter & 0x0FC0) >> 6)
		);
		++dest;

		*dest = (char)(
			0x80 |
			(*iter & 0x003F)
		);
		++dest;
	}
}

template<typename InputIterator, typename OutputIterator>
static OutputIterator wchar_utf8(InputIterator first, InputIterator last, OutputIterator dest) {
	for(; first!=last; ++first)
		encode_wchar(first, dest);
	return dest;
}

}

static void utf8_wchar(const std::string &utf8, std::wstring &wide) {
	wide.clear();
	utf8_wchar(utf8.begin(), utf8.end(), std::insert_iterator<std::wstring>(wide, wide.end()));
}

static std::wstring utf8_wchar(const std::string &str) {
	std::wstring ret;
	utf8_wchar(str, ret);
	return ret;
}

static std::string wchar_utf8(const std::wstring &wide, std::string &utf8) {
	utf8.clear();
	wchar_utf8(wide.begin(), wide.end(), std::insert_iterator<std::string>(utf8, utf8.end()));
}

static std::string wchar_utf8(const std::wstring &str) {
	std::string ret;
	wchar_utf8(str, ret);
	return ret;
}

}

#endif
