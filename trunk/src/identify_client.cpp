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
	using ::toupper;
	using ::isalnum;
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

		if (id[0] != '-' || !std::isprint(id[1]) || !std::isprint(id[2])
			|| !std::isalnum(id[3]) || !std::isalnum(id[4])
			|| !std::isalnum(id[5]) || !std::isalnum(id[6])
			|| id[7] != '-')
			return boost::optional<fingerprint>();

		ret.id[0] = id[1];
		ret.id[1] = id[2];
		ret.major_version = decode_digit(id[3]);
		ret.minor_version = decode_digit(id[4]);
		ret.revision_version = decode_digit(id[5]);
		ret.tag_version = decode_digit(id[6]);

		return boost::optional<fingerprint>(ret);
	}

	// checks if a peer id can possibly contain a shadow-style
	// identification
	boost::optional<fingerprint> parse_shadow_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);

		if (!std::isalnum(id[0]))
			return boost::optional<fingerprint>();

		if (std::equal(id.begin()+4, id.begin()+8, "----"))
		{
			if (!std::isalnum(id[1]) || !std::isalnum(id[2])
				|| !std::isalnum(id[3]))
				return boost::optional<fingerprint>();
			ret.major_version = decode_digit(id[1]);
			ret.minor_version = decode_digit(id[2]);
			ret.revision_version = decode_digit(id[3]);
		}
		else
		{
			if (id[8] != 0 || id[1] > 127 || id[2] > 127 || id[3] > 127)
				return boost::optional<fingerprint>();
			ret.major_version = id[1];
			ret.minor_version = id[2];
			ret.revision_version = id[3];
		}

		ret.id[0] = id[0];
		ret.id[1] = 0;

		ret.tag_version = 0;
		return boost::optional<fingerprint>(ret);
	}

	// checks if a peer id can possibly contain a mainline-style
	// identification
	boost::optional<fingerprint> parse_mainline_style(const peer_id& id)
	{
		if (!std::isprint(id[0])
			|| !std::isalnum(id[1])
			|| id[2] != '-'
			|| !std::isalnum(id[3])
			|| id[4] != '-'
			|| !std::isalnum(id[5])
			|| !std::equal(id.begin() + 6, id.begin() + 8, "--"))
			return boost::optional<fingerprint>();

		fingerprint ret("..", 0, 0, 0, 0);

		ret.id[0] = id[0];
		ret.id[1] = 0;
		ret.major_version = decode_digit(id[1]);
		ret.minor_version = decode_digit(id[3]);
		ret.revision_version = decode_digit(id[5]);
		ret.tag_version = 0;
		return boost::optional<fingerprint>(ret);
	}

	typedef std::pair<char const*, char const*> map_entry;

	// only support BitTorrentSpecification
	// must be ordered alphabetically
	map_entry name_map[] =
	{
		map_entry("A",  "ABC")
		, map_entry("AZ", "Azureus")
		, map_entry("BB", "BitBuddy")
		, map_entry("BX", "BittorrentX")
		, map_entry("CT", "CTorrent")
		, map_entry("LT", "libtorrent")
		, map_entry("M",  "Mainline")
		, map_entry("MT", "Moonlight Torrent")
		, map_entry("S",  "Shadow")
		, map_entry("SS", "SwarmScope")
		, map_entry("T",  "BitTornado")
		, map_entry("TN", "TorrentDotNET")
		, map_entry("TS", "TorrentStorm")
		, map_entry("U",  "UPnP")
		, map_entry("XT", "XanTorrent")
	};

	bool compare_first_string(map_entry const& e, char const* str)
	{
		return e.first[0] < str[0]
			|| ((e.first[0] == str[0]) && (e.first[1] < str[1]));
	}

	std::string lookup(fingerprint const& f)
	{
		std::stringstream identity;

		const int size = sizeof(name_map)/sizeof(name_map[0]);
		map_entry* i =
			std::lower_bound(name_map, name_map + size
				, f.id, &compare_first_string);

		if (i < name_map + size && std::equal(f.id, f.id + 2, i->first))
			identity << i->second;
		else
			identity << std::string(f.id, f.id + 2);

		identity << " " << (int)f.major_version
			<< "." << (int)f.minor_version
			<< "." << (int)f.revision_version
			<< "." << (int)f.tag_version;

		return identity.str();
	}

	bool find_string(unsigned char const* id, char const* search)
	{
		return std::equal(search, search + std::strlen(search), id);
	}
}

namespace libtorrent
{

	std::string identify_client(const peer_id& p)
	{
		peer_id::const_iterator PID = p.begin();
		boost::optional<fingerprint> f;

		if (p.is_all_zeros()) return "Unknown";

		// look for azureus style id
		f = parse_az_style(p);
		if (f) return lookup(*f);

		// look for shadow style id
		f = parse_shadow_style(p);
		if (f) return lookup(*f);

		// look for mainline style id
		f = parse_mainline_style(p);
		if (f) return lookup(*f);

		// ----------------------
		// non standard encodings
		// ----------------------

		if (find_string(PID, "Deadman Walking-")) return "Deadman";
		if (find_string(PID + 5, "Azureus")) return "Azureus 2.0.3.2";
		if (find_string(PID, "DansClient")) return "XanTorrent";
		if (find_string(PID + 4, "btfans")) return "SimpleBT";
		if (find_string(PID, "PRC.P---")) return "Bittorrent Plus! II";
		if (find_string(PID, "P87.P---")) return "Bittorrent Plus!";
		if (find_string(PID, "S587Plus")) return "Bittorrent Plus!";
		if (find_string(PID, "martini")) return "Martini Man";
		if (find_string(PID, "Plus---")) return "Bittorrent Plus";
		if (find_string(PID, "turbobt")) return "TurboBT";
		if (find_string(PID, "BTDWV-")) return "Deadman Walking";
		if (find_string(PID + 2, "BS")) return "BitSpirit";
		if (find_string(PID, "btuga")) return "BTugaXP";
		if (find_string(PID, "oernu")) return "BTugaXP";
		if (find_string(PID, "Mbrst")) return "Burst!";
		if (find_string(PID, "Plus")) return "Plus!";
		if (find_string(PID, "exbc")) return "BitComet";
		if (find_string(PID, "-G3")) return "G3 Torrent";
		if (find_string(PID, "XBT")) return "XBT";

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\x97"))
			return "Experimental 3.2.1b2";

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\0"))
			return "Experimental 3.1";

		if (std::equal(PID, PID + 12, "\0\0\0\0\0\0\0\0\0\0\0\0"))
			return "Generic";

		std::string unknown("Unknown [");
		for (peer_id::const_iterator i = p.begin(); i != p.end(); ++i)
		{
			unknown += std::isprint(*i)?*i:'.';
		}
		unknown += "]";
		return unknown;
	}

}
