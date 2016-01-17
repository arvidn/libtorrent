/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include <algorithm>
#include <stdio.h>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/identify_client.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/string_util.hpp"

namespace
{

	using namespace libtorrent;

	int decode_digit(char c)
	{
		if (is_digit(c)) return c - '0';
		return unsigned(c) - 'A' + 10;
	}

	// takes a peer id and returns a valid boost::optional
	// object if the peer id matched the azureus style encoding
	// the returned fingerprint contains information about the
	// client's id
	boost::optional<fingerprint> parse_az_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);

		if (id[0] != '-' || !is_print(id[1]) || (id[2] < '0')
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

		if (!is_alpha(id[0]) && !is_digit(id[0]))
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
			|| !is_print(ret.name[0]))
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
		, {"AG", "Ares"}
		, {"AR", "Arctic Torrent"}
		, {"AT", "Artemis"}
		, {"AV", "Avicora"}
		, {"AX", "BitPump"}
		, {"AZ", "Azureus"}
		, {"A~", "Ares"}
		, {"BB", "BitBuddy"}
		, {"BC", "BitComet"}
		, {"BE", "baretorrent"}
		, {"BF", "Bitflu"}
		, {"BG", "BTG"}
		, {"BL", "BitBlinder"}
		, {"BP", "BitTorrent Pro"}
		, {"BR", "BitRocket"}
		, {"BS", "BTSlave"}
		, {"BT", "BitTorrent"}
		, {"BU", "BigUp"}
		, {"BW", "BitWombat"}
		, {"BX", "BittorrentX"}
		, {"CD", "Enhanced CTorrent"}
		, {"CT", "CTorrent"}
		, {"DE", "Deluge"}
		, {"DP", "Propagate Data Client"}
		, {"EB", "EBit"}
		, {"ES", "electric sheep"}
		, {"FC", "FileCroc"}
		, {"FT", "FoxTorrent"}
		, {"FX", "Freebox BitTorrent"}
		, {"GS", "GSTorrent"}
		, {"HK", "Hekate"}
		, {"HL", "Halite"}
		, {"HN", "Hydranode"}
		, {"IL", "iLivid"}
		, {"KG", "KGet"}
		, {"KT", "KTorrent"}
		, {"LC", "LeechCraft"}
		, {"LH", "LH-ABC"}
		, {"LK", "Linkage"}
		, {"LP", "lphant"}
		, {"LT", "libtorrent"}
		, {"LW", "Limewire"}
		, {"M",  "Mainline"}
		, {"ML", "MLDonkey"}
		, {"MO", "Mono Torrent"}
		, {"MP", "MooPolice"}
		, {"MR", "Miro"}
		, {"MT", "Moonlight Torrent"}
		, {"NX", "Net Transport"}
		, {"O",  "Osprey Permaseed"}
		, {"OS", "OneSwarm"}
		, {"OT", "OmegaTorrent"}
		, {"PD", "Pando"}
		, {"Q",  "BTQueue"}
		, {"QD", "QQDownload"}
		, {"QT", "Qt 4"}
		, {"R",  "Tribler"}
		, {"RT", "Retriever"}
		, {"RZ", "RezTorrent"}
		, {"S",  "Shadow"}
		, {"SB", "Swiftbit"}
		, {"SD", "Xunlei"}
		, {"SK", "spark"}
		, {"SN", "ShareNet"}
		, {"SS", "SwarmScope"}
		, {"ST", "SymTorrent"}
		, {"SZ", "Shareaza"}
		, {"S~", "Shareaza (beta)"}
		, {"T",  "BitTornado"}
		, {"TB", "Torch"}
		, {"TL", "Tribler"}
		, {"TN", "Torrent.NET"}
		, {"TR", "Transmission"}
		, {"TS", "TorrentStorm"}
		, {"TT", "TuoTu"}
		, {"U",  "UPnP"}
		, {"UL", "uLeecher"}
		, {"UM", "uTorrent Mac"}
		, {"UT", "uTorrent"}
		, {"VG", "Vagaa"}
		, {"WT", "BitLet"}
		, {"WY", "FireTorrent"}
		, {"XF", "Xfplay"}
		, {"XL", "Xunlei"}
		, {"XS", "XSwifter"}
		, {"XT", "XanTorrent"}
		, {"XX", "Xtorrent"}
		, {"ZT", "ZipTorrent"}
		, {"lt", "rTorrent"}
		, {"pX", "pHoeniX"}
		, {"qB", "qBittorrent"}
		, {"st", "SharkTorrent"}
	};

	struct generic_map_entry
	{
		int offset;
		char const* id;
		char const* name;
	};
	// non-standard names
	generic_map_entry generic_mappings[] =
	{
		{0, "Deadman Walking-", "Deadman"}
		, {5, "Azureus", "Azureus 2.0.3.2"}
		, {0, "DansClient", "XanTorrent"}
		, {4, "btfans", "SimpleBT"}
		, {0, "PRC.P---", "Bittorrent Plus! II"}
		, {0, "P87.P---", "Bittorrent Plus!"}
		, {0, "S587Plus", "Bittorrent Plus!"}
		, {0, "martini", "Martini Man"}
		, {0, "Plus---", "Bittorrent Plus"}
		, {0, "turbobt", "TurboBT"}
		, {0, "a00---0", "Swarmy"}
		, {0, "a02---0", "Swarmy"}
		, {0, "T00---0", "Teeweety"}
		, {0, "BTDWV-", "Deadman Walking"}
		, {2, "BS", "BitSpirit"}
		, {0, "Pando-", "Pando"}
		, {0, "LIME", "LimeWire"}
		, {0, "btuga", "BTugaXP"}
		, {0, "oernu", "BTugaXP"}
		, {0, "Mbrst", "Burst!"}
		, {0, "PEERAPP", "PeerApp"}
		, {0, "Plus", "Plus!"}
		, {0, "-Qt-", "Qt"}
		, {0, "exbc", "BitComet"}
		, {0, "DNA", "BitTorrent DNA"}
		, {0, "-G3", "G3 Torrent"}
		, {0, "-FG", "FlashGet"}
		, {0, "-ML", "MLdonkey"}
		, {0, "-MG", "Media Get"}
		, {0, "XBT", "XBT"}
		, {0, "OP", "Opera"}
		, {2, "RS", "Rufus"}
		, {0, "AZ2500BT", "BitTyrant"}
		, {0, "btpd/", "BitTorrent Protocol Daemon"}
		, {0, "TIX", "Tixati"}
		, {0, "QVOD", "Qvod"}
	};

	bool compare_id(map_entry const& lhs, map_entry const& rhs)
	{
		return lhs.id[0] < rhs.id[0]
			|| ((lhs.id[0] == rhs.id[0]) && (lhs.id[1] < rhs.id[1]));
	}

	std::string lookup(fingerprint const& f)
	{
		char identity[200];

		const int size = sizeof(name_map)/sizeof(name_map[0]);
		map_entry tmp = {f.name, ""};
		map_entry* i =
			std::lower_bound(name_map, name_map + size
				, tmp, &compare_id);

#ifndef NDEBUG
		for (int i = 1; i < size; ++i)
		{
			TORRENT_ASSERT(compare_id(name_map[i-1]
				, name_map[i]));
		}
#endif

		char temp[3];
		char const* name = 0;
		if (i < name_map + size && std::equal(f.name, f.name + 2, i->id))
		{
			name = i->name;
		}
		else
		{
			// if we don't have this client in the list
			// just use the one or two letter code
			memcpy(temp, f.name, 2);
			temp[2] = 0;
			name = temp;
		}

		int num_chars = snprintf(identity, sizeof(identity), "%s %u.%u.%u", name
			, f.major_version, f.minor_version, f.revision_version);

		if (f.tag_version != 0)
		{
			snprintf(identity + num_chars, sizeof(identity) - num_chars
				, ".%u", f.tag_version);
		}

		return identity;
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

		int num_generic_mappings = sizeof(generic_mappings) / sizeof(generic_mappings[0]);

		for (int i = 0; i < num_generic_mappings; ++i)
		{
			generic_map_entry const& e = generic_mappings[i];
			if (find_string(PID + e.offset, e.id)) return e.name;
		}

		if (find_string(PID, "-BOW") && PID[7] == '-')
			return "Bits on Wheels " + std::string((char const*)PID + 4, (char const*)PID + 7);
		

		if (find_string(PID, "eX"))
		{
			std::string user((char const*)PID + 2, (char const*)PID + 14);
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
			unknown += is_print(char(*i))?*i:'.';
		}
		unknown += "]";
		return unknown;
	}

}
