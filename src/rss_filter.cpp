/*

Copyright (c) 2013, Arvid Norberg
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

#include "rss_filter.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/alert_handler.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"

namespace libtorrent
{
	struct str_map_t
	{
		char const* str;
		int quality;
		int source;
	};

	str_map_t str_map[] =
	{
		{"hdtv", item_properties::hd720, item_properties::tv},
		{"dsr", item_properties::hd720, item_properties::sattelite},
		{"dsrip", item_properties::hd720, item_properties::sattelite},
		{"hddvd", item_properties::hd720, item_properties::dvd},
		{"dvd", item_properties::sd, item_properties::dvd},
		{"dvd5", item_properties::sd, item_properties::dvd},
		{"dvd9", item_properties::sd, item_properties::dvd},
		{"dvdrip", item_properties::sd, item_properties::dvd},
		{"dvdscr", item_properties::sd, item_properties::dvd},
		{"screener", item_properties::sd, item_properties::dvd},
		{"pal", item_properties::sd, item_properties::unknown},
		{"ntsc", item_properties::sd, item_properties::unknown},
		{"cam", item_properties::sd, item_properties::cam},
		{"hdcam", item_properties::hd720, item_properties::cam},
		{"pdtv", item_properties::sd, item_properties::tv},
		{"tvrip", item_properties::sd, item_properties::tv},
		{"dvbr", item_properties::sd, item_properties::tv},
		{"dvbrip", item_properties::sd, item_properties::tv},
		{"telesync", item_properties::sd, item_properties::telesync},
		{"ts", item_properties::sd, item_properties::telesync},
		{"bdrip", item_properties::hd720, item_properties::bluray},
		{"bdr", item_properties::hd720, item_properties::bluray},
		{"brrip", item_properties::hd720, item_properties::bluray},
		{"bluray", item_properties::hd720, item_properties::bluray},
		{"720p", item_properties::hd720, item_properties::unknown},
		{"720i", item_properties::hd720, item_properties::unknown},
		{"1080p", item_properties::hd1080, item_properties::unknown},
		{"1080i", item_properties::hd1080, item_properties::unknown},
		{"480p", item_properties::sd, item_properties::unknown},
		{"480i", item_properties::sd, item_properties::unknown},
		{"576p", item_properties::sd, item_properties::unknown},
		{"576i", item_properties::sd, item_properties::unknown},
	};

	// these format strings are matches against each individual token
	char const* ep_format[] =
	{
		"s%ue%u", "%ux%u"
	};

	// these format strings arematches against the raw input string
	char const* ep_format_raw[] =
	{
		"%*[^0123456789]%4u%*[-. ]%2u%*[-. ]%2u"
	};

	void handle_str(char const* str, item_properties& p)
	{
		str_map_t* end = str_map + sizeof(str_map)/sizeof(str_map[0]);
		str_map_t* i = std::find_if(str_map, end, boost::bind(&strcmp, boost::bind(&str_map_t::str, _1), str) == 0);
		if (i != end)
		{
			if (i->quality != item_properties::unknown)
				p.quality = i->quality;
			if (i->source != item_properties::unknown)
				p.source = i->source;
		}

		int season;
		int episode;
		int day;
		for (int k = 0; k < sizeof(ep_format)/sizeof(ep_format[0]); ++k)
		{
			int matches = sscanf(str, ep_format[k], &season, &episode, &day);
			if (matches == 2)
			{
				p.season = season;
				p.episode = episode;
				break;
			}
			if (matches == 3)
			{
				p.season = season;
				p.episode = episode * 100 + day;
				break;
			}
		}
	}

	void parse_name(std::string name, item_properties& p)
	{
		char* str = &name[0];
		char* start = str;

		int season;
		int episode;
		int day;
		for (int k = 0; k < sizeof(ep_format_raw)/sizeof(ep_format_raw[0]); ++k)
		{
			int matches = sscanf(str, ep_format_raw[k], &season, &episode, &day);
			if (matches == 2)
			{
				p.season = season;
				p.episode = episode;
				break;
			}
			if (matches == 3)
			{
				p.season = season;
				p.episode = episode * 100 + day;
				break;
			}
		}

		do
		{
			*str = to_lower(*str);
			if (strchr("abcdefghijklmnopqrstuvwxyz0123456789-", *str) == NULL)
			{
				if (str == start)
				{
					++start;
				}
				else
				{
					*str = '\0';
					handle_str(start, p);
					start = str+1;
				}
			}
			++str;
		
		} while (*str);
	}

	rss_filter_handler::rss_filter_handler(alert_handler& h, session& ses)
		: m_handler(h)
		, m_ses(ses)
		, m_next_id(0)
	{
		m_handler.subscribe(this, 0, rss_item_alert::alert_type, 0);
	}

	rss_filter_handler::~rss_filter_handler()
	{
		m_handler.unsubscribe(this);
	}

	std::string normalize_title(std::string title)
	{
		char* dst = &title[0];
		char last = 0;
		for (char* src = &title[0]; *src; ++src)
		{
			char c = to_lower(*src);
			if (strchr("abcdefghijklmnopqrstuvwxyz0123456789-", c) == NULL)
			{
				// collapse spaces to just one space
				if (last == ' ') continue;
				c = ' ';
			}

			*dst++ = c;
			last = c;
		}
		if (last == ' ') --dst;
		*dst = '\0';
		title.resize(dst - &title[0]);
		return title;
	}

	void rss_filter_handler::handle_alert(alert const* a)
	{
		rss_item_alert const* ri = alert_cast<rss_item_alert>(a);
		if (ri == NULL) return;

		item_properties p;
		parse_name(ri->item.title, p);

		std::string exact_title = ri->item.title;
		std::string normalized_title = normalize_title(exact_title);

		for (std::vector<rss_rule_t>::iterator i = m_rules.begin()
			, end(m_rules.end()); i != end; ++i)
		{
			if (i->exact_match)
			{
				if (strstr(exact_title.c_str(), i->search.c_str()) == NULL)
					continue;

				if (strstr(exact_title.c_str(), i->search_not.c_str()) != NULL)
					continue;
			}
			else
			{
				std::string search = normalize_title(i->search);
				if (strstr(normalized_title.c_str(), search.c_str()) == NULL)
					continue;
				std::string search_not = normalize_title(i->search_not);
				if (strstr(normalized_title.c_str(), search_not.c_str()) != NULL)
					continue;
			}

			// it's a match!

			if (i->episode_filter)
			{
				item_properties p;
				parse_name(exact_title, p);

				// when the episode filter is enabled, only
				// download files that has a season and episode
				if (p.season == 0 || p.episode == 0)
					continue;

				// have we already downloaded this episode?
				if (i->downloaded_episodes.count(std::make_pair(p.season, p.episode)))
					continue;

				i->downloaded_episodes.insert(std::make_pair(p.season, p.episode));
			}
			
			add_torrent_params params = i->params;
			params.url = ri->item.url;
			m_ses.async_add_torrent(params);

			// when adding the torrent, stop trying to match it against
			// subsequent rules.
			break;
		}
	}

	rss_rule rss_filter_handler::get_rule(int id) const
	{
		mutex::scoped_lock l(m_mutex);
		for (std::vector<rss_rule_t>::const_iterator i = m_rules.begin()
			, end(m_rules.end()); i != end; ++i)
		{
			if (i->id != id) continue;
			return *i;
		}
	}

	int rss_filter_handler::add_rule(rss_rule const& r)
	{
		mutex::scoped_lock l(m_mutex);

		int id = m_next_id++;
		m_rules.push_back(r);
		m_rules.back().id = id;
		return id;
	}

	void rss_filter_handler::edit_rule(rss_rule const& r)
	{
		mutex::scoped_lock l(m_mutex);
		for (std::vector<rss_rule_t>::iterator i = m_rules.begin()
			, end(m_rules.end()); i != end; ++i)
		{
			if (i->id != r.id) continue;
			*i = r;
			break;
		}
	}

	void rss_filter_handler::remove_rule(int id)
	{
		mutex::scoped_lock l(m_mutex);

		for (std::vector<rss_rule_t>::iterator i = m_rules.begin()
			, end(m_rules.end()); i != end; ++i)
		{
			if (i->id != id) continue;
			m_rules.erase(i);
			break;
		}
	}

	std::vector<rss_rule> rss_filter_handler::get_rules() const
	{
		mutex::scoped_lock l(m_mutex);
		std::vector<rss_rule> ret;

		for (std::vector<rss_rule_t>::const_iterator i = m_rules.begin()
			, end(m_rules.end()); i != end; ++i)
		{
			ret.push_back(*i);
		}
		return ret;
	}

	int rss_filter_handler::num_rules() const
	{
		mutex::scoped_lock l(m_mutex);
		return m_rules.size();
	}
}


