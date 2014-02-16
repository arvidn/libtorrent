/*

Copyright (c) 2010, Arvid Norberg
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

#include "libtorrent/rss.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings.hpp"
#include "libtorrent/alert_types.hpp" // for rss_alert

#include <boost/bind.hpp>
#include <set>
#include <map>
#include <algorithm>

namespace libtorrent {

feed_item::feed_item(): size(-1) {}
feed_item::~feed_item() {}

struct feed_state
{
	feed_state(feed& r)
		: in_item(false)
		, num_items(0)
		, type(none)
		, ret(r)
	{}

	bool in_item;
	int num_items;
	std::string current_tag;
	enum feed_type
	{
		none, atom, rss2
	} type;
	feed_item current_item;
	feed& ret;

	bool is_item(char const* tag) const
	{
		switch (type)
		{
			case atom: return string_equal_no_case(tag, "entry");
			case rss2: return string_equal_no_case(tag, "item");
			default: return false;
		}
	}

	bool is_title(char const* tag) const
	{
		switch (type)
		{
			case atom:
			case rss2: return string_equal_no_case(tag, "title");
			default: return false;
		}
	}

	bool is_url(char const* tag) const
	{
		switch (type)
		{
			case atom:
			case rss2: return string_equal_no_case(tag, "link");
			default: return false;
		}
	}

	bool is_desc(char const* tag) const
	{
		switch (type)
		{
			case atom: return string_equal_no_case(tag, "summary");
			case rss2: return string_equal_no_case(tag, "description")
				|| string_equal_no_case(tag, "media:text");
			default: return false;
		}
	}

	bool is_uuid(char const* tag) const
	{
		switch (type)
		{
			case atom: return string_equal_no_case(tag, "id");
			case rss2: return string_equal_no_case(tag, "guid");
			default: return false;
		}
	}

	bool is_comment(char const* tag) const
	{
		switch (type)
		{
			case atom: return false;
			case rss2: return string_equal_no_case(tag, "comments");
			default: return false;
		}
	}

	bool is_category(char const* tag) const
	{
		switch (type)
		{
			case atom: return false;
			case rss2: return string_equal_no_case(tag, "category");
			default: return false;
		}
	}

	bool is_size(char const* tag) const
	{
		return string_equal_no_case(tag, "size")
		 || string_equal_no_case(tag, "contentlength");
	}

	bool is_hash(char const* tag) const
	{
		return string_equal_no_case(tag, "hash")
			|| string_equal_no_case(tag, "media:hash");
	}

	bool is_ttl(char const* tag) const
	{
		return string_equal_no_case(tag, "ttl");
	}
};

void parse_feed(feed_state& f, int token, char const* name, char const* val)
{
	switch (token)
	{
		case xml_parse_error:
			f.ret.m_error = errors::parse_failed;
			return;
		case xml_start_tag:
		case xml_empty_tag:
		{
			f.current_tag = name;
			if (f.type == feed_state::none)
			{
				if (string_equal_no_case(f.current_tag.c_str(), "feed"))
					f.type = feed_state::atom;
				else if (string_equal_no_case(f.current_tag.c_str(), "rss"))
					f.type = feed_state::rss2;
			}
			if (f.is_item(name)) f.in_item = true;
			return;
		}
		case xml_attribute:
		{
			if (!f.in_item) return;
			if (f.is_url(f.current_tag.c_str())
				&& f.type == feed_state::atom)
			{
				// atom feeds have items like this:
				// <link href="http://..." length="12345"/>
				if (string_equal_no_case(name, "href"))
					f.current_item.url = val;
				else if (string_equal_no_case(name, "length"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			else if (f.type == feed_state::rss2
				&& string_equal_no_case(f.current_tag.c_str(), "enclosure"))
			{
				// rss feeds have items like this:
				// <enclosure url="http://..." length="12345"/>
				if (string_equal_no_case(name, "url"))
					f.current_item.url = val;
				else if (string_equal_no_case(name, "length"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			else if (f.type == feed_state::rss2
				&& string_equal_no_case(f.current_tag.c_str(), "media:content"))
			{
				// rss feeds sometimes have items like this:
				// <media:content url="http://..." filesize="12345"/>
				if (string_equal_no_case(name, "url"))
					f.current_item.url = val;
				else if (string_equal_no_case(name, "filesize"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			return;
		}
		case xml_end_tag:
		{
			if (f.in_item && f.is_item(name))
			{
				f.in_item = false;
				if (!f.current_item.title.empty()
					&& !f.current_item.url.empty())
				{
					f.ret.add_item(f.current_item);
					++f.num_items;
				}
				f.current_item = feed_item();
			}
			f.current_tag = "";
			return;
		}
		case xml_string:
		{
			if (!f.in_item)
			{
				if (f.is_title(f.current_tag.c_str()))
					f.ret.m_title = name;
				else if (f.is_desc(f.current_tag.c_str()))
					f.ret.m_description = name;
				else if (f.is_ttl(f.current_tag.c_str()))
				{
					int tmp = atoi(name);
					if (tmp > 0) f.ret.m_ttl = tmp;
				}
				return;
			}
			if (f.is_title(f.current_tag.c_str()))
				f.current_item.title = name;
			else if (f.is_desc(f.current_tag.c_str()))
				f.current_item.description = name;
			else if (f.is_uuid(f.current_tag.c_str()))
				f.current_item.uuid = name;
			else if (f.is_url(f.current_tag.c_str()) && f.type != feed_state::atom)
				f.current_item.url = name;
			else if (f.is_comment(f.current_tag.c_str()))
				f.current_item.comment = name;
			else if (f.is_category(f.current_tag.c_str()))
				f.current_item.category = name;
			else if (f.is_size(f.current_tag.c_str()))
				f.current_item.size = strtoll(name, 0, 10);
			else if (f.is_hash(f.current_tag.c_str()) && strlen(name) == 40)
			{
				if (!from_hex(name, 40, (char*)&f.current_item.info_hash[0]))
				{
					// hex parsing failed
					f.current_item.info_hash.clear();
				}
			}
			return;
		}
		case xml_declaration_tag: return;
		case xml_comment: return;
	}
}

torrent_handle add_feed_item(session& s, feed_item const& fi
	, add_torrent_params const& tp, error_code& ec)
{
	add_torrent_params p = tp;
	p.url = fi.url;
	p.uuid = fi.uuid;
	// #error figure out how to get the feed url in here
//	p.source_feed_url = ???;
	p.ti.reset();
	p.info_hash.clear();
	p.name = fi.title.c_str();
	return s.add_torrent(p, ec);
}

#ifndef BOOST_NO_EXCEPTIONS
torrent_handle add_feed_item(session& s, feed_item const& fi
	, add_torrent_params const& tp)
{
	error_code ec;
	torrent_handle ret = add_feed_item(s, fi, tp, ec);
	if (ec) throw libtorrent_exception(ec);
	return ret;
}
#endif

boost::shared_ptr<feed> new_feed(aux::session_impl& ses, feed_settings const& sett)
{
	return boost::shared_ptr<feed>(new feed(ses, sett));
}

feed::feed(aux::session_impl& ses, feed_settings const& sett)
	: m_last_attempt(0)
	, m_last_update(0)
	, m_ttl(-1)
	, m_failures(0)
	, m_updating(false)
	, m_settings(sett)
	, m_ses(ses)
{
}

void feed::set_settings(feed_settings const& s)
{
	m_settings = s;
}

void feed::get_settings(feed_settings* s) const
{
	*s = m_settings;
}

feed_handle feed::my_handle()
{
	return feed_handle(boost::weak_ptr<feed>(shared_from_this()));
}

void feed::on_feed(error_code const& ec
	, http_parser const& parser, char const* data, int size)
{
	// enabling this assert makes the unit test a lot more difficult
//	TORRENT_ASSERT(m_updating);
	m_updating = false;

	if (ec && ec != asio::error::eof)
	{
		++m_failures;
		m_error = ec;
		if (m_ses.m_alerts.should_post<rss_alert>())
		{
			m_ses.m_alerts.post_alert(rss_alert(my_handle(), m_settings.url
				, rss_alert::state_error, m_error));
		}
		return;
	}

	if (parser.status_code() != 200)
	{
		++m_failures;
		m_error = error_code(parser.status_code(), get_http_category());
		if (m_ses.m_alerts.should_post<rss_alert>())
		{
			m_ses.m_alerts.post_alert(rss_alert(my_handle(), m_settings.url
				, rss_alert::state_error, m_error));
		}
		return;
	}

	m_failures = 0;

	char* buf = const_cast<char*>(data);

	feed_state s(*this);
	xml_parse(buf, buf + size, boost::bind(&parse_feed, boost::ref(s), _1, _2, _3));

	time_t now = time(NULL);

	if (m_settings.auto_download || m_settings.auto_map_handles)
	{
		for (std::vector<feed_item>::iterator i = m_items.begin()
			, end(m_items.end()); i != end; ++i)
		{
			i->handle = torrent_handle(m_ses.find_torrent(i->uuid.empty() ? i->url : i->uuid));

			// if we're already downloading this torrent, or if we
			// don't have auto-download enabled, just move along to
			// the next one
			if (i->handle.is_valid() || !m_settings.auto_download) continue;

			// has this already been added?
			if (m_added.find(i->url) != m_added.end()) continue;

			// this means we should add this torrent to the session
			add_torrent_params p = m_settings.add_args;
			p.url = i->url;
			p.uuid = i->uuid;
			p.source_feed_url = m_settings.url;
			p.ti.reset();
			p.info_hash.clear();
			p.name = i->title.c_str();

			error_code e;
			torrent_handle h = m_ses.add_torrent(p, e);
			m_ses.m_alerts.post_alert(add_torrent_alert(h, p, e));
			m_added.insert(make_pair(i->url, now));
		}
	}

	m_last_update = now;

	// keep history of the typical feed size times 5
	int max_history = (std::max)(s.num_items * 5, 100);

	// this is not very efficient, but that's probably OK for now
	while (int(m_added.size()) > max_history)
	{
		// loop over all elements and find the one with the lowest timestamp
		// i.e. it was added the longest ago, then remove it
		std::map<std::string, time_t>::iterator i = std::min_element(
			m_added.begin(), m_added.end()
			, boost::bind(&std::pair<const std::string, time_t>::second, _1)
			< boost::bind(&std::pair<const std::string, time_t>::second, _2));
		m_added.erase(i);
	}

	// report that we successfully updated the feed
	if (m_ses.m_alerts.should_post<rss_alert>())
	{
		m_ses.m_alerts.post_alert(rss_alert(my_handle(), m_settings.url
			, rss_alert::state_updated, error_code()));
	}

	// update m_ses.m_next_rss_update timestamps
	// now that we have updated our timestamp
	m_ses.update_rss_feeds();
}

#define TORRENT_SETTING(t, x) {#x, offsetof(feed_settings,x), t},
	bencode_map_entry feed_settings_map[] =
	{
		TORRENT_SETTING(std_string, url)
		TORRENT_SETTING(boolean, auto_download)
		TORRENT_SETTING(boolean, auto_map_handles)
		TORRENT_SETTING(integer, default_ttl)
	};
#undef TORRENT_SETTING

#define TORRENT_SETTING(t, x) {#x, offsetof(feed_item,x), t},
	bencode_map_entry feed_item_map[] =
	{
		TORRENT_SETTING(std_string, url)
		TORRENT_SETTING(std_string, uuid)
		TORRENT_SETTING(std_string, title)
		TORRENT_SETTING(std_string, description)
		TORRENT_SETTING(std_string, comment)
		TORRENT_SETTING(std_string, category)
		TORRENT_SETTING(size_integer, size)
	};
#undef TORRENT_SETTING

#define TORRENT_SETTING(t, x) {#x, offsetof(feed,x), t},
	bencode_map_entry feed_map[] =
	{
		TORRENT_SETTING(std_string, m_title)
		TORRENT_SETTING(std_string, m_description)
		TORRENT_SETTING(time_integer, m_last_attempt)
		TORRENT_SETTING(time_integer, m_last_update)
	};
#undef TORRENT_SETTING

#define TORRENT_SETTING(t, x) {#x, offsetof(add_torrent_params,x), t},
	bencode_map_entry add_torrent_map[] =
	{
		TORRENT_SETTING(std_string, save_path)
		TORRENT_SETTING(size_integer, flags)
	};
#undef TORRENT_SETTING

void feed::load_state(lazy_entry const& rd)
{
	load_struct(rd, this, feed_map, sizeof(feed_map)/sizeof(feed_map[0]));
	lazy_entry const* e = rd.dict_find_list("items");
	if (e)
	{
		m_items.reserve(e->list_size());
		for (int i = 0; i < e->list_size(); ++i)
		{
			if (e->list_at(i)->type() != lazy_entry::dict_t) continue;
			m_items.push_back(feed_item());
			load_struct(*e->list_at(i), &m_items.back(), feed_item_map
				, sizeof(feed_item_map)/sizeof(feed_item_map[0]));

			// don't load duplicates
			if (m_urls.find(m_items.back().url) != m_urls.end())
			{
				m_items.pop_back();
				continue;
			}
			m_urls.insert(m_items.back().url);
		}
	}
	load_struct(rd, &m_settings, feed_settings_map
		, sizeof(feed_settings_map)/sizeof(feed_settings_map[0]));
	e = rd.dict_find_dict("add_params");
	if (e)
	{
		load_struct(*e, &m_settings.add_args, add_torrent_map
			, sizeof(add_torrent_map)/sizeof(add_torrent_map[0]));
	}

	e = rd.dict_find_list("history");
	if (e)
	{
		for (int i = 0; i < e->list_size(); ++i)
		{
			if (e->list_at(i)->type() != lazy_entry::list_t) continue;

			lazy_entry const* item = e->list_at(i);

			if (item->list_size() != 2
				|| item->list_at(0)->type() != lazy_entry::string_t
				|| item->list_at(1)->type() != lazy_entry::int_t)
				continue;

			m_added.insert(std::pair<std::string, time_t>(
				item->list_at(0)->string_value()
				, item->list_at(1)->int_value()));
		}
	}
}

void feed::save_state(entry& rd) const
{
	// feed properties
	save_struct(rd, this, feed_map, sizeof(feed_map)/sizeof(feed_map[0]));

	// items
	entry::list_type& items = rd["items"].list();
	for (std::vector<feed_item>::const_iterator i = m_items.begin()
		, end(m_items.end()); i != end; ++i)
	{
		items.push_back(entry());
		entry& item = items.back();
		save_struct(item, &*i, feed_item_map, sizeof(feed_item_map)/sizeof(feed_item_map[0]));
	}
	
	// settings
	feed_settings sett_def;
	save_struct(rd, &m_settings, feed_settings_map
		, sizeof(feed_settings_map)/sizeof(feed_settings_map[0]), &sett_def);
	entry& add = rd["add_params"];
	add_torrent_params add_def;
	save_struct(add, &m_settings.add_args, add_torrent_map
		, sizeof(add_torrent_map)/sizeof(add_torrent_map[0]), &add_def);

	entry::list_type& history = rd["history"].list();
	for (std::map<std::string, time_t>::const_iterator i = m_added.begin()
		, end(m_added.end()); i != end; ++i)
	{
		history.push_back(entry());
		entry::list_type& item = history.back().list();
		item.push_back(entry(i->first));
		item.push_back(entry(i->second));
	}
}

void feed::add_item(feed_item const& item)
{
	// don't add duplicates
	if (m_urls.find(item.url) != m_urls.end())
		return;

	m_urls.insert(item.url);
	m_items.push_back(item);
}

// returns the number of seconds until trying again
int feed::update_feed()
{
	if (m_updating) return 60;

	m_last_attempt = time(0);
	m_last_update = 0;

	if (m_ses.m_alerts.should_post<rss_alert>())
	{
		m_ses.m_alerts.post_alert(rss_alert(my_handle(), m_settings.url
			, rss_alert::state_updating, error_code()));
	}

	boost::shared_ptr<http_connection> feed(
		new http_connection(m_ses.m_io_service, m_ses.m_half_open
			, boost::bind(&feed::on_feed, shared_from_this()
			, _1, _2, _3, _4)));

	m_updating = true;
	feed->get(m_settings.url, seconds(30), 0, 0, 5, m_ses.m_settings.user_agent);

	return 60 + m_failures * m_failures * 60;
}

void feed::get_feed_status(feed_status* ret) const
{
	ret->items = m_items;
	ret->last_update = m_last_update;
	ret->updating = m_updating;
	ret->url = m_settings.url;
	ret->title = m_title;
	ret->description = m_description;
	ret->error = m_error;
	ret->ttl = m_ttl == -1 ? m_settings.default_ttl : m_ttl;
	ret->next_update = next_update(time(0));
}

int feed::next_update(time_t now) const
{
	if (m_last_update == 0) return m_last_attempt + 60 * 5 - now;
	int ttl = m_ttl == -1 ? m_settings.default_ttl : m_ttl;
	TORRENT_ASSERT((m_last_update + ttl * 60) - now < INT_MAX);
	return int((m_last_update + ttl * 60) - now);
}

// defined in session.cpp
void fun_wrap(bool* done, condition* e, mutex* m, boost::function<void(void)> f);

#define TORRENT_ASYNC_CALL(x) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (!f) return; \
	aux::session_impl& ses = f->session(); \
	ses.m_io_service.post(boost::bind(&feed:: x, f))

#define TORRENT_ASYNC_CALL1(x, a1) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (!f) return; \
	aux::session_impl& ses = f->session(); \
	ses.m_io_service.post(boost::bind(&feed:: x, f, a1))

#define TORRENT_SYNC_CALL1(x, a1) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (f) { \
	bool done = false; \
	aux::session_impl& ses = f->session(); \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond, &ses.mut, boost::function<void(void)>(boost::bind(&feed:: x, f, a1)))); \
	f.reset(); \
	do { ses.cond.wait(l); } while(!done); }

feed_handle::feed_handle(boost::weak_ptr<feed> const& p)
	: m_feed_ptr(p) {}

void feed_handle::update_feed()
{
	TORRENT_ASYNC_CALL(update_feed);
}

feed_status feed_handle::get_feed_status() const
{
	feed_status ret;
	TORRENT_SYNC_CALL1(get_feed_status, &ret);
	return ret;
}

void feed_handle::set_settings(feed_settings const& s)
{
	TORRENT_SYNC_CALL1(set_settings, s);
}

feed_settings feed_handle::settings() const
{
	feed_settings ret;
	TORRENT_SYNC_CALL1(get_settings, &ret);
	return ret;
}

};

