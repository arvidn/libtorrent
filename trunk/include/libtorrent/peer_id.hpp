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

#ifndef TORRENT_PEER_ID_HPP_INCLUDED
#define TORRENT_PEER_ID_HPP_INCLUDED


namespace libtorrent
{

	class big_number
	{
	// the number of bytes of the number
	enum { number_size = 20 };
	public:

		bool operator==(const big_number& n) const
		{
			return std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator!=(const big_number& n) const
		{
			return !std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator<(const big_number& n) const
		{
			for(int i = 0; i < number_size; ++i)
			{
				if (m_number[i] < n.m_number[i]) return true;
				if (m_number[i] > n.m_number[i]) return false;
			}
			return false;
		}

		const unsigned char* begin() const { return m_number; }
		const unsigned char* end() const { return m_number+number_size; }

		unsigned char* begin() { return m_number; }
		unsigned char* end() { return m_number+number_size; }

	private:

		unsigned char m_number[number_size];

	};

	typedef big_number peer_id;
	typedef big_number sha1_hash;

}

#endif // TORRENT_PEER_ID_HPP_INCLUDED
