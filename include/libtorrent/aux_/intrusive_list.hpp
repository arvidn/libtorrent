/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_INTRUSIVE_LIST_HPP_INCLUDED
#define TORRENT_INTRUSIVE_LIST_HPP_INCLUDED

#include <memory>
#include <iterator>
#include "libtorrent/assert.hpp"

namespace libtorrent::aux {
// The `ownership_intrusive_list` class implements an intrusive list with smart_ptr ownership.
//
// Storage Overhead:
// - Requires 2 pointers per object
//
// Disadvantages:
// - Objects not in any `ownership_intrusive_list` still incur storage overhead of next/prev pointers.
// - Each object can belong to only one `ownership_intrusive_list` when using unique_ptr.

template<typename T>
struct unique_ptr_intrusive_list_base {
	T* prev_ = nullptr;
	std::unique_ptr<T> next_;
};

// This type trait requires that T inherits from unique_ptr_intrusive_list_base
// note: additional type traits can be defined that can allow storing a list in a member variable or with a
// different type of smart_ptr
template<typename T>
struct unique_ptr_intrusive_list_traits {
	using node = T;
	using node_ownership = std::unique_ptr<T>;

	static_assert(std::is_base_of_v<unique_ptr_intrusive_list_base<T>, T>);

	// for the move operations to be noexcept
	static_assert(std::is_nothrow_move_constructible_v<node_ownership>);

	static node_ownership take_next_ownership(node& n) noexcept
	{
		return std::move(n.next_);
	}

	static node& set_next(node& n, node_ownership next) noexcept
	{
		TORRENT_ASSERT_PRECOND_MSG(
			next, "set_next(n, nullptr) is a no-op");

		TORRENT_ASSERT_PRECOND_MSG(
			!n.next_, "Overwriting 'next' pointer risks accidentally deleting all list-items after this one recursively"
					  " and overflowing the stack");

		n.next_ = std::move(next);
		return *n.next_;
	}

	static node* get_next(const node& n)          noexcept { return n.next_.get(); }
	static node* get_previous(const node& n)      noexcept { return n.prev_; }
	static void set_previous(node& n, node* prev) noexcept { n.prev_ = prev; }
};

template<typename value_traits>
class ownership_intrusive_list {
	using node = typename value_traits::node;
	using node_ownership = typename value_traits::node_ownership;

	// an exception in one of these functions would leave the list in an unspecified state
	static_assert(
		// must be able to transfer ownership
		std::is_nothrow_move_constructible_v<node_ownership>
		// support pointer operations
		&& std::is_nothrow_invocable_v<decltype(&node_ownership::operator*), const node_ownership&>
		&& std::is_nothrow_invocable_v<decltype(&node_ownership::get), const node_ownership&>
		// The value trait hides the next/prev pointer implementation from the list logic
		// Make sure these functions exist on the value trait
		&& std::is_nothrow_invocable_v<decltype(value_traits::take_next_ownership), node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::set_next), node&, node_ownership>
		&& std::is_nothrow_invocable_v<decltype(value_traits::get_next), const node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::get_previous), const node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::set_previous), node&, node*>);

	node_ownership m_head;

	// optionally make this template dependant to optimize for space
	std::size_t m_size = 0;

public:
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_size;
	}

	void clear() noexcept
	{
		while (!empty())
		{
			remove( *m_head );
		}
	}

	~ownership_intrusive_list() noexcept
	{
		clear();
	}

	[[nodiscard]] bool empty() const noexcept { return size() == 0; }

	// Add item to the front of the list
	// Returns a reference to the newly added node, to signal that after transferring ownership, the object is still
	// alive and can be accessed through this reference. Usually the address of the object does not change but that
	// is an implementation detail of the move constructor.
	node& add(node_ownership new_node) noexcept
	{
		TORRENT_ASSERT(new_node);
		TORRENT_ASSERT_PRECOND_MSG(!value_traits::get_next(*new_node) && !value_traits::get_previous(*new_node),
									"Attempt to add 'dirty' item that already belongs/belonged to a list");

		if (empty())
		{
			m_head = std::move(new_node);
		}
		else
		{
			auto& next = value_traits::set_next(*new_node, std::move(m_head));
			m_head = std::move(new_node);
			value_traits::set_previous(next, m_head.get());
		}

		++m_size;
		return *m_head;
	}

	node_ownership remove(std::reference_wrapper<node> item) noexcept
	{
		TORRENT_ASSERT_PRECOND_MSG(m_head.get(), "cannot remove list-item from empty list");

		node* next = value_traits::get_next(item);
		node* prev = value_traits::get_previous(item);

		node_ownership item_ownership;
		if (prev)
		{
			// remove item
			value_traits::set_previous(item, nullptr);
			item_ownership = value_traits::take_next_ownership(*prev);

			if (next)
			{
				// inner node, link neighbors
				next = &value_traits::set_next(*prev, value_traits::take_next_ownership(*item_ownership));
				value_traits::set_previous(*next, prev);
			}
		}
		else
		{
			// removing the head
			item_ownership = std::move(m_head);

			if (next)
			{
				m_head = value_traits::take_next_ownership(*item_ownership);
				value_traits::set_previous(*m_head, nullptr);
			}
		}

		--m_size;
		return item_ownership;
	}

	template<bool IsConst>
	class basic_iterator {
		using node_t = typename value_traits::node;
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = std::conditional_t<IsConst, const node_t, node_t>;
		using difference_type = std::ptrdiff_t;
		using pointer = value_type *;
		using reference = value_type &;

		pointer m_node = nullptr;
		friend class ownership_intrusive_list;

		explicit basic_iterator(pointer n) noexcept : m_node(n)
		{
		}

		basic_iterator() noexcept = default;

		// copy non-const -> const
		template<bool B = IsConst, typename = std::enable_if_t<B> >
		basic_iterator(const basic_iterator<false>& other) noexcept : m_node(other.m_node)
		{
		}

		reference operator*() const noexcept { return *m_node; }
		pointer operator->() const noexcept { return m_node; }

		basic_iterator& operator++() noexcept
		{
			m_node = value_traits::get_next(*m_node);
			return *this;
		}

		basic_iterator operator++(int) noexcept
		{
			basic_iterator tmp = *this;
			++*this;
			return tmp;
		}

		friend bool operator==(const basic_iterator& a, const basic_iterator& b) noexcept
		{
			return a.m_node == b.m_node;
		}

		friend bool operator!=(const basic_iterator& a, const basic_iterator& b) noexcept
		{
			return a.m_node != b.m_node;
		}
	};

	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;

	[[nodiscard]]           iterator begin() noexcept { return iterator{m_head.get()}; }
	[[nodiscard]] constexpr iterator end()   noexcept { return iterator{nullptr}; }

	[[nodiscard]]			const_iterator begin() const noexcept { return const_iterator{m_head.get()}; }
	[[nodiscard]] constexpr const_iterator end()   const noexcept { return const_iterator{nullptr}; }
};

template<typename T>
using unique_ptr_intrusive_list = ownership_intrusive_list<unique_ptr_intrusive_list_traits<T>>;
}

#endif //TORRENT_INTRUSIVE_LIST_HPP_INCLUDED
