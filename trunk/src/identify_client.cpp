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

#include <cctype>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/identify_client.hpp"
#include "libtorrent/fingerprint.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::isprint;
	using ::isdigit;
}
#endif

namespace
{

	using namespace libtorrent;

	int decode_digit(char c)
	{
		if (std::isdigit(c)) return c - '0';
		return std::toupper(c) - 'A' + 10;
	}

	// takes a peer id and returns a valid boost::optional
	// object if the peer id matched the azureus style encoding
	// the returned fingerprint contains information about the
	// client's id
	boost::optional<fingerprint> parse_az_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);
		peer_id::const_iterator i = id.begin();

		if (*i != '-') return boost::optional<fingerprint>();
		++i;

		for (int j = 0; j < 2; ++j)
		{
			if (!std::isprint(*i)) return boost::optional<fingerprint>();
			ret.id[j] = *i;
			++i;
		}

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.major_version = decode_digit(*i);
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.minor_version = decode_digit(*i);
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.revision_version = decode_digit(*i);
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.tag_version = decode_digit(*i);
		++i;

		if (*i != '-') return boost::optional<fingerprint>();

		return boost::optional<fingerprint>(ret);
	}

	// checks if a peer id can possibly contain a shadow-style
	// identification
	boost::optional<fingerprint> parse_shadow_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);
		peer_id::const_iterator i = id.begin();

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.id[0] = *i;
		ret.id[1] = 0;
		++i;

		if (std::equal(id.begin()+4, id.begin()+8, "----"))
		{
			if (!std::isalnum(*i)) return boost::optional<fingerprint>();
			ret.major_version = decode_digit(*i);
			++i;

			if (!std::isalnum(*i)) return boost::optional<fingerprint>();
			ret.minor_version = decode_digit(*i);
			++i;

			if (!std::isalnum(*i)) return boost::optional<fingerprint>();
			ret.revision_version = decode_digit(*i);
		}
		else if (id[8] == 0)
		{
			if (*i > 127) return boost::optional<fingerprint>();
			ret.major_version = *i;
			++i;

			if (*i > 127) return boost::optional<fingerprint>();
			ret.minor_version = *i;
			++i;

			if (*i > 127) return boost::optional<fingerprint>();
			ret.revision_version = *i;
		}
		else
			return boost::optional<fingerprint>();

		ret.tag_version = 0;
		return boost::optional<fingerprint>(ret);
	}

	// checks if a peer id can possibly contain a mainline-style
	// identification
	boost::optional<fingerprint> parse_mainline_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);
		peer_id::const_iterator i = id.begin();

		if (!std::isprint(*i)) return boost::optional<fingerprint>();
		ret.id[0] = *i;
		ret.id[1] = 0;
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.major_version = decode_digit(*i);
		++i;

		if (*i != '-') return boost::optional<fingerprint>();
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.minor_version = decode_digit(*i);
		++i;

		if (*i != '-') return boost::optional<fingerprint>();
		++i;

		if (!std::isalnum(*i)) return boost::optional<fingerprint>();
		ret.revision_version = decode_digit(*i);
		++i;

		if (!std::equal(i, i+1, "--")) return boost::optional<fingerprint>();

		ret.tag_version = 0;
		return boost::optional<fingerprint>(ret);
	}

} // namespace unnamed

namespace libtorrent
{

