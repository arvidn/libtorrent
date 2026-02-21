/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_INTRUSIVE_LIST_HPP
#define LIBTORRENT_INTRUSIVE_LIST_HPP

#include <memory>
#include <iterator>
#include "libtorrent/assert.hpp"

namespace libtorrent::aux {
// The `ownership_intrusive_list` class implements an intrusive list with smart_ptr ownership.
//
// Complexity:
// - Pointer based lookups: O(1)
// - Additions: O(1)
// - Removals: O(1)
// - Destruction: O(1) per element; O(n) for n elements
//
// Storage Overhead:
// - Requires 2 pointers per object
//
// Disadvantages:
// - Each object can belong to only one `ownership_intrusive_list`, or it incurs additional storage overhead for multiple lists.
// - Objects not in any `ownership_intrusive_list` still incur storage overhead.
//
// The primary use case is managing the lifetimes for a list of `unique_ptr` objects.
//
// Comparisons with `vector<unique_ptr>`:
// - `vector` has O(n) lookup complexity. Removing O(n) items randomly results in O(n^2) complexity through lookup operations.
// - In optimal cases, `vector` needs only 1 pointer per object, but it can reserve double the storage due to amortized overallocation.
//
// Comparisons with `unordered_map<string/hash, unique_ptr>`:
// - If implemented optimally, the complexities are similar.
// - `unordered_map` has greater storage overhead per node (including key storage, key pointer, value pointer, and next pointer) and also utilizes amortized overallocation.
//
// Use `ownership_intrusive_list` if you need to store a list of unique_ptrs and don't require key-based lookups.

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
			!n.next_, "Overwriting 'next' pointer risks accidentally deleting all list-items after this one recursively"
					  " and overflowing the stack");
		n.next_ = std::move(next);
		return *n.next_;
	}

	static node* ptr(const node_ownership& item) noexcept { return item.get(); }
	static node& ref(const node_ownership& item) noexcept { return *item; }

	static node* get_next(const node& n)          noexcept { return n.next_.get(); }
	static node* get_previous(const node& n)      noexcept { return n.prev_; }
	static void set_previous(node& n, node* prev) noexcept { n.prev_ = prev; }
};

template<typename value_traits>
class ownership_intrusive_list {
	using node = typename value_traits::node;
	using node_ownership = typename value_traits::node_ownership;

	// an exception in one of these functions would leave the list in an unspecified state
	static_assert(std::is_nothrow_move_constructible_v<node_ownership>
		&& std::is_nothrow_invocable_v<decltype(value_traits::take_next_ownership), node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::set_next), node&, node_ownership>
		&& std::is_nothrow_invocable_v<decltype(value_traits::get_previous), const node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::get_next), const node&>
		&& std::is_nothrow_invocable_v<decltype(value_traits::set_previous), node&, node*>
		&& std::is_nothrow_invocable_v<decltype(value_traits::ptr), const node_ownership &>
		&& std::is_nothrow_invocable_v<decltype(value_traits::ref), const node_ownership &>);

	// tail is stored as the previous() of the head
	// tail has an empty next() pointer
	// tail is equal to the head for a list with 1 item (self-referential)
	node_ownership m_head;

	// optionally make this template dependant to optimize for space
	std::size_t m_size = 0;

	void set_tail(node& item) noexcept
	{
		node* head = value_traits::ptr(m_head);
		TORRENT_ASSERT(head);
		value_traits::set_previous(*head, &item);
	}

	[[maybe_unused]] bool is_head(node& item) const noexcept
	{
		return &item == value_traits::ptr(m_head);
	}

public:
	[[nodiscard]] node* tail() const noexcept
	{
		return empty() ? nullptr : value_traits::get_previous(value_traits::ref(m_head));
	}

	[[nodiscard]] node* head() const noexcept
	{
		return value_traits::ptr(m_head);
	}

	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_size;
	}

	void clear() noexcept
	{
		while (auto h = value_traits::ptr(m_head))
		{
			m_head = value_traits::take_next_ownership( *h );
		}
		m_size = 0;
	}

	~ownership_intrusive_list() noexcept
	{
		clear();
	}

	[[nodiscard]] bool empty() const noexcept { return value_traits::ptr(m_head) == nullptr; }

	// add item to the back of the list
	node& add(node_ownership new_tail) noexcept
	{
		[[maybe_unused]] auto new_tail_ptr = value_traits::ptr(new_tail);
		TORRENT_ASSERT(new_tail_ptr);
		TORRENT_ASSERT_PRECOND_MSG(!value_traits::get_next(*new_tail_ptr) && !value_traits::get_previous(*new_tail_ptr),
									"Attempt to add 'dirty' item that already belongs/belonged to a list");

		++m_size;
		if (empty())
		{
			m_head = std::move(new_tail);
			node& new_head_ref = value_traits::ref(m_head);
			// this must self reference the new head
			set_tail(new_head_ref);
			return new_head_ref;
		}
		else
		{
			node* old_tail = tail();
			TORRENT_ASSERT(old_tail);
			node& new_tail_ref = value_traits::set_next(*old_tail, std::move(new_tail));
			value_traits::set_previous(new_tail_ref, old_tail);
			set_tail(new_tail_ref);
			return new_tail_ref;
		}
	}

	node_ownership remove(std::reference_wrapper<node> item) noexcept
	{
		TORRENT_ASSERT_PRECOND_MSG(value_traits::ptr(m_head), "cannot remove list-item from empty list");
		node* next = value_traits::get_next(item);
		node* prev = value_traits::get_previous(item);
		TORRENT_ASSERT_PRECOND_MSG(prev, "list-item should have a previous node, is it not part of any list?");

		[[maybe_unused]] const bool removing_this_list_head = is_head(item);
		[[maybe_unused]] const bool removing_this_list_tail =
			(value_traits::get_previous(value_traits::ref(m_head)) == &item.get());

		// 'prev' of 'head' list-item points to 'tail', and the 'tail' list-item is the only node with no 'next'
		const bool removing_list_head = (value_traits::get_next(*prev) == nullptr);
		const bool removing_list_tail = (next == nullptr);

		TORRENT_ASSERT_PRECOND_MSG(removing_this_list_head == removing_list_head,
									"list-item is a head, but for a different list");
		TORRENT_ASSERT_PRECOND_MSG(removing_this_list_tail == removing_list_tail,
									"list-item is a tail, but for a different list");

		node_ownership item_ownership;
		if (!removing_list_head)
		{
			// remove item
			item_ownership = value_traits::take_next_ownership(*prev);
			item = value_traits::ref(item_ownership);

			if (removing_list_tail)
			{
				set_tail(*prev);
			}
			else
			{
				// inner node, link neighbors
				next = &value_traits::set_next(*prev, value_traits::take_next_ownership(item));
				value_traits::set_previous(*next, prev);
			}
		}
		else
		{
			// removing the head
			item_ownership = std::move(m_head);
			item = value_traits::ref(item_ownership);

			if (removing_list_tail)
			{
				// list is now empty
			}
			else
			{
				// set new head
				m_head = value_traits::take_next_ownership(item);
				// "prev" was the tail of previous head and is still valid, update the new head to point to this tail
				// If the list has only 1 item, this must self reference the new head
				set_tail(*prev);
			}
		}

		TORRENT_ASSERT_PRECOND_MSG(!value_traits::get_next(item),
									"removed list-item but haven't transferred ownership of the 'next' list-item");

		value_traits::set_previous(item, nullptr);
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

	[[nodiscard]] iterator begin() noexcept { return iterator{ value_traits::ptr(m_head) }; }
	[[nodiscard]] iterator end()   noexcept { return iterator{nullptr}; }

	[[nodiscard]] const_iterator begin() const noexcept { return const_iterator{ value_traits::ptr(m_head) }; }
	[[nodiscard]] const_iterator end()   const noexcept { return const_iterator{nullptr}; }

	[[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
	[[nodiscard]] const_iterator cend()   const noexcept { return end(); }

	// invalidates "it" but leaves all other iterators valid, returns a new valid iterator to the next element
	iterator remove(iterator it) noexcept
	{
		TORRENT_ASSERT_PRECOND_MSG(it != end(), "Attempt to remove invalid iterator");
		iterator next = it;
		++next;
		remove(*it.m_node);
		return next;
	}
};

template<typename T>
using unique_ptr_intrusive_list = ownership_intrusive_list<unique_ptr_intrusive_list_traits<T>>;
}

#endif //LIBTORRENT_INTRUSIVE_LIST_HPP
