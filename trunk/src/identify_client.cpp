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

#include "libtorrent/pch.hpp"

#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/identify_client.hpp"
#include "libtorrent/fingerprint.hpp"

namespace
{

	using namespace libtorrent;

	int decode_digit(char c)
	{
		if (std::isdigit(c)) return c - '0';
		return unsigned(c) - 'A' + 10;
	}

	// takes a peer id and returns a valid boost::optional
	// object if the peer id matched the azureus style encoding
	// the returned fingerprint contains information about the
	// client's id
	boost::optional<fingerprint> parse_az_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);

		if (id[0] != '-' || !std::isprint(id[1]) || (id[2] < '0')
			|| (id[3] < '0') || (id[4] < '0')
			|| (id[5] < '0') || (id[6] < '0')
			|| id[7] != '-')
			return boost::optional<fingerprint>();

		ret.name[0] = id[1];
		ret.name[1] = id[2];
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

		if (std::equal(id.begin()+4, id.begin()+6, "--"))
		{
			if ((id[1] < '0') || (id[2] < '0')
				|| (id[3] < '0'))
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

		ret.name[0] = id[0];
		ret.name[1] = 0;

		ret.tag_version = 0;
		return boost::optional<fingerprint>(ret);
	}

	// checks if a peer id can possibly contain a mainline-style
	// identification
	boost::optional<fingerprint> parse_mainline_style(const peer_id& id)
	{
		char ids[21];
		std::copy(id.begin(), id.end(), ids);
		ids[20] = 0;
		fingerprint ret("..", 0, 0, 0, 0);
		ret.name[1] = 0;
		ret.tag_version = 0;
		if (sscanf(ids, "%c%d-%d-%d--", &ret.name[0], &ret.major_version, &ret.minor_version
			, &ret.revision_version) != 4
			|| !std::isprint(ret.name[0]))
			return boost::optional<fingerprint>();

		return boost::optional<fingerprint>(ret);
	}

	struct map_entry
	{
		char const* id;
		char const* name;
	};

	// only support BitTorrentSpecification
	// must be ordered alphabetically
	map_entry name_map[] =
	{
		{"A",  "ABC"}
		, {"AG",  "Ares"}
		, {"AR", "Arctic Torrent"}
		, {"AV", "Avicora"}
		, {"AX", "BitPump"}
		, {"AZ", "Azureus"}
		, {"A~",  "Ares"}
		, {"BB", "BitBuddy"}
		, {"BC", "BitComet"}
		, {"BF", "Bitflu"}
		, {"BG", "BTG"}
		, {"BR", "BitRocket"}
		, {"BS", "BTSlave"}
		, {"BX", "BittorrentX"}
		, {"CD", "Enhanced CTorrent"}
		, {"CT", "CTorrent"}
		, {"DE", "Deluge Torrent"}
		, {"EB", "EBit"}
		, {"ES", "electric sheep"}
		, {"HL", "Halite"}
		, {"HN", "Hydranode"}
		, {"KT", "KTorrent"}
		, {"LK", "Linkage"}
		, {"LP", "lphant"}
		, {"LT", "libtorrent"}
		, {"M",  "Mainline"}
		, {"ML", "MLDonkey"}
		, {"MO", "Mono Torrent"}
		, {"MP", "MooPolice"}
		, {"MT", "Moonlight Torrent"}
		, {"O",  "Osprey Permaseed"}
		, {"PD",  "Pando"}
		, {"Q", "BTQueue"}
		, {"QT", "Qt 4"}
		, {"R",  "Tribler"}
		, {"S",  "Shadow"}
		, {"SB", "Swiftbit"}
		, {"SN", "ShareNet"}
		, {"SS", "SwarmScope"}
		, {"SZ", "Shareaza"}
		, {"S~",  "Shareaza (beta)"}
		, {"T",  "BitTornado"}
		, {"TN", "Torrent.NET"}
		, {"TR", "Transmission"}
		, {"TS", "TorrentStorm"}
		, {"TT", "TuoTu"}
		, {"U",  "UPnP"}
		, {"UL", "uLeecher"}
		, {"UT", "uTorrent"}
		, {"XT", "XanTorrent"}
		, {"XX", "Xtorrent"}
		, {"ZT", "ZipTorrent"}
		, {"lt", "rTorrent"}
		, {"pX", "pHoeniX"}
		, {"qB", "qBittorrent"}
	};

	bool compare_id(map_entry const& lhs, map_entry const& rhs)
	{
		return lhs.id[0] < rhs.id[0]
			|| ((lhs.id[0] == rhs.id[0]) && (lhs.id[1] < rhs.id[1]));
	}

	std::string lookup(fingerprint const& f)
	{
		std::stringstream identity;

		const int size = sizeof(name_map)/sizeof(name_map[0]);
		map_entry tmp = {f.name, ""};
		map_entry* i =
			std::lower_bound(name_map, name_map + size
				, tmp, &compare_id);

#ifndef NDEBUG
		for (int i = 1; i < size; ++i)
		{
			assert(compare_id(name_map[i-1]
				, name_map[i]));
		}
#endif

		if (i < name_map + size && std::equal(f.name, f.name + 2, i->id))
			identity << i->name;
		else
		{
			identity << f.name[0];
			if (f.name[1] != 0) identity << f.name[1];
		}

		identity << " " << (int)f.major_version
			<< "." << (int)f.minor_version
			<< "." << (int)f.revision_version;

		if (f.name[1] != 0)
			identity << "." << (int)f.tag_version;

		return identity.str();
	}

	bool find_string(unsigned char const* id, char const* search)
	{
		return std::equal(search, search + std::strlen(search), id);
	}
}

