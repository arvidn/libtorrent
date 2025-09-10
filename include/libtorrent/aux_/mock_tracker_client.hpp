#ifndef TORRENT_MOCK_TRACKER_CLIENT_HPP
#define TORRENT_MOCK_TRACKER_CLIENT_HPP

#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent::aux {

// Mock implementation for testing
class TORRENT_EXTRA_EXPORT mock_tracker_client {
public:
    mock_tracker_client(io_context& ios, settings_pack const& settings)
        : m_ios(ios)
        , m_settings(settings)
        , m_delay_timer(ios)
        , m_response(create_default_response())
    {}

    void announce(
        tracker_request const& /*req*/,
        std::function<void(error_code const&, tracker_response const&)> handler
    ) {
        if (m_closed) {
            post(m_ios, [handler = std::move(handler)]() mutable {
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
                    [this, handler = std::move(handler)](error_code const& ec) mutable {
                        if (ec) return;
                        handler(m_error, {});
                    });
            } else {
                post(m_ios, [this, handler = std::move(handler)]() mutable {
                    handler(m_error, {});
                });
            }
            return;
        }

        if (m_delay.count() > 0) {
            m_delay_timer.expires_after(m_delay);
            m_delay_timer.async_wait(
                [this, handler = std::move(handler)](error_code const& ec) mutable {
                    if (ec) return;
                    handler({}, m_response);
                });
        } else {
            post(m_ios, [this, handler = std::move(handler)]() mutable {
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

    [[nodiscard]] bool can_reuse() const {
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
    static tracker_response create_default_response() {
        tracker_response resp{};
        resp.interval = seconds(1800);
        resp.min_interval = seconds(900);
        resp.complete = 20;
        resp.incomplete = 5;
        resp.downloaded = 100;
        return resp;
    }

    io_context& m_ios;
    settings_pack m_settings;
    deadline_timer m_delay_timer;
    tracker_response m_response;
    error_code m_error{};
    time_duration m_delay{milliseconds(0)};
    bool m_closed{false};
};

} // namespace libtorrent::aux

#endif // TORRENT_MOCK_TRACKER_CLIENT_HPP