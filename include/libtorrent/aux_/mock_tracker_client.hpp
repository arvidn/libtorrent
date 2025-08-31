#ifndef TORRENT_MOCK_TRACKER_CLIENT_HPP
#define TORRENT_MOCK_TRACKER_CLIENT_HPP

#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent { namespace aux {

// Mock implementation for testing
class TORRENT_EXTRA_EXPORT mock_tracker_client {
public:
    mock_tracker_client(io_context& ios, settings_pack const& settings)
        : m_ios(ios)
        , m_settings(settings)
        , m_delay_timer(ios)
        , m_delay(milliseconds(0))
        , m_closed(false) {
        m_response.interval = seconds(1800);
        m_response.min_interval = seconds(900);
        m_response.complete = 20;
        m_response.incomplete = 5;
        m_response.downloaded = 100;
    }

    void announce(
        tracker_request const& /*req*/,
        std::function<void(error_code const&, tracker_response const&)> handler
    ) {
        if (m_closed) {
            post(m_ios, [handler]() {
                error_code ec;
                ec.assign(boost::system::errc::operation_canceled, boost::system::generic_category());
                handler(ec, {});
            });
            return;
        }

        if (m_error) {
            if (m_delay.count() > 0) {
                m_delay_timer.expires_after(m_delay);
                m_delay_timer.async_wait(
                    [this, handler](error_code const& ec) {
                        if (ec) return;
                        handler(m_error, {});
                    });
            } else {
                post(m_ios, [this, handler]() {
                    handler(m_error, {});
                });
            }
            return;
        }

        if (m_delay.count() > 0) {
            m_delay_timer.expires_after(m_delay);
            m_delay_timer.async_wait(
                [this, handler](error_code const& ec) {
                    if (ec) return;
                    handler({}, m_response);
                });
        } else {
            post(m_ios, [this, handler]() {
                handler({}, m_response);
            });
        }
    }

    void scrape(
        tracker_request const& req,
        std::function<void(error_code const&, tracker_response const&)> handler
    ) {
        // Reuse announce logic for scrape
        announce(req, handler);
    }

    bool can_reuse() const {
        return !m_closed;
    }

    void close() {
        m_closed = true;
        m_delay_timer.cancel();
    }

    void set_mock_response(tracker_response const& resp) {
        m_response = resp;
    }

    void set_mock_error(error_code const& ec) {
        m_error = ec;
    }

    void set_mock_delay(time_duration delay) {
        m_delay = delay;
    }

private:
    io_context& m_ios;
    settings_pack m_settings;
    deadline_timer m_delay_timer;
    tracker_response m_response;
    error_code m_error;
    time_duration m_delay;
    bool m_closed;
};

}} // namespace libtorrent::aux

#endif // TORRENT_MOCK_TRACKER_CLIENT_HPP