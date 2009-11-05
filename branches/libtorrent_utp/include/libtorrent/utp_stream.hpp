#ifndef TORRENT_UTP_STREAM_HPP_INCLUDED
#define TORRENT_UTP_STREAM_HPP_INCLUDED

#include "libtorrent/connection_queue.hpp"
#include "libtorrent/proxy_base.hpp"
#include "libtorrent/udp_socket.hpp"

#define CCONTROL_TARGET 100

namespace libtorrent
{

struct utp_header
{
	int ver;
	enum { ST_DATA = 0, ST_FIN, ST_STATE, ST_RESET, ST_SYN } type;
	int extension;
	int connection_id;
	int timestamp_microseconds;
	int timestamp_difference_microseconds;
	int wnd_size;
	int seq_nr;
	int ack_nr;
};

struct utp_socket
{
	udp_socket& socket;
	int state;
	int seq_nr;
	int ack_nr;
	int conn_id_recv;
	int conn_id_send;
};

class utp_stream : public proxy_base
{
public:

	explicit utp_stream(io_service& ios, connection_queue& cc)
		: proxy_base(ios)
		, m_sock(ios, boost::bind(&utp_stream::on_receive, this, _1, _2, _3, _4), cc)
		, m_state(action_error)
	{}

	typedef boost::function<void(error_code const&)> handler_type;

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		m_remote_endpoint = udp::endpoint(endpoint.address(), endpoint.port());

		// the connect is split up in the following steps:
		// 1. 

		// to avoid unnecessary copying of the handler,
		// store it in a shaed_ptr
		boost::shared_ptr<handler_type> h(new handler_type(handler));
		
		if (!m_sock.is_open()) return; // the operation was aborted
		
		error_code ec;
		char buf[30];
		m_sock.send(m_remote_endpoint, buf, 16, ec);
		
		m_state = action_connect;
	}
	
	void bind(endpoint_type const& ep, error_code& ec);
	void bind(udp::endpoint const& ep, error_code& ec);
	void on_receive(error_code const& e, udp::endpoint const& ep
			, char const* buf, int size);

	~utp_stream();
	
private:

	enum action_t
	{
		action_connect,
		action_error
	};

	udp_socket m_sock;
	udp::endpoint m_remote_endpoint;
	action_t m_state;
};

}

#endif
