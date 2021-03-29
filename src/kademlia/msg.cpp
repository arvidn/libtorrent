/*

Copyright (c) 2015-2016, Steven Siloti
Copyright (c) 2016, 2018-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/msg.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/entry.hpp"

namespace lt { namespace dht {

bool verify_message_impl(bdecode_node const& message, span<key_desc_t const> desc
	, span<bdecode_node> ret, span<char> error)
{
	TORRENT_ASSERT(desc.size() == ret.size());

	auto const size = ret.size();

	// get a non-root bdecode_node that still
	// points to the root. message should not be copied
	bdecode_node msg = message.non_owning();

	// clear the return buffer
	for (int i = 0; i < size; ++i)
		ret[i].clear();

	// when parsing child nodes, this is the stack
	// of bdecode_nodes to return to
	bdecode_node stack[5];
	int stack_ptr = -1;

	if (msg.type() != bdecode_node::dict_t)
	{
		std::snprintf(error.data(), static_cast<std::size_t>(error.size()), "not a dictionary");
		return false;
	}
	++stack_ptr;
	stack[stack_ptr] = msg;
	for (int i = 0; i < size; ++i)
	{
		key_desc_t const& k = desc[i];

		//		std::fprintf(stderr, "looking for %s in %s\n", k.name, print_entry(*msg).c_str());

		ret[i] = msg.dict_find(k.name);
		// none_t means any type
		if (ret[i] && ret[i].type() != k.type && k.type != bdecode_node::none_t)
			ret[i].clear();
		if (!ret[i] && (k.flags & key_desc_t::optional) == 0)
		{
			// the key was not found, and it's not an optional key
			std::snprintf(error.data(), static_cast<std::size_t>(error.size()), "missing '%s' key", k.name);
			return false;
		}

		if (k.size > 0
			&& ret[i]
			&& k.type == bdecode_node::string_t)
		{
			bool const invalid = (k.flags & key_desc_t::size_divisible)
				? (ret[i].string_length() % k.size) != 0
				: ret[i].string_length() != k.size;

			if (invalid)
			{
				// the string was not of the required size
				ret[i].clear();
				if ((k.flags & key_desc_t::optional) == 0)
				{
					std::snprintf(error.data(), static_cast<std::size_t>(error.size())
						, "invalid value for '%s'", k.name);
					return false;
				}
			}
		}
		if (k.flags & key_desc_t::parse_children)
		{
			TORRENT_ASSERT(k.type == bdecode_node::dict_t);

			if (ret[i])
			{
				++stack_ptr;
				TORRENT_ASSERT(stack_ptr < int(sizeof(stack) / sizeof(stack[0])));
				msg = ret[i];
				stack[stack_ptr] = msg;
			}
			else
			{
				// skip all children
				while (i < size && (desc[i].flags & key_desc_t::last_child) == 0) ++i;
				// if this assert is hit, desc is incorrect
				TORRENT_ASSERT(i < size);
			}
		}
		else if (k.flags & key_desc_t::last_child)
		{
			TORRENT_ASSERT(stack_ptr > 0);
			// this can happen if the specification passed
			// in is unbalanced. i.e. contain more last_child
			// nodes than parse_children
			if (stack_ptr == 0) return false;
			--stack_ptr;
			msg = stack[stack_ptr];
		}
	}
	return true;
}

} }
