/*

Copyright (c) 2010-2016, Arvid Norberg
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
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp" // for rss_alert

#include <boost/bind.hpp>
#include <set>
#include <map>
#include <algorithm>

#ifndef TORRENT_NO_DEPRECATE

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
			case none: return false;
		}
		return false;
	}

	bool is_title(char const* tag) const
	{
		switch (type)
		{
			case atom:
			case rss2: return string_equal_no_case(tag, "title");
			case none: return false;
		}
		return false;
	}

	bool is_url(char const* tag) const
	{
		switch (type)
		{
			case atom:
			case rss2: return string_equal_no_case(tag, "link");
			case none: return false;
		}
		return false;
	}

	bool is_desc(char const* tag) const
	{
		switch (type)
		{
			case atom: return string_equal_no_case(tag, "summary");
			case rss2: return string_equal_no_case(tag, "description")
				|| string_equal_no_case(tag, "media:text");
			case none: return false;
		}
		return false;
	}

	bool is_uuid(char const* tag) const
	{
		switch (type)
		{
			case atom: return string_equal_no_case(tag, "id");
			case rss2: return string_equal_no_case(tag, "guid");
			case none: return false;
		}
		return false;
	}

	bool is_comment(char const* tag) const
	{
		switch (type)
		{
			case atom: return false;
			case rss2: return string_equal_no_case(tag, "comments");
			case none: return false;
		}
		return false;
	}

	bool is_category(char const* tag) const
	{
		switch (type)
		{
			case atom: return false;
			case rss2: return string_equal_no_case(tag, "category");
			case none: return false;
		}
		return false;
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

void parse_feed(feed_state& f, int token, char const* name, int name_len
	, char const* val, int val_len)
{
	switch (token)
	{
		case xml_parse_error:
			f.ret.m_error = errors::parse_failed;
			return;
		case xml_start_tag:
		case xml_empty_tag:
		{
			f.current_tag.assign(name, name_len);
			if (f.type == feed_state::none)
			{
				if (string_equal_no_case(f.current_tag.c_str(), "feed"))
					f.type = feed_state::atom;
				else if (string_equal_no_case(f.current_tag.c_str(), "rss"))
					f.type = feed_state::rss2;
			}
			if (f.is_item(f.current_tag.c_str())) f.in_item = true;
			return;
		}
		case xml_attribute:
		{
			if (!f.in_item) return;
			std::string str(name, name_len);
			if (f.is_url(f.current_tag.c_str())
				&& f.type == feed_state::atom)
			{
				// atom feeds have items like this:
				// <link href="http://..." length="12345"/>
				if (string_equal_no_case(str.c_str(), "href"))
					f.current_item.url.assign(val, val_len);
				else if (string_equal_no_case(str.c_str(), "length"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			else if (f.type == feed_state::rss2
				&& string_equal_no_case(f.current_tag.c_str(), "enclosure"))
			{
				// rss feeds have items like this:
				// <enclosure url="http://..." length="12345"/>
				if (string_equal_no_case(str.c_str(), "url"))
					f.current_item.url.assign(val, val_len);
				else if (string_equal_no_case(str.c_str(), "length"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			else if (f.type == feed_state::rss2
				&& string_equal_no_case(f.current_tag.c_str(), "media:content"))
			{
				// rss feeds sometimes have items like this:
				// <media:content url="http://..." filesize="12345"/>
				if (string_equal_no_case(str.c_str(), "url"))
					f.current_item.url.assign(val, val_len);
				else if (string_equal_no_case(str.c_str(), "filesize"))
					f.current_item.size = strtoll(val, 0, 10);
			}
			return;
		}
		case xml_end_tag:
		{
			if (f.in_item && f.is_item(std::string(name, name_len).c_str()))
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
					f.ret.m_title.assign(name, name_len);
				else if (f.is_desc(f.current_tag.c_str()))
					f.ret.m_description.assign(name, name_len);
				else if (f.is_ttl(f.current_tag.c_str()))
				{
					int tmp = atoi(name);
					if (tmp > 0) f.ret.m_ttl = tmp;
				}
				return;
			}
			if (f.is_title(f.current_tag.c_str()))
				f.current_item.title.assign(name, name_len);
			else if (f.is_desc(f.current_tag.c_str()))
				f.current_item.description.assign(name, name_len);
			else if (f.is_uuid(f.current_tag.c_str()))
				f.current_item.uuid.assign(name, name_len);
			else if (f.is_url(f.current_tag.c_str()) && f.type != feed_state::atom)
				f.current_item.url.assign(name, name_len);
			else if (f.is_comment(f.current_tag.c_str()))
				f.current_item.comment.assign(name, name_len);
			else if (f.is_category(f.current_tag.c_str()))
				f.current_item.category.assign(name, name_len);
			else if (f.is_size(f.current_tag.c_str()))
				f.current_item.size = strtoll(name, 0, 10);
			else if (f.is_hash(f.current_tag.c_str()) && name_len == 40)
			{
				if (!from_hex(name, 40, f.current_item.info_hash.data()))
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

	// rss_alert is deprecated, and so is all of this code.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	if (ec && ec != boost::asio::error::eof)
	{
		++m_failures;
		m_error = ec;
		if (m_ses.alerts().should_post<rss_alert>())
		{
			m_ses.alerts().emplace_alert<rss_alert>(my_handle(), m_settings.url
				, rss_alert::state_error, m_error);
		}
		return;
	}

	if (parser.status_code() != 200)
	{
		++m_failures;
		m_error = error_code(parser.status_code(), get_http_category());
		if (m_ses.alerts().should_post<rss_alert>())
		{
			m_ses.alerts().emplace_alert<rss_alert>(my_handle(), m_settings.url
				, rss_alert::state_error, m_error);
		}
		return;
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	m_failures = 0;

	feed_state s(*this);
	xml_parse(data, data + size, boost::bind(&parse_feed, boost::ref(s)
			, _1, _2, _3, _4, _5));

	time_t now = time(NULL);

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

	m_last_update = now;

	// rss_alert is deprecated, and so is all of this code.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	// report that we successfully updated the feed
	if (m_ses.alerts().should_post<rss_alert>())
	{
		m_ses.alerts().emplace_alert<rss_alert>(my_handle(), m_settings.url
			, rss_alert::state_updated, error_code());
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	// update m_ses.m_next_rss_update timestamps
	// now that we have updated our timestamp
	m_ses.update_rss_feeds();
}

void feed::load_state(bdecode_node const& rd)
{
	m_title = rd.dict_find_string_value("m_title");
	m_description = rd.dict_find_string_value("m_description");
	m_last_attempt = rd.dict_find_int_value("m_last_attempt");
	m_last_update = rd.dict_find_int_value("m_last_update");

	bdecode_node e = rd.dict_find_list("items");
	if (e)
	{
		m_items.reserve(e.list_size());
		for (int i = 0; i < e.list_size(); ++i)
		{
			bdecode_node entry = e.list_at(i);
			if (entry.type() != bdecode_node::dict_t) continue;

			m_items.push_back(feed_item());
			feed_item& item = m_items.back();
			item.url = entry.dict_find_string_value("url");
			item.uuid = entry.dict_find_string_value("uuid");
			item.title = entry.dict_find_string_value("title");
			item.description = entry.dict_find_string_value("description");
			item.comment = entry.dict_find_string_value("comment");
			item.category = entry.dict_find_string_value("category");
			item.size = entry.dict_find_int_value("size");

			// don't load duplicates
			if (m_urls.find(item.url) != m_urls.end())
			{
				m_items.pop_back();
				continue;
			}
			m_urls.insert(item.url);
		}
	}

	m_settings.url = rd.dict_find_string_value("url");
	m_settings.auto_download = rd.dict_find_int_value("auto_download");
	m_settings.auto_map_handles = rd.dict_find_int_value("auto_map_handles");
	m_settings.default_ttl = rd.dict_find_int_value("default_ttl");

	e = rd.dict_find_dict("add_params");
	if (e)
	{
		m_settings.add_args.save_path = e.dict_find_string_value("save_path");
		m_settings.add_args.flags = e.dict_find_int_value("flags");
	}

	e = rd.dict_find_list("history");
	if (e)
	{
		for (int i = 0; i < e.list_size(); ++i)
		{
			if (e.list_at(i).type() != bdecode_node::list_t) continue;

			bdecode_node item = e.list_at(i);

			if (item.list_size() != 2
				|| item.list_at(0).type() != bdecode_node::string_t
				|| item.list_at(1).type() != bdecode_node::int_t)
				continue;

			m_added.insert(std::pair<std::string, time_t>(
				item.list_at(0).string_value()
				, item.list_at(1).int_value()));
		}
	}
}

void feed::save_state(entry& rd) const
{
	// feed properties
	rd["m_title"] = m_title;
	rd["m_description"] = m_description;
	rd["m_last_attempt"] = m_last_attempt;
	rd["m_last_update"] = m_last_update;

	// items
	entry::list_type& items = rd["items"].list();
	for (std::vector<feed_item>::const_iterator i = m_items.begin()
		, end(m_items.end()); i != end; ++i)
	{
		items.push_back(entry());
		entry& item = items.back();
		item["url"] = i->url;
		item["uuid"] = i->uuid;
		item["title"] = i->title;
		item["description"] = i->description;
		item["comment"] = i->comment;
		item["category"] = i->category;
		item["size"] = i->size;
	}
	
	// settings
	feed_settings sett_def;
#define TORRENT_WRITE_SETTING(name) \
	if (m_settings.name != sett_def.name) rd[#name] = m_settings.name

	TORRENT_WRITE_SETTING(url);
	TORRENT_WRITE_SETTING(auto_download);
	TORRENT_WRITE_SETTING(auto_map_handles);
	TORRENT_WRITE_SETTING(default_ttl);

#undef TORRENT_WRITE_SETTING

	entry& add = rd["add_params"];
	add_torrent_params add_def;
#define TORRENT_WRITE_SETTING(name) \
	if (m_settings.add_args.name != add_def.name) add[#name] = m_settings.add_args.name;

	TORRENT_WRITE_SETTING(save_path);
	TORRENT_WRITE_SETTING(flags);

#undef TORRENT_WRITE_SETTING

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

	feed_item& i = m_items.back();

	if (m_settings.auto_map_handles)
		i.handle = torrent_handle(m_ses.find_torrent(i.uuid.empty() ? i.url : i.uuid));

	if (m_ses.alerts().should_post<rss_item_alert>())
		m_ses.alerts().emplace_alert<rss_item_alert>(my_handle(), i);

	if (m_settings.auto_download)
	{
		if (!m_settings.auto_map_handles)
			i.handle = torrent_handle(m_ses.find_torrent(i.uuid.empty() ? i.url : i.uuid));

		// if we're already downloading this torrent
		// move along to the next one
		if (i.handle.is_valid()) return;

		// has this already been added?
		if (m_added.find(i.url) != m_added.end()) return;

		// this means we should add this torrent to the session
		add_torrent_params p = m_settings.add_args;
		p.url = i.url;
		p.uuid = i.uuid;
		p.source_feed_url = m_settings.url;
		p.ti.reset();
		p.info_hash.clear();
		p.name = i.title.c_str();

		error_code e;
		m_ses.add_torrent(p, e);
		time_t now = time(NULL);
		m_added.insert(make_pair(i.url, now));
	}
}

// returns the number of seconds until trying again
int feed::update_feed()
{
	if (m_updating) return 60;

	m_last_attempt = time(0);
	m_last_update = 0;

	// rss_alert is deprecated, and so is all of this code.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	if (m_ses.alerts().should_post<rss_alert>())
	{
		m_ses.alerts().emplace_alert<rss_alert>(my_handle(), m_settings.url
			, rss_alert::state_updating, error_code());
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	boost::shared_ptr<http_connection> feed(
		new http_connection(m_ses.get_io_service()
			, m_ses.get_resolver()
			, boost::bind(&feed::on_feed, shared_from_this()
			, _1, _2, _3, _4)));

	m_updating = true;
	feed->get(m_settings.url, seconds(30), 0, 0, 5
		, m_ses.settings().get_str(settings_pack::user_agent));

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
	if (m_last_update == 0) return int(m_last_attempt + 60 * 5 - now);
	int ttl = m_ttl == -1 ? m_settings.default_ttl : m_ttl;
	TORRENT_ASSERT((m_last_update + ttl * 60) - now < INT_MAX);
	return int((m_last_update + ttl * 60) - now);
}

#define TORRENT_ASYNC_CALL(x) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (!f) return; \
	aux::session_impl& ses = f->session(); \
	ses.get_io_service().post(boost::bind(&feed:: x, f))

#define TORRENT_ASYNC_CALL1(x, a1) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (!f) return; \
	aux::session_impl& ses = f->session(); \
	ses.get_io_service().post(boost::bind(&feed:: x, f, a1))

#define TORRENT_SYNC_CALL1(x, a1) \
	boost::shared_ptr<feed> f = m_feed_ptr.lock(); \
	if (f) aux::sync_call_handle(f, boost::bind(&feed:: x, f, a1));

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
	TORRENT_ASYNC_CALL1(set_settings, s);
}

feed_settings feed_handle::settings() const
{
	feed_settings ret;
	TORRENT_SYNC_CALL1(get_settings, &ret);
	return ret;
}

}

#endif // TORRENT_NO_DEPRECATE

