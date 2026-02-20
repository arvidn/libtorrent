/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/intrusive_list.hpp"
#include "test.hpp"

using namespace libtorrent::aux;

TORRENT_TEST(intrusive_list)
{
	struct list_item : public unique_ptr_intrusive_list_base<list_item> {
		~list_item()
		{
			// avoid recursive deletions
			TEST_EQUAL(next_.get(), nullptr);
		}
	};
	using traits = unique_ptr_intrusive_list_traits<list_item>;
	using list_type = ownership_intrusive_list<unique_ptr_intrusive_list_traits<list_item>>;

	auto len = [](list_type& list) {
		int count = 0;
		int size = static_cast<int>(list.size());
		for ([[maybe_unused]] auto const& item : list) {
			++count;
		}
		TEST_EQUAL(size, count);
		return count;
	};

	// Add multiple items to the list
	list_type list;
	std::vector<list_item*> pointers;

	auto add = [&]() {
		auto old_tail = list.tail();
		auto item = std::make_unique<list_item>();
		auto item_ptr_before = item.get();
		auto item_ptr = &list.add(std::move(item));
		TEST_EQUAL(item_ptr_before, item_ptr);
		TEST_EQUAL(list.tail(), item_ptr);
		TEST_EQUAL(traits::get_next(*list.tail()), nullptr);
		if (old_tail)
		{
			TEST_EQUAL(traits::get_previous(*item_ptr), old_tail);
			TEST_EQUAL(traits::get_next(*old_tail), item_ptr);
		}
		else
		{
			TEST_EQUAL(traits::get_previous(*item_ptr), item_ptr);
		}
		return item_ptr;
	};

	auto remove = [&](list_item* item) {
		bool is_head = list.head() == item;
		bool is_tail = list.tail() == item;
		auto prev = traits::get_previous(*item);
		auto next = traits::get_next(*item);

		(void)list.remove(*item);

		if (is_head && is_tail)
		{
			TEST_EQUAL(list.head(), nullptr);
			TEST_EQUAL(list.tail(), nullptr);
		}
		else if (is_head)
		{
			TEST_EQUAL(list.head(), next);
			TEST_EQUAL(traits::get_previous(*next), prev);
			TEST_EQUAL(traits::get_next(*prev), nullptr);
		}
		else if (is_tail)
		{
			auto new_tail = prev;
			TEST_EQUAL(list.tail(), new_tail);
			TEST_EQUAL(traits::get_next(*new_tail), nullptr);
		}
		else
		{
			TEST_EQUAL(traits::get_previous(*next), prev);
			TEST_EQUAL(traits::get_next(*prev), next);
		}
	};

	auto create_list = [&](int size)
	{
		list.clear();
		pointers.clear();
		TEST_CHECK(list.empty());
		TEST_EQUAL(len(list), 0);

		for (int i = 0; i < size; ++i)
		{
			auto item_ptr = add();
			TEST_EQUAL(len(list), i+1);

			pointers.push_back(item_ptr);
			TEST_EQUAL(list.head(), pointers.front());
		}
		return size;
	};

	// remove in normal order
	int size = create_list(10);
	for (int i = 0, end = size; i < end; ++i)
	{
		remove(pointers[i]);
		TEST_EQUAL(len(list), --size);
	}

	TEST_CHECK(list.empty());
	TEST_EQUAL(len(list), 0);

	// remove in reverse order
	size = create_list(10);
	for (int i = size - 1, end = 0; i >= end; --i)
	{
		remove(pointers[i]);
		TEST_EQUAL(len(list), --size);
	}

	TEST_CHECK(list.empty());
	TEST_EQUAL(len(list), 0);

	// remove interior node
	size = create_list(10);
	for (int i = 1, end = size; i < end; ++i)
	{
		remove(pointers[i]);
		TEST_EQUAL(len(list), --size);
	}

	// remove interior node reverse
	size = create_list(10);
	for (int i = size - 2, end = 0; i >= end; --i)
	{
		remove(pointers[i]);
		TEST_EQUAL(len(list), --size);
	}

	add();
	TEST_EQUAL(len(list), ++size);
	remove(list.head());
	TEST_EQUAL(len(list), --size);
	add();
	TEST_EQUAL(len(list), ++size);
}
