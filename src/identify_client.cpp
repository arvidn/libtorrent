/*

Copyright (c) 2003-2018, Arvid Norberg
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
#include <cstdio>
#include <cstring>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/identify_client.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace {

	using namespace libtorrent;

	int decode_digit(std::uint8_t c)
	{
		if (is_digit(char(c))) return c - '0';
		return c - 'A' + 10;
	}

	// takes a peer id and returns a valid boost::optional
	// object if the peer id matched the azureus style encoding
	// the returned fingerprint contains information about the
	// client's id
	boost::optional<fingerprint> parse_az_style(const peer_id& id)
	{
		fingerprint ret("..", 0, 0, 0, 0);

		if (id[0] != '-' || !is_print(char(id[1])) || (id[2] < '0')
			|| (id[3] < '0') || (id[4] < '0')
			|| (id[5] < '0') || (id[6] < '0')
			|| id[7] != '-')
			return boost::optional<fingerprint>();

		ret.name[0] = char(id[1]);
		ret.name[1] = char(id[2]);
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

		if (!is_alpha(char(id[0])) && !is_digit(char(id[0])))
			return boost::optional<fingerprint>();

		if (std::equal(id.begin() + 4, id.begin() + 6, "--"))
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

		ret.name[0] = char(id[0]);
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
		if (sscanf(ids, "%1c%3d-%3d-%3d--", &ret.name[0], &ret.major_version, &ret.minor_version
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
	const map_entry name_map[] =
	{
		  {"7T", "aTorrent for android"}
		, {"A",  "ABC"}
		, {"AB", "AnyEvent BitTorrent"}
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
		, {"FW", "FrostWire"}
		, {"FX", "Freebox BitTorrent"}
		, {"GS", "GSTorrent"}
		, {"HK", "Hekate"}
		, {"HL", "Halite"}
		, {"HN", "Hydranode"}
		, {"IL", "iLivid"}
		, {"KC", "Koinonein"}
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
	const generic_map_entry generic_mappings[] =
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
		const map_entry tmp = {f.name, ""};
		const map_entry* i =
			std::lower_bound(name_map, name_map + size
				, tmp, &compare_id);

#ifndef NDEBUG
		for (int j = 1; j < size; ++j)
		{
			TORRENT_ASSERT(compare_id(name_map[j-1]
				, name_map[j]));
		}
#endif

		char temp[3];
		char const* name = nullptr;
		if (i < name_map + size && std::equal(f.name, f.name + 2, i->id))
		{
			name = i->name;
		}
		else
		{
			// if we don't have this client in the list
			// just use the one or two letter code
			std::memcpy(temp, f.name, 2);
			temp[2] = 0;
			name = temp;
		}

		int num_chars = std::snprintf(identity, sizeof(identity), "%s %d.%d.%d", name
			, f.major_version, f.minor_version, f.revision_version);

		if (f.tag_version != 0)
		{
			std::snprintf(identity + num_chars, sizeof(identity) - aux::numeric_cast<std::size_t>(num_chars)
				, ".%d", f.tag_version);
		}

		return identity;
	}

	bool find_string(char const* id, char const* search)
	{
		return std::equal(search, search + std::strlen(search), id);
	}

} // anonymous namespace

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1

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
		return parse_mainline_style(p);
	}

#endif

	std::string identify_client(peer_id const& p)
	{
		return aux::identify_client_impl(p);
	}

namespace aux {

	std::string identify_client_impl(peer_id const& p)
	{
		char const* PID = p.data();

		if (p.is_all_zeros()) return "Unknown";

		// ----------------------
		// non standard encodings
		// ----------------------

		for (auto const& e : generic_mappings)
		{
			if (find_string(PID + e.offset, e.id)) return e.name;
		}

		if (find_string(PID, "-BOW") && PID[7] == '-')
			return "Bits on Wheels " + std::string(PID + 4, PID + 7);

		if (find_string(PID, "eX"))
		{
			std::string user(PID + 2, 12);
			return std::string("eXeem ('") + user + "')";
		}
		bool const is_equ_zero = std::equal(PID, PID + 12, "\0\0\0\0\0\0\0\0\0\0\0\0");

		if (is_equ_zero && PID[12] == '\x97')
			return "Experimental 3.2.1b2";

		if (is_equ_zero && PID[12] == '\0')
			return "Experimental 3.1";

		// look for azureus style id
		boost::optional<fingerprint> f = parse_az_style(p);
		if (f) return lookup(*f);

		// look for shadow style id
		f = parse_shadow_style(p);
		if (f) return lookup(*f);

		// look for mainline style id
		f = parse_mainline_style(p);
		if (f) return lookup(*f);


		if (is_equ_zero)
			return "Generic";

		std::string unknown("Unknown [");
		for (unsigned char const c : p)
			unknown += is_print(char(c)) ? char(c) : '.';
		unknown += "]";
		return unknown;
	}

} // aux
} // libtorrent
