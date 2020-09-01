/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

struct read_bits
{
	read_bits(std::uint8_t const* d, std::size_t s)
		: m_data(d), m_size(s)
	{}

	int read(int bits)
	{
		if (m_size == 0) return 0;
		int ret = 0;
		while (bits > 0 && m_size > 0)
		{
			int const bits_to_copy = std::min(8 - m_bit, bits);
			ret <<= bits_to_copy;
			ret |= ((*m_data) >> m_bit) & ((1 << bits_to_copy) - 1);
			m_bit += bits_to_copy;
			bits -= bits_to_copy;
			if (m_bit == 8)
			{
				--m_size;
				++m_data;
				m_bit = 0;
			}
		}
		return ret;
	}
private:
	std::uint8_t const* m_data;
	std::size_t m_size;
	int m_bit = 0;
};


