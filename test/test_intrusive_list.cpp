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

	auto tail = [](auto& list)
	{
		// not efficient but the list doesn't track the tail
		list_item* item{};
		for (auto& it : list)
		{
			item = &it;
		}
		return item;
	};

	auto head = [](auto& list)
	{
		list_item* item = &*list.begin();
		return item;
	};

	auto add_item = [&](std::unique_ptr<list_item> item) {
		auto old_tail = tail(list);
		auto item_ptr_before = item.get();
		auto item_ptr = &list.add(std::move(item));

		// test invariants
		TEST_EQUAL(head(list), item_ptr);

		// item pointer was not changed while adding
		TEST_EQUAL(item_ptr_before, item_ptr);

		if (old_tail)
		{
			TEST_EQUAL(tail(list), old_tail);
		}
		else
		{
			TEST_EQUAL(traits::get_next(*item_ptr), nullptr);
		}
		return item_ptr;
	};

	auto add = [&]() {
		return add_item(std::make_unique<list_item>());
	};

	auto remove = [&](list_item* item) {
		bool is_head = head(list) == item;
		bool is_tail = tail(list) == item;
		auto prev = traits::get_previous(*item);
		auto next = traits::get_next(*item);

		auto item_ownership = list.remove(*item);

		// test invariants
		TEST_EQUAL(traits::get_previous(*item_ownership), nullptr);
		TEST_EQUAL(traits::get_next(*item_ownership), nullptr);

		if (is_head && is_tail)
		{
			TEST_EQUAL(head(list), nullptr);
			TEST_EQUAL(tail(list), nullptr);
			TEST_EQUAL(list.size(), 0);
		}
		else if (is_head)
		{
			TEST_EQUAL(prev, nullptr);
			TEST_EQUAL(head(list), next);
			TEST_EQUAL(traits::get_previous(*next), nullptr);
		}
		else if (is_tail)
		{
			auto new_tail = prev;
			TEST_EQUAL(tail(list), new_tail);
			TEST_EQUAL(traits::get_next(*new_tail), nullptr);
		}
		else
		{
			TEST_EQUAL(traits::get_previous(*next), prev);
			TEST_EQUAL(traits::get_next(*prev), next);
		}

		return item_ownership;
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
			pointers.insert(pointers.begin(), item_ptr);
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

	// The following tests do a series of list operations while checking invariants

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

	auto item_ownership = remove(head(list));
	TEST_EQUAL(len(list), --size);

	// re-add the removed item
	add_item(std::move(item_ownership));
	TEST_EQUAL(len(list), ++size);
}
