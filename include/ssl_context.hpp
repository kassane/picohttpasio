#pragma once

#ifdef PICO_ENABLE_TLS

#include <asio/ssl.hpp>
#include <string>
#include <string_view>

namespace pico::ssl {

// ---------------------------------------------------------------------------
// Server SSL context — loads certificate chain + private key from PEM files.
//
//   auto ctx = pico::ssl::make_server_context("cert.pem", "key.pem");
//   // Optional: ctx.use_tmp_dh_file("dh4096.pem");
// ---------------------------------------------------------------------------
inline asio::ssl::context make_server_context(
        std::string_view cert_file,
        std::string_view key_file,
        std::string_view dh_params_file = {})
{
    asio::ssl::context ctx(asio::ssl::context::tls_server);
    ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2           |
        asio::ssl::context::no_sslv3           |
        asio::ssl::context::single_dh_use);

    ctx.use_certificate_chain_file(std::string(cert_file));
    ctx.use_private_key_file(std::string(key_file), asio::ssl::context::pem);

    if (!dh_params_file.empty())
        ctx.use_tmp_dh_file(std::string(dh_params_file));

    return ctx;
}

// ---------------------------------------------------------------------------
// Client SSL context — peer verification on by default.
//
//   auto ctx = pico::ssl::make_client_context();            // verify peer
//   auto ctx = pico::ssl::make_client_context(false);       // skip (self-signed)
// ---------------------------------------------------------------------------
inline asio::ssl::context make_client_context(bool verify_peer = true)
{
    asio::ssl::context ctx(asio::ssl::context::tls_client);
    if (verify_peer) {
        ctx.set_verify_mode(asio::ssl::verify_peer);
        ctx.set_default_verify_paths();
    } else {
        ctx.set_verify_mode(asio::ssl::verify_none);
    }
    return ctx;
}

// ---------------------------------------------------------------------------
// Load a certificate and key from in-memory PEM strings (useful for tests).
// ---------------------------------------------------------------------------
inline asio::ssl::context make_server_context_from_memory(
        std::string_view cert_pem,
        std::string_view key_pem)
{
    asio::ssl::context ctx(asio::ssl::context::tls_server);
    ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2           |
        asio::ssl::context::no_sslv3);

    ctx.use_certificate_chain(asio::buffer(cert_pem.data(), cert_pem.size()));
    ctx.use_private_key(
        asio::buffer(key_pem.data(), key_pem.size()),
        asio::ssl::context::pem);
    return ctx;
}

} // namespace pico::ssl

#endif // PICO_ENABLE_TLS
