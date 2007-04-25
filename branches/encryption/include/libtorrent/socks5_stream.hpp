#include "libtorrent/io.hpp"
#include "libtorrent/socket.hpp"
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>


namespace libtorrent {

class socks5_stream : boost::noncopyable
{
public:

	typedef stream_socket::lowest_layer_type lowest_layer_type;
	typedef stream_socket::endpoint_type endpoint_type;
	typedef stream_socket::protocol_type protocol_type;

	explicit socks5_stream(asio::io_service& io_service)
		: m_sock(io_service)
		, m_resolver(io_service)
	{}

	void set_proxy(std::string hostname, int port)
	{
		m_hostname = hostname;
		m_port = port;
	}

	void set_username(std::string const& user
		, std::string const& password)
	{
		m_user = user;
		m_password = password;
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_read_some(buffers, handler);
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_write_some(buffers, handler);
	}

	void bind(endpoint_type const& endpoint)
	{
		m_sock.bind(endpoint);
	}

	template <class Error_Handler>
	void bind(endpoint_type const& endpoint, Error_Handler const& error_handler)
	{
		m_sock.bind(endpoint, error_handler);
	}

	void open(protocol_type const& p)
	{
		m_sock.open(p);
	}

	template <class Error_Handler>
	void open(protocol_type const& p, Error_Handler const& error_handler)
	{
		m_sock.open(p, error_handler);
	}

	void close()
	{
		m_remote_endpoint = endpoint_type();
		m_sock.close();
	}

	template <class Error_Handler>
	void close(Error_Handler const& error_handler)
	{
		m_sock.close(error_handler);
	}

	endpoint_type remote_endpoint()
	{
		return m_remote_endpoint;
	}

	template <class Error_Handler>
	endpoint_type remote_endpoint(Error_Handler const& error_handler)
	{
		return m_remote_endpoint;
	}

	endpoint_type local_endpoint()
	{
		return m_sock.local_endpoint();
	}

	template <class Error_Handler>
	endpoint_type local_endpoint(Error_Handler const& error_handler)
	{
		return m_sock.local_endpoint(error_handler);
	}

	asio::io_service& io_service()
	{
		return m_sock.io_service();
	}

	lowest_layer_type& lowest_layer()
	{
		return m_sock.lowest_layer();
	}

	typedef boost::function<void(asio::error_code const&)> handler_type;

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		m_remote_endpoint = endpoint;

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to proxy server
		// 3. send SOCKS5 authentication method message
		// 4. read SOCKS5 authentication response
		// 5. send username+password
		// 6. send SOCKS5 CONNECT message

		// to avoid unnecessary copying of the handler,
		// store it in a shaed_ptr
		boost::shared_ptr<handler_type> h(new handler_type(handler));

		tcp::resolver::query q(m_hostname
			, boost::lexical_cast<std::string>(m_port));
		m_resolver.async_resolve(q, boost::bind(
			&socks5_stream::name_lookup, this, _1, _2, h));
	}

private:

	void name_lookup(asio::error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h);
	void connected(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake1(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake2(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake3(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake4(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void socks_connect(boost::shared_ptr<handler_type> h);
	void connect1(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void connect2(asio::error_code const& e, boost::shared_ptr<handler_type> h);
	void connect3(asio::error_code const& e, boost::shared_ptr<handler_type> h);

	stream_socket m_sock;
	// the socks5 proxy
	std::string m_hostname;
	int m_port;
	// send and receive buffer
	std::vector<char> m_buffer;
	// proxy authentication
	std::string m_user;
	std::string m_password;

	endpoint_type m_remote_endpoint;

	tcp::resolver m_resolver;
};

}