namespace libtorrent
{

	boost::optional<fingerprint> client_fingerprint(peer_id const& p)
	{
		// look for azureus style id
		boost::optional<fingerprint> f;
		f = parse_az_style(p);
		if (f) return f;

		// look for shadow style id
		f = parse_shadow_style(p);
		if (f) return f;

		// look for mainline style id
		f = parse_mainline_style(p);
		if (f) return f;
		return f;
	}

	std::string identify_client(peer_id const& p)
	{
		peer_id::const_iterator PID = p.begin();
		boost::optional<fingerprint> f;

		if (p.is_all_zeros()) return "Unknown";

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
		if (find_string(PID, "a00---0")) return "Swarmy";
		if (find_string(PID, "a02---0")) return "Swarmy";
		if (find_string(PID, "T00---0")) return "Teeweety";
		if (find_string(PID, "BTDWV-")) return "Deadman Walking";
		if (find_string(PID + 2, "BS")) return "BitSpirit";
		if (find_string(PID, "Pando-")) return "Pando";
		if (find_string(PID, "LIME")) return "LimeWire";
		if (find_string(PID, "btuga")) return "BTugaXP";
		if (find_string(PID, "oernu")) return "BTugaXP";
		if (find_string(PID, "Mbrst")) return "Burst!";
		if (find_string(PID, "Plus")) return "Plus!";
		if (find_string(PID, "-Qt-")) return "Qt";
		if (find_string(PID, "exbc")) return "BitComet";
		if (find_string(PID, "DNA")) return "BitTorrent DNA";
		if (find_string(PID, "-G3")) return "G3 Torrent";
		if (find_string(PID, "XBT")) return "XBT";
		if (find_string(PID, "OP")) return "Opera";

		if (find_string(PID, "-BOW") && PID[7] == '-')
			return "Bits on Wheels " + std::string(PID + 4, PID + 7);
		

		if (find_string(PID, "eX"))
		{
			std::string user(PID + 2, PID + 14);
			return std::string("eXeem ('") + user.c_str() + "')"; 
		}

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\x97"))
			return "Experimental 3.2.1b2";

		if (std::equal(PID, PID + 13, "\0\0\0\0\0\0\0\0\0\0\0\0\0"))
			return "Experimental 3.1";

		
		// look for azureus style id
		f = parse_az_style(p);
		if (f) return lookup(*f);

		// look for shadow style id
		f = parse_shadow_style(p);
		if (f) return lookup(*f);

		// look for mainline style id
		f = parse_mainline_style(p);
		if (f) return lookup(*f);
														
		
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
