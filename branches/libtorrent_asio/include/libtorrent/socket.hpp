/*

Copyright (c) 2003, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_SOCKET_HPP_INCLUDED
#define TORRENT_SOCKET_HPP_INCLUDED

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/asio.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{
/*
	namespace asio = boost::asio;

	using boost::asio::ipv4::tcp;
	using boost::asio::ipv4::address;
	using boost::asio::stream_socket;
	using boost::asio::datagram_socket;
	using boost::asio::socket_acceptor;
	using boost::asio::demuxer;
	using boost::asio::ipv4::host_resolver;
	using boost::asio::async_write;
	using boost::asio::ipv4::host;
	using boost::asio::deadline_timer;
*/
	namespace asio = ::asio;

	using asio::ipv4::tcp;
	typedef asio::ipv4::tcp::socket stream_socket;
	using asio::ipv4::address;
	typedef asio::ipv4::udp::socket datagram_socket;
	typedef asio::ipv4::tcp::acceptor socket_acceptor;
	typedef asio::io_service demuxer;
	using asio::ipv4::host_resolver;
	using asio::ipv4::host;

	using asio::async_write;
	using asio::deadline_timer;
}

/*
#include <vector>
#include <exception>
#include <string>

#include "libtorrent/config.hpp"

namespace libtorrent
{

	class TORRENT_EXPORT network_error : public std::exception
	{
	public:
		network_error(int error_code): m_error_code(error_code) {}
		virtual const char* what() const throw();
		int error_code() const
		{ return m_error_code; }
	private:
		int m_error_code;
	};

	class socket;

	class TORRENT_EXPORT address
	{
	friend class socket;
	public:
		address();
		address(
			unsigned char a
			, unsigned char b
			, unsigned char c
			, unsigned char d
			, unsigned short port);
		
		address(unsigned int addr, unsigned short port);

		address(const char* addr, unsigned short port);
		address(const address& a);
		~address();

		std::string as_string() const;
		unsigned int ip() const { return m_ip; }

		bool operator<=(const address& a) const
		{ if (ip() == a.ip()) return port <= a.port; else return ip() <= a.ip(); }
		bool operator<(const address& a) const
		{ if (ip() == a.ip()) return port < a.port; else return ip() < a.ip(); }
		bool operator!=(const address& a) const
		{ return ip() != a.ip() || port != a.port; }
		bool operator==(const address& a) const
		{ return ip() == a.ip() && port == a.port; }

		unsigned short port;

		BOOST_STATIC_CONSTANT(unsigned short, any_port = 0);
		BOOST_STATIC_CONSTANT(unsigned int, any_addr = 0);

	private:

		unsigned int m_ip;
	};

	class TORRENT_EXPORT socket: public boost::noncopyable
	{
	friend class address;
	friend class selector;
	public:
		
		enum type
		{
			tcp = 0,
			udp
		};

		socket(type t, bool blocking = true, unsigned short receive_port = 0);
		virtual ~socket();

		void connect(
			const address& addr
			, address const& bind_to
				= address(address::any_addr, address::any_port));
		void close();
		
		void set_blocking(bool blocking);
		bool is_blocking() { return m_blocking; }

		const address& sender() const { assert(m_sender != address()); return m_sender; }
		address name() const;

		void listen(libtorrent::address const& iface, int queue);
		boost::shared_ptr<libtorrent::socket> accept();

		template<class T> int send(const T& buffer);
		template<class T> int send_to(const address& addr, const T& buffer);
		template<class T> int receive(T& buf);

		int send(const char* buffer, int size);
		int send_to(const address& addr, const char* buffer, int size);
		int receive(char* buffer, int size);

		void set_receive_bufsize(int size);
		void set_send_bufsize(int size);

		bool is_readable() const;
		bool is_writable() const;
		bool has_error() const;

		enum error_code
		{
			netdown,
			fault,
			access,
			address_in_use,
			address_not_available,
			in_progress,
			interrupted,
			invalid,
			net_reset,
			not_connected,
			no_buffers,
			operation_not_supported,
			not_socket,
			shutdown,
			would_block,
			connection_reset,
			timed_out,
			connection_aborted,
			message_size,
			not_ready,
			no_support,
			connection_refused,
			is_connected,
			net_unreachable,
			not_initialized,
			host_not_found,
			unknown_error
		};

		error_code last_error() const;

	private:

		socket(int sock, const address& sender, bool blocking);

		int m_socket;
		address m_sender;
		bool m_blocking;

#ifndef NDEBUG
		bool m_connected; // indicates that this socket has been connected
		type m_type;
#endif

	};

	template<class T>
	inline int socket::send(const T& buf)
	{
		return send(reinterpret_cast<const char*>(&buf), sizeof(buf));
	}

	template<class T>
	inline int socket::send_to(const address& addr, const T& buf)
	{
		return send_to(addr, reinterpret_cast<const unsigned char*>(&buf), sizeof(buf));
	}

	template<class T>
	inline int socket::receive(T& buf)
	{
		return receive(reinterpret_cast<char*>(&buf), sizeof(T));
	}


	// timeout is given in microseconds
	// modified is cleared and filled with the sockets that is ready for reading or writing
	// or have had an error

	


	class TORRENT_EXPORT selector
	{
	public:

		void monitor_readability(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			assert(std::find(m_readable.begin(), m_readable.end(), s) == m_readable.end());
			m_readable.push_back(s);
		}
		void monitor_writability(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			assert(std::find(m_writable.begin(), m_writable.end(), s) == m_writable.end());
			m_writable.push_back(s);
		}
		void monitor_errors(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			assert(std::find(m_error.begin(), m_error.end(), s) == m_error.end());
			m_error.push_back(s);
		}

		void remove(boost::shared_ptr<socket> s);

		void remove_writable(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			m_writable.erase(std::find(m_writable.begin(), m_writable.end(), s));
		}

		void remove_readable(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			m_readable.erase(std::find(m_readable.begin(), m_readable.end(), s));
		}

		bool is_writability_monitored(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			return std::find(m_writable.begin(), m_writable.end(), s)
				!= m_writable.end();
		}

		bool is_readability_monitored(boost::shared_ptr<socket> s)
		{
			boost::mutex::scoped_lock l(m_mutex);
			return std::find(m_readable.begin(), m_readable.end(), s)
				!= m_readable.end();
		}

		void wait(int timeout
			, std::vector<boost::shared_ptr<socket> >& readable
			, std::vector<boost::shared_ptr<socket> >& writable
			, std::vector<boost::shared_ptr<socket> >& error);

		int count_read_monitors() const
		{
			boost::mutex::scoped_lock l(m_mutex);
			return (int)m_readable.size();
		}

	private:

		mutable boost::mutex m_mutex;
		std::vector<boost::shared_ptr<socket> > m_readable;
		std::vector<boost::shared_ptr<socket> > m_writable;
		std::vector<boost::shared_ptr<socket> > m_error;
	};

}
*/

#endif // TORRENT_SOCKET_HPP_INCLUDED

