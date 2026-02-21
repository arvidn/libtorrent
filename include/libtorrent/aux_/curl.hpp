/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_HPP
#define LIBTORRENT_CURL_HPP
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <stdexcept>
#include <string>
#include <curl/curl.h>

namespace libtorrent::aux {

enum class curl_poll_t : int {
	none   = CURL_POLL_NONE,
	in     = CURL_POLL_IN,
	out    = CURL_POLL_OUT,
	remove = CURL_POLL_REMOVE,
};
static_assert(CURL_POLL_INOUT == (CURL_POLL_IN | CURL_POLL_OUT));

enum class curl_cselect_t : int {
	none = 0, // curl allows the value to be 0, but does not define a type for it
	in   = CURL_CSELECT_IN,
	out  = CURL_CSELECT_OUT,
	err  = CURL_CSELECT_ERR,
};

// Making `option` a compile time constant allows curl's typechecker
// to verify the types (it's currently not working for C++)

template<CURLoption option>
CURLcode curl_easy_setopt_typechecked(CURL* easy_handle, const long value)
{
	static_assert(option >= CURLOPTTYPE_LONG && option < CURLOPTTYPE_OBJECTPOINT);
	return curl_easy_setopt(easy_handle, option, value);
}

// char*, function pointers, callback data (void*)
template<CURLoption option, typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
CURLcode curl_easy_setopt_typechecked(CURL* easy_handle, const T value)
{
	static_assert(option >= CURLOPTTYPE_OBJECTPOINT && option < CURLOPTTYPE_OFF_T);
	return curl_easy_setopt(easy_handle, option, value);
}

template<CURLoption option>
CURLcode curl_easy_setopt_typechecked(CURL* easy_handle, const std::string& value)
{
	static_assert(option >= CURLOPTTYPE_OBJECTPOINT && option < CURLOPTTYPE_FUNCTIONPOINT);
	return curl_easy_setopt_typechecked<option>(easy_handle, value.c_str());
}

template<typename T, CURLINFO info>
CURLcode curl_easy_getinfo_typechecked(CURL* easy_handle, T& value)
{
	using basic_type = std::decay_t<T>;
	constexpr auto info_type = info & CURLINFO_TYPEMASK;

	// these are currently the same
	static_assert(CURLINFO_SLIST == CURLINFO_PTR);

	// switch becomes constexpr in c++20
	if constexpr (info_type == CURLINFO_STRING)
	{
		static_assert(std::is_same_v<basic_type, const char *> || std::is_same_v<basic_type, char *>);
	}
	else if constexpr (info_type == CURLINFO_SLIST)
	{
		static_assert(std::is_pointer_v<basic_type>);
	}
	else if constexpr (info_type == CURLINFO_OFF_T)
	{
		static_assert(std::is_same_v<basic_type, curl_off_t>);
	}
	else if constexpr (info_type == CURLINFO_LONG)
	{
		static_assert(std::is_same_v<basic_type, long>);
	}
	else if constexpr (info_type == CURLINFO_SOCKET)
	{
		static_assert(std::is_same_v<basic_type, curl_socket_t>);
	}
	else if constexpr (info_type == CURLINFO_DOUBLE)
	{
		static_assert(std::is_same_v<basic_type, double>);
	}
	else
	{
		// this triggers if new types are added and used.
		static_assert(false);
	}

	return curl_easy_getinfo(easy_handle, info, &value);
}

class curl_easy_error: public std::runtime_error
{
	CURLcode code_;

public:
	curl_easy_error( CURLcode const ec, std::string const & prefix ):
		std::runtime_error( prefix + ": " + curl_easy_strerror(ec) ), code_( ec ) {}

	[[nodiscard]] CURLcode code() const noexcept
	{
		return code_;
	}
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_HPP
