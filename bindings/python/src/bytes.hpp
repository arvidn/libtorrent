// Copyright Arvid Norberg 2006-2013. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BYTES_HPP
#define BYTES_HPP

#include <string>

struct bytes
{
	bytes(char const* s, std::size_t len): arr(s, len) {}
	bytes(std::string const& s): arr(s) {}
	bytes(std::string&& s): arr(std::move(s)) {}
	bytes(bytes const&) = default;
	bytes(bytes&&) noexcept = default;
	bytes& operator=(bytes&&) & noexcept = default;
	bytes() {}
	std::string arr;
};

#endif
