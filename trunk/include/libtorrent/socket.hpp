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

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include <vector>
#include <exception>
#include <string>

// TODO: support ToS

namespace libtorrent
{

	class network_error : public std::exception
	{
	public:
		network_error(int error_code): m_error_code(error_code) {}
		virtual const char* what() const throw();
	private:
		int m_error_code;
	};

	class socket;

	class address
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

		address(const std::string& addr, unsigned short port);
		address(const address& a);
		~address();

		std::string as_string() const;
		unsigned int ip() const { return m_ip; }
		unsigned short port() const { return m_port; }

		bool operator<(const address& a) const
		{ if (ip() == a.ip()) return port() < a.port(); else return ip() < a.ip(); }
		bool operator!=(const address& a) const
		{ return ip() != a.ip() || port() != a.port(); }
		bool operator==(const address& a) const
		{ return ip() == a.ip() && port() == a.port(); }

	private:

		unsigned int m_ip;
		unsigned short m_port;
	};

	class socket: public boost::noncopyable
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

		void connect(const address& addr);
		void close();
		
		void set_blocking(bool blocking);
		bool is_blocking() { return m_blocking; }

		const address& sender() const { return m_sender; }
		address name() const;

		void listen(unsigned short port, int queue);
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
			unknown_error
		};

		error_code last_error() const;

	private:

		socket(int sock, const address& sender);

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

	


	class selector
	{
	public:

		void monitor_readability(boost::shared_ptr<socket> s) { m_readable.push_back(s); }
		void monitor_writability(boost::shared_ptr<socket> s) { m_writable.push_back(s); }
		void monitor_errors(boost::shared_ptr<socket> s) { m_error.push_back(s); }
/*
		void clear_readable() { m_readable.clear(); }
		void clear_writable() { m_writable.clear(); }
*/
		void remove(boost::shared_ptr<socket> s);

		void remove_writable(boost::shared_ptr<socket> s)
		{ m_writable.erase(std::find(m_writable.begin(), m_writable.end(), s)); }

		bool is_writability_monitored(boost::shared_ptr<socket> s)
		{
			return std::find(m_writable.begin(), m_writable.end(), s)
				!= m_writable.end();
		}

		void wait(int timeout
			, std::vector<boost::shared_ptr<socket> >& readable
			, std::vector<boost::shared_ptr<socket> >& writable
			, std::vector<boost::shared_ptr<socket> >& error);

		int count_read_monitors() const { return (int)m_readable.size(); }

	private:

		std::vector<boost::shared_ptr<socket> > m_readable;
		std::vector<boost::shared_ptr<socket> > m_writable;
		std::vector<boost::shared_ptr<socket> > m_error;
	};

}

#endif // TORRENT_SOCKET_HPP_INCLUDED

