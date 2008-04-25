#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>
#include <vector>

using namespace libtorrent;

int main()
{
	io_service ios;
	asio::error_code ec;
	std::vector<ip_interface> const& net = enum_net_interfaces(ios, ec);

	address local = guess_local_address(ios);
	std::cout << "Local address: " << local << std::endl;

	address gateway = get_default_gateway(ios, local, ec);
	std::cout << "Default gateway: " << gateway << std::endl;
	std::cout << "================\n";

	std::cout << "interface\tnetmask  \tflags\n";
	for (std::vector<ip_interface>::const_iterator i = net.begin()
		, end(net.end()); i != end; ++i)
	{
		std::cout << i->interface_address << "\t" << i->netmask << "\t";
		if (is_multicast(i->interface_address)) std::cout << "multicast ";
		if (is_local(i->interface_address)) std::cout << "local ";
		if (is_loopback(i->interface_address)) std::cout << "loopback ";
		std::cout << std::endl;
	}

}

