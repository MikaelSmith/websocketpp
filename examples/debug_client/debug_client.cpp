#include <websocketpp/config/debug_asio.hpp>
#include <websocketpp/client.hpp>

#include <iostream>
#include <chrono>

typedef websocketpp::lib::asio::ssl::context context;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class debug_client {
public:
    typedef debug_client type;

    debug_client () {
        m_endpoint.set_access_channels(websocketpp::log::alevel::all);
        m_endpoint.set_error_channels(websocketpp::log::elevel::all);

        m_endpoint.init_asio();

        m_endpoint.set_tls_init_handler(bind(&type::on_tls_init,this,::_1));
        m_endpoint.set_pong_timeout(1000);
        m_endpoint.set_pong_timeout_handler(bind(&type::on_pong_timeout,this,::_1,::_2));
        m_endpoint.set_open_handler(bind(&type::on_open,this,::_1));
    }

    void start(std::string uri) {
        websocketpp::lib::error_code ec;
        auto con = m_endpoint.get_connection(uri, ec);
        if (ec) throw ec;

        m_endpoint.connect(con);
        m_endpoint.run();
    }

    websocketpp::lib::shared_ptr<context> on_tls_init(websocketpp::connection_hdl) {
        auto ctx = websocketpp::lib::make_shared<context>(context::tlsv12_client);
        ctx->set_options(context::default_workarounds | context::no_sslv2 |
                         context::no_sslv3 | context::single_dh_use);
        return ctx;
    }

    void on_open(websocketpp::connection_hdl hdl) {
        // Start thread sending ping every few seconds
        m_thread = websocketpp::lib::thread { &debug_client::ping, this, hdl };
    }

    void on_pong_timeout(websocketpp::connection_hdl hdl, std::string payload) {
        if (m_timeouts < 2) {
            m_timeouts++;
            return;
        }

        std::cout << "PONG TIMEOUT!" << std::endl;
        m_endpoint.close(hdl, websocketpp::close::status::normal, "consecutive onPongTimeouts");
    }

    void ping(websocketpp::connection_hdl hdl) {
        auto con = m_endpoint.get_con_from_hdl(hdl);
        while (con->get_state() == websocketpp::session::state::open) {
            std::cout << "Sending heartbeat" << std::endl;
            websocketpp::lib::error_code ec;
            m_endpoint.ping(hdl, "", ec);
            if (ec) {
                std::cout << "Ping error: " << ec << std::endl;
                break;
            }

            // Make sure this is longer than the pong timeout (currently 1000ms) so we
            // don't send another too quickly.
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }

    void wait() {
        m_thread.join();
    }
private:
    websocketpp::client<websocketpp::config::debug_asio_tls> m_endpoint;
    websocketpp::lib::thread m_thread;
    uint8_t m_timeouts = 0;
};

int main(int argc, char* argv[]) {
    // Matches echo_server_tls
    std::string uri = "wss://localhost:9002";

    if (argc == 2) {
        uri = argv[1];
    }

    debug_client endpoint;
    endpoint.start(uri);
    endpoint.wait();
}