	std::string identify_client(const peer_id& p)
	{
		peer_id::const_iterator PID = p.begin();
		boost::optional<fingerprint> f;

		if (p.is_all_zeros()) return "Unknown";

		// look for azureus style id	
		f = parse_az_style(p);
		if (f)
		{
			std::stringstream identity;

			// azureus
			if (std::equal(f->id, f->id+2, "AZ"))
				identity << "Azureus ";

			// BittorrentX
			else if (std::equal(f->id, f->id+2, "BX"))
				identity << "BittorrentX ";

			// libtorrent
			else if (std::equal(f->id, f->id+2, "LT"))
				identity << "libtorrent ";

			// Moonlight Torrent
			else if (std::equal(f->id, f->id+2, "MT"))
				identity << "Moonlight Torrent ";

			// Torrent Storm
			else if (std::equal(f->id, f->id+2, "TS"))
				identity << "TorrentStorm ";

			// SwarmScope
			else if (std::equal(f->id, f->id+2, "SS"))
				identity << "SwarmScope ";

			// XanTorrent
			else if (std::equal(f->id, f->id+2, "XT"))
				identity << "XanTorrent ";

			// unknown client
			else
				identity << std::string(f->id, f->id+2) << " ";

			identity << (int)f->major_version
				<< "." << (int)f->minor_version
				<< "." << (int)f->revision_version
				<< "." << (int)f->tag_version;

			return identity.str();
		}
	

		// look for shadow style id	
		f = parse_shadow_style(p);
		if (f)
		{
			std::stringstream identity;

			// Shadow
			if (std::equal(f->id, f->id+1, "S"))
				identity << "Shadow ";

			// ABC
			else if (std::equal(f->id, f->id+1, "A"))
				identity << "ABC ";

			// UPnP
			else if (std::equal(f->id, f->id+1, "U"))
				identity << "UPnP ";

			// BitTornado
			else if (std::equal(f->id, f->id+1, "T"))
				identity << "BitTornado ";

			// unknown client
			else
				identity << std::string(f->id, f->id+1) << " ";

			identity << (int)f->major_version
				<< "." << (int)f->minor_version
				<< "." << (int)f->revision_version;

			return identity.str();
		}
	
		f = parse_mainline_style(p);
		if (f)
		{
			std::stringstream identity;

			// Mainline
			if (std::equal(f->id, f->id+1, "M"))
				identity << "Mainline ";

			// unknown client
			else
				identity << std::string(f->id, f->id+1) << " ";

			identity << (int)f->major_version
				<< "." << (int)f->minor_version
				<< "." << (int)f->revision_version;

			return identity.str();
		}

		// ----------------------
		// non standard encodings
		// ----------------------

		if (std::equal(PID, PID + 12, "-G3g3rmz    "))
		{
			return "G3 Torrent";
		}

		if (std::equal(PID, PID + 4, "exbc"))
		{
			std::stringstream s;
			s << "BitComet " << (int)PID[4] << "." << (int)PID[5]/10 << (int)PID[5]%10;
			return s.str();
		}

		if (std::equal(PID + 5, PID + 5 + 8, "Azureus"))
		{
			return "Azureus 2.0.3.2";
		}
	
		if (std::equal(PID, PID + 11, "DansClient"))
		{
			return "XanTorrent";
		}

		if (std::equal(PID, PID + 7, "Plus---"))
		{
			return "Bittorrent Plus";
		}

		if (std::equal(PID, PID + 16, "Deadman Walking-"))
		{
			return "Deadman";
		}

		if (std::equal(PID, PID + 7, "btuga"))
		{
			return "BTugaXP";
		}

		if (std::equal(PID, PID + 7, "btfans"))
		{
			return "SimpleBT";
		}

		if (std::equal(PID, PID + 7, "turbobt")
			&& std::isalnum(PID[8])
			&& std::isalnum(PID[10])
			&& std::isalnum(PID[12]))
		{
			std::stringstream s;
			s << "TurboBT " << PID[8] << "." << PID[10] << "." << PID[12];
			return s.str();
		}

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\x97"))
		{
			return "Experimental 3.2.1b2";
		}

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\0"))
		{
			return "Experimental 3.1";
		}

		if (std::equal(PID, PID + 12, "\0\0\0\0\0\0\0\0\0\0\0\0"))
		{
			return "Generic";
		}

		std::string unknown("Unknown [");
		for (peer_id::const_iterator i = p.begin(); i != p.end(); ++i)
		{
			unknown += std::isprint(*i)?*i:'.';
		}
		unknown += "]";
		return unknown;
	}

}
