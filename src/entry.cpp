/*

Copyright (c) 2003-2005, 2007-2008, 2010, 2015-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2017, 2020, Alden Torres
Copyright (c) 2019, Amir Abrams
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/string_util.hpp"
#include "libtorrent/aux_/throw.hpp"

namespace lt {
namespace aux {

	string_view integer_to_str(std::array<char, 21>& buf, entry::integer_type val)
	{
		if (val >= 0)
		{
			if (val < 10)
			{
				buf[0] = '0' + static_cast<char>(val);
				return {buf.data(), std::size_t(1)};
			}
			if (val < 100)
			{
				buf[0] = '0' + (val / 10) % 10;
				buf[1] = '0' + val % 10;
				return {buf.data(), std::size_t(2)};
			}
			if (val < 1000)
			{
				buf[0] = '0' + (val / 100) % 10;
				buf[1] = '0' + (val / 10) % 10;
				buf[2] = '0' + val % 10;
				return {buf.data(), std::size_t(3)};
			}
			if (val < 10000)
			{
				buf[0] = '0' + (val / 1000) % 10;
				buf[1] = '0' + (val / 100) % 10;
				buf[2] = '0' + (val / 10) % 10;
				buf[3] = '0' + val % 10;
				return {buf.data(), std::size_t(4)};
			}
			if (val < 100000)
			{
				buf[0] = '0' + (val / 10000) % 10;
				buf[1] = '0' + (val / 1000) % 10;
				buf[2] = '0' + (val / 100) % 10;
				buf[3] = '0' + (val / 10) % 10;
				buf[4] = '0' + val % 10;
				return {buf.data(), std::size_t(5)};
			}
		}
		// slow path
		// convert positive values to negative, since the negative space is
		// larger, so we can fit INT64_MIN
		int sign = 1;
		if (val >= 0)
		{
			sign = 0;
			val = -val;
		}
		char* ptr = &buf.back();
		if (val == 0) *ptr-- = '0';
		while (val != 0)
		{
			*ptr-- = '0' - char(val % 10);
			val /= 10;
		}
		if (sign) *ptr-- = '-';
		++ptr;
		return {ptr, static_cast<std::size_t>(&buf.back() - ptr + 1)};
	}
} // aux

namespace {
[[noreturn]] inline void throw_error()
{ aux::throw_ex<system_error>(errors::invalid_entry_type); }
} // anonymous

	entry& entry::operator[](string_view key)
	{
		// at least GCC-5.4 for ARM (on travis) has a libstdc++ whose debug map$
		// doesn't seem to support transparent comparators$
#if ! defined _GLIBCXX_DEBUG
		auto const i = dict().find(key);
#else
		auto const i = dict().find(std::string(key));
#endif
		if (i != dict().end()) return i->second;
		auto const ret = dict().emplace(
			std::piecewise_construct,
			std::forward_as_tuple(key),
			std::forward_as_tuple()).first;
		return ret->second;
	}

	const entry& entry::operator[](string_view key) const
	{
		// at least GCC-5.4 for ARM (on travis) has a libstdc++ whose debug map$
		// doesn't seem to support transparent comparators$
#if ! defined _GLIBCXX_DEBUG
		auto const i = dict().find(key);
#else
		auto const i = dict().find(std::string(key));
#endif
		if (i == dict().end()) throw_error();
		return i->second;
	}

	entry* entry::find_key(string_view key)
	{
#if ! defined _GLIBCXX_DEBUG
		auto const i = dict().find(key);
#else
		auto const i = dict().find(std::string(key));
#endif
		if (i == dict().end()) return nullptr;
		return &i->second;
	}

	entry const* entry::find_key(string_view key) const
	{
#if ! defined _GLIBCXX_DEBUG
		auto const i = dict().find(key);
#else
		auto const i = dict().find(std::string(key));
#endif
		if (i == dict().end()) return nullptr;
		return &i->second;
	}

	entry::data_type entry::type() const
	{
		return static_cast<entry::data_type>(index());
	}

	entry::~entry() = default;

	entry& entry::operator=(entry const& e) & = default;
	entry& entry::operator=(entry&& e) & = default;

	entry& entry::operator=(dictionary_type d) & { emplace<dictionary_type>(std::move(d)); return *this; }
	entry& entry::operator=(span<char const> str) & { emplace<string_type>(str.data(), str.size()); return *this; }
	entry& entry::operator=(string_view str) & { emplace<string_type>(str.data(), str.size()); return *this; }
	entry& entry::operator=(string_type str) & { emplace<string_type>(std::move(str)); return *this; }
	entry& entry::operator=(list_type i) & { emplace<list_type>(std::move(i)); return *this; }
	entry& entry::operator=(integer_type i) & { emplace<integer_type>(i); return *this; }
	entry& entry::operator=(preformatted_type d) & { emplace<preformatted_type>(std::move(d)); return *this; }

	template <typename U, typename Cond>
	entry& entry::operator=(U v) &
	{
		emplace<string_type>(v);
		return *this;
	}

	// explicit template instantiation
	template TORRENT_EXPORT
	entry& entry::operator=(char const*) &;

	template <typename T>
	T& entry::get()
	{
		if (std::holds_alternative<uninitialized_type>(*this)) emplace<T>();
		else if (!std::holds_alternative<T>(*this)) throw_error();
		return std::get<T>(*this);
	}

	template <typename T>
	T const& entry::get() const
	{
		if (!std::holds_alternative<T>(*this)) throw_error();
		return std::get<T>(*this);
	}

	entry::integer_type& entry::integer() { return get<integer_type>(); }
	entry::integer_type const& entry::integer() const { return get<integer_type>(); }
	entry::string_type& entry::string() { return get<string_type>(); }
	entry::string_type const& entry::string() const { return get<string_type>(); }
	entry::list_type& entry::list() { return get<list_type>(); }
	entry::list_type const& entry::list() const { return get<list_type>(); }
	entry::dictionary_type& entry::dict() { return get<dictionary_type>(); }
	entry::dictionary_type const& entry::dict() const { return get<dictionary_type>(); }
	entry::preformatted_type& entry::preformatted() { return get<preformatted_type>(); }
	entry::preformatted_type const& entry::preformatted() const { return get<preformatted_type>(); }

	entry::entry() : variant_type(std::in_place_type<uninitialized_type>) {}
	entry::entry(data_type t) : variant_type(std::in_place_type<uninitialized_type>)
	{
		switch (t)
		{
		case int_t: emplace<integer_type>(); break;
		case string_t: emplace<string_type>(); break;
		case list_t: emplace<list_type>(); break;
		case dictionary_t: emplace<dictionary_type>(); break;
		case undefined_t: emplace<uninitialized_type>(); break;
		case preformatted_t: emplace<preformatted_type>(); break;
		}
	}

	entry::entry(const entry& e) = default;
	entry::entry(entry&& e) noexcept = default;

	entry::entry(bdecode_node const& n)
		: variant_type(std::in_place_type<uninitialized_type>)
	{
		this->operator=(n);
	}

	entry::entry(dictionary_type v) : variant_type(std::move(v)) {}
	entry::entry(list_type v) : variant_type(std::move(v)) {}
	entry::entry(span<char const> v) : variant_type(std::in_place_type<string_type>, v.data(), v.size()) {}
	entry::entry(string_view v) : variant_type(std::in_place_type<string_type>, v.data(), v.size()) {}
	entry::entry(string_type s) : variant_type(std::move(s)) {}

	template <typename U, typename Cond>
	entry::entry(U v) // NOLINT
			: variant_type(std::in_place_type<string_type>, std::move(v))
	{}

	// explicit template instantiation
	template TORRENT_EXPORT
	entry::entry(char const*);

	entry::entry(integer_type v) : variant_type(std::move(v)) {}
	entry::entry(preformatted_type v) : variant_type(std::move(v)) {}

	// convert a bdecode_node into an old school entry
	entry& entry::operator=(bdecode_node const& e) &
	{
		emplace<uninitialized_type>();
		switch (e.type())
		{
			case bdecode_node::string_t:
				this->string() = e.string_value();
				break;
			case bdecode_node::int_t:
				this->integer() = e.int_value();
				break;
			case bdecode_node::dict_t:
			{
				dictionary_type& d = this->dict();
				for (int i = 0; i < e.dict_size(); ++i)
				{
					std::pair<string_view, bdecode_node> elem = e.dict_at(i);
					d[std::string(elem.first)] = elem.second;
				}
				break;
			}
			case bdecode_node::list_t:
			{
				list_type& l = this->list();
				for (int i = 0; i < e.list_size(); ++i)
				{
					l.emplace_back();
					l.back() = e.list_at(i);
				}
				break;
			}
			case bdecode_node::none_t:
				break;
		}
		return *this;
	}

	bool operator==(entry const& lhs, entry const& rhs)
	{
		return static_cast<entry::variant_type const&>(lhs)
			== static_cast<entry::variant_type const&>(rhs);
	}

namespace {
	bool is_binary(std::string const& str)
	{
		return std::any_of(str.begin(), str.end()
			, [](char const c) { return !aux::is_print(c); });
	}

	std::string print_string(std::string const& str)
	{
		if (is_binary(str)) return aux::to_hex(str);
		else return str;
	}

	void add_indent(std::string& out, int const indent)
	{
		out.resize(out.size() + size_t(indent), ' ');
	}

	struct to_string_visitor
	{
		std::string& out;
		int indent;
		bool single_line;

		void operator()(entry::integer_type i) const
		{
			out += aux::to_string(i).data();
		}

		void operator()(entry::string_type const& s) const
		{
			out += "'";
			out += print_string(s);
			out += "'";
		}

		void operator()(entry::dictionary_type const& d)
		{
			out += single_line ? "{ " : "{\n";
			bool first = true;
			++indent;
			for (auto const& item : d)
			{
				if (!first) out += single_line ? ", " : ",\n";
				first = false;
				if (!single_line) add_indent(out, indent);
				out += "'";
				out += print_string(item.first);
				out += "': ";

				std::visit(*this, static_cast<entry::variant_type const&>(item.second));
			}
			--indent;
			out += " }";
		}

		void operator()(entry::list_type const& l)
		{
			out += single_line ? "[ " : "[\n";
			bool first = true;
			++indent;
			for (auto const& item : l)
			{
				if (!first) out += single_line ? ", " : ",\n";
				first = false;
				if (!single_line) add_indent(out, indent);

				std::visit(*this, static_cast<entry::variant_type const&>(item));
			}
			out += " ]";
			--indent;
		}

		void operator()(entry::preformatted_type const&) const
		{
			out += "<preformatted>";
		}

		void operator()(entry::uninitialized_type const&) const
		{
			out += "<uninitialized>";
		}
	};
}

	std::string entry::to_string(bool const single_line) const
	{
		std::string ret;
		std::visit(to_string_visitor{ret, 0, single_line}, static_cast<entry::variant_type const&>(*this));
		return ret;
	}

}
