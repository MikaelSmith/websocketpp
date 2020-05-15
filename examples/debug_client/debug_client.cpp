// Compile with
// c++ debug_client.cpp -lboost_system -lboost_chrono -lpthread -I /path/to/websocketpp

/*
 * Copyright (c) 2014, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** ====== WARNING ========
 * This example is presently used as a scratch space. It may or may not be broken
 * at any given time.
 */

#include <websocketpp/config/debug_asio.hpp>

#include <websocketpp/client.hpp>

#include <iostream>
#include <chrono>

typedef websocketpp::client<websocketpp::config::debug_asio_tls> client;
typedef websocketpp::config::debug_asio_tls::message_type::ptr message_ptr;
typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> context_ptr;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class debug_client {
public:
    typedef debug_client type;
    typedef std::chrono::duration<int,std::micro> dur_type;

    debug_client () {
        m_endpoint.set_access_channels(websocketpp::log::alevel::all);
        m_endpoint.set_error_channels(websocketpp::log::elevel::all);

        // Initialize ASIO
        m_endpoint.init_asio();

        // Register our handlers
        m_endpoint.set_tls_init_handler(bind(&type::on_tls_init,this,::_1));
        m_endpoint.set_message_handler(bind(&type::on_message,this,::_1,::_2));
        m_endpoint.set_open_handler(bind(&type::on_open,this,::_1));
        m_endpoint.set_close_handler(bind(&type::on_close,this,::_1));
        m_endpoint.set_fail_handler(bind(&type::on_fail,this,::_1));
    }

    void start(std::string uri) {
        websocketpp::lib::error_code ec;
        auto con = m_endpoint.get_connection(uri, ec);

        if (ec) {
            m_endpoint.get_alog().write(websocketpp::log::alevel::app,ec.message());
            return;
        }

        m_endpoint.connect(con);

        // Start the ASIO io_service run loop
        m_endpoint.run();
    }

    context_ptr on_tls_init(websocketpp::connection_hdl) {
        context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv1);

        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                             boost::asio::ssl::context::no_sslv2 |
                             boost::asio::ssl::context::no_sslv3 |
                             boost::asio::ssl::context::single_dh_use);
        } catch (std::exception& e) {
            std::cout << e.what() << std::endl;
        }
        return ctx;
    }

    void on_fail(websocketpp::connection_hdl hdl) {
        m_open = false;
        auto con = m_endpoint.get_con_from_hdl(hdl);
        
        std::cout << "Fail handler" << std::endl;
        std::cout << con->get_state() << std::endl;
        std::cout << con->get_local_close_code() << std::endl;
        std::cout << con->get_local_close_reason() << std::endl;
        std::cout << con->get_remote_close_code() << std::endl;
        std::cout << con->get_remote_close_reason() << std::endl;
        std::cout << con->get_ec() << " - " << con->get_ec().message() << std::endl;
    }

    void on_open(websocketpp::connection_hdl hdl) {
        m_open = true;
        // Start thread sending hello every few seconds
        m_thread = websocketpp::lib::thread { &debug_client::spew, this, hdl };
    }

    void on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
        // Cancel current timer when we receive a response.
        m_timeout->cancel();
        std::cout << msg->get_payload() << std::endl;
    }

    void on_close(websocketpp::connection_hdl hdl) {
        auto con = m_endpoint.get_con_from_hdl(hdl);
        auto close_code = con->get_remote_close_code();
        std::cout << "Closed: " << close_code << std::endl;
        m_open = false;
    }

    void spew(websocketpp::connection_hdl hdl) {
        while (m_open) {
            try {
                m_endpoint.send(hdl, "hello", websocketpp::frame::opcode::text);
                start_timer(hdl);
            } catch (websocketpp::exception &e) {
                std::cout << "Send error: " << e.what() << std::endl;
                break;
            }

            // Make sure this is longer than the timer timeout (currently 1000ms) so we
            // don't overwrite the previous timer before it expires.
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    void start_timer(websocketpp::connection_hdl hdl) {
        // Set a timer to close the connection if we don't get a response.
        auto con = m_endpoint.get_con_from_hdl(hdl);
        m_timeout = con->set_timer(1000, [this, hdl](websocketpp::lib::error_code ec) {
            if (ec != websocketpp::transport::error::operation_aborted) {
                std::cout << "Timer expired with " << ec << std::endl;
                m_endpoint.close(hdl, websocketpp::close::status::normal, "");
                m_thread.join();
            }
        });
    }
private:
    client m_endpoint;
    client::connection_type::timer_ptr m_timeout;
    websocketpp::lib::thread m_thread;
    bool m_open;
};

int main(int argc, char* argv[]) {
    // Matches echo_server_tls
    std::string uri = "wss://localhost:9002";

    if (argc == 2) {
        uri = argv[1];
    }

    try {
        debug_client endpoint;
        endpoint.start(uri);

        std::cout << "Press enter to exit...";
        std::cin.get();
    } catch (websocketpp::exception const & e) {
        std::cout << e.what() << std::endl;
    } catch (std::exception const & e) {
        std::cout << e.what() << std::endl;
    } catch (...) {
        std::cout << "other exception" << std::endl;
    }
}
