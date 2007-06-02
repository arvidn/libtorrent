#include "libtorrent/aux_/allocate_resources_impl.hpp"
#include <boost/utility.hpp>

#include "test.hpp"

using namespace libtorrent;

struct resource_entry
{
	resource_entry(resource_request r_): r(r_) {}
	resource_request r;
};

void fill_client_vector(std::vector<resource_entry>& v)
{
	v.push_back(resource_request(5000, 20, 20000, 10000));
	v.push_back(resource_request(9000, 20, 20000, 10000));
	v.push_back(resource_request(8000, 20, 20000, 10000));
	v.push_back(resource_request(7000, 20, 20000, 10000));
	v.push_back(resource_request(5000, 20, 20000, 10000));
	v.push_back(resource_request(8000, 20, 20000, 10000));
}

void check_client_vec(std::vector<resource_entry> const& v, int resources)
{
	int sum = 0;
	int min_sum = 0;
	for (std::vector<resource_entry>::const_iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		TEST_CHECK(i->r.given >= i->r.min);
		TEST_CHECK(i->r.given <= i->r.max);
		sum += i->r.given;
		min_sum += i->r.min;
	}
	TEST_CHECK(sum <= (std::max)(resources, min_sum));
}

int test_main()
{
	using namespace libtorrent;

	std::vector<resource_entry> clients;
	fill_client_vector(clients);

	using aux::allocate_resources_impl;

	allocate_resources_impl(20, clients.begin(), clients.end(), &resource_entry::r);
	check_client_vec(clients, 20);
	
	fill_client_vector(clients);
	allocate_resources_impl(20000, clients.begin(), clients.end(), &resource_entry::r);
	check_client_vec(clients, 20000);

	return 0;
}

