#include "libtorrent/error_code.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/utp_stream.hpp"

using namespace libtorrent;

void on_connect(error_code const& e)
{
}

void on_udp_receive(error_code const& e, udp::endpoint const& ep
		, char const* buf, int size)
{
}

void on_utp_incoming(void* userdata
		, boost::shared_ptr<utp_stream> const& utp_sock)
{
}

int main(int argc, char* argv[])
{
	//int rtt, rtt_var;
	//int max_window, cur_window;
	//int delay_factor, window_factor, scaled_gain;

	/*session s;
	s.listen_on(std::make_pair(6881, 6889));*/

	io_service ios;
	connection_queue cc(ios);
	udp_socket udp_sock(ios, boost::bind(&on_udp_receive, _1, _2, _3, _4), cc);
	
	void* userdata;
	utp_socket_manager utp_sockets(udp_sock, boost::bind(&on_utp_incoming, _1, _2), userdata);
	
	/*error_code ec;
	utp_stream sock(ios, cc);
	sock.bind(udp::endpoint(address_v4::any(), 0), ec);
	
	tcp::endpoint ep(address_v4::from_string("239.192.152.143", ec), 6771);
	
	sock.async_connect(ep, boost::bind(on_connect, _1));*/

	return 0;
}
