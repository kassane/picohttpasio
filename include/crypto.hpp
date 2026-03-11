#pragma once

// ---------------------------------------------------------------------------
// pico::crypto — libsodium wrapper
//
// Provides:
//   - Secure random bytes
//   - SHA-256 / SHA-512 hashing
//   - Ed25519 digital signatures
//   - XSalsa20-Poly1305 AEAD (secretbox) symmetric encryption
//   - X25519/XSalsa20 public-key AEAD (box)
//   - BLAKE2b hashing (fast, cryptographically secure)
//
// Requires: libsodium  (PICO_ENABLE_CRYPTO must be defined via CMake)
//
// Usage:
//   pico::crypto::init();   // call once at program start (thread-safe)
//
//   auto hash = pico::crypto::sha256("hello world");
//   auto key  = pico::crypto::secretbox::generate_key();
//   auto enc  = pico::crypto::secretbox::encrypt("secret", key);
//   auto dec  = pico::crypto::secretbox::decrypt(enc, key);
// ---------------------------------------------------------------------------

#ifdef PICO_ENABLE_CRYPTO

#include <sodium.h>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pico::crypto {

// ---------------------------------------------------------------------------
// Initialisation — must be called before any other crypto function.
// Idempotent and thread-safe.
// ---------------------------------------------------------------------------
inline void init() {
    if (sodium_init() < 0)
        throw std::runtime_error("libsodium initialisation failed");
}

// ---------------------------------------------------------------------------
// Secure random bytes
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> random_bytes(size_t n) {
    std::vector<uint8_t> out(n);
    randombytes_buf(out.data(), n);
    return out;
}

inline std::string random_string(size_t n) {
    std::string out(n, '\0');
    randombytes_buf(out.data(), n);
    return out;
}

// ---------------------------------------------------------------------------
// SHA-256 / SHA-512 hashing
// (crypto_hash_sha256 / crypto_hash_sha512 from libsodium)
// ---------------------------------------------------------------------------
using SHA256Digest = std::array<uint8_t, crypto_hash_sha256_BYTES>;  // 32 bytes
using SHA512Digest = std::array<uint8_t, crypto_hash_sha512_BYTES>;  // 64 bytes

inline SHA256Digest sha256(std::string_view data) {
    SHA256Digest out;
    crypto_hash_sha256(out.data(),
                       reinterpret_cast<const uint8_t*>(data.data()),
                       data.size());
    return out;
}

inline SHA512Digest sha512(std::string_view data) {
    SHA512Digest out;
    crypto_hash_sha512(out.data(),
                       reinterpret_cast<const uint8_t*>(data.data()),
                       data.size());
    return out;
}

// BLAKE2b — often faster than SHA-2 on modern CPUs, also cryptographically secure
using Blake2bDigest = std::array<uint8_t, crypto_generichash_BYTES>;  // 32 bytes default

inline Blake2bDigest blake2b(std::string_view data,
                              std::string_view key = {}) {
    Blake2bDigest out;
    crypto_generichash(
        out.data(), out.size(),
        reinterpret_cast<const uint8_t*>(data.data()), data.size(),
        key.empty() ? nullptr : reinterpret_cast<const uint8_t*>(key.data()),
        key.size());
    return out;
}

// Hex-encode a digest (utility)
inline std::string to_hex(const uint8_t* data, size_t len) {
    std::string out(len * 2 + 1, '\0');
    sodium_bin2hex(out.data(), out.size(), data, len);
    out.pop_back();  // remove null terminator
    return out;
}

template<size_t N>
inline std::string to_hex(const std::array<uint8_t, N>& digest) {
    return to_hex(digest.data(), N);
}

// ---------------------------------------------------------------------------
// Ed25519 digital signatures
// ---------------------------------------------------------------------------
namespace sign {

constexpr size_t PUBLIC_KEY_BYTES  = crypto_sign_PUBLICKEYBYTES;   // 32
constexpr size_t SECRET_KEY_BYTES  = crypto_sign_SECRETKEYBYTES;   // 64
constexpr size_t SIGNATURE_BYTES   = crypto_sign_BYTES;            // 64

using PublicKey = std::array<uint8_t, crypto_sign_PUBLICKEYBYTES>;
using SecretKey = std::array<uint8_t, crypto_sign_SECRETKEYBYTES>;
using Signature = std::array<uint8_t, crypto_sign_BYTES>;

struct KeyPair {
    PublicKey pk;
    SecretKey sk;
};

inline KeyPair generate_keypair() {
    KeyPair kp;
    crypto_sign_keypair(kp.pk.data(), kp.sk.data());
    return kp;
}

// Attach a signature prefix to message (combined format)
inline std::string sign_combined(std::string_view message, const SecretKey& sk) {
    std::string out(message.size() + SIGNATURE_BYTES, '\0');
    unsigned long long len = 0;
    crypto_sign(reinterpret_cast<uint8_t*>(out.data()), &len,
                reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                sk.data());
    out.resize(static_cast<size_t>(len));
    return out;
}

// Verify and extract message from combined signed message
inline std::optional<std::string> verify_combined(std::string_view signed_msg,
                                                    const PublicKey& pk) {
    if (signed_msg.size() < SIGNATURE_BYTES) return std::nullopt;
    std::string out(signed_msg.size() - SIGNATURE_BYTES, '\0');
    unsigned long long len = 0;
    if (crypto_sign_open(
            reinterpret_cast<uint8_t*>(out.data()), &len,
            reinterpret_cast<const uint8_t*>(signed_msg.data()), signed_msg.size(),
            pk.data()) != 0)
        return std::nullopt;
    out.resize(static_cast<size_t>(len));
    return out;
}

// Detached signature
inline Signature sign_detached(std::string_view message, const SecretKey& sk) {
    Signature sig;
    crypto_sign_detached(sig.data(), nullptr,
                         reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                         sk.data());
    return sig;
}

inline bool verify_detached(std::string_view message,
                             const Signature& sig,
                             const PublicKey& pk) {
    return crypto_sign_verify_detached(
        sig.data(),
        reinterpret_cast<const uint8_t*>(message.data()), message.size(),
        pk.data()) == 0;
}

} // namespace sign

// ---------------------------------------------------------------------------
// XSalsa20-Poly1305 symmetric AEAD (secretbox)
// Key + random nonce; generates and prepends nonce to ciphertext for convenience.
// ---------------------------------------------------------------------------
namespace secretbox {

constexpr size_t KEY_BYTES   = crypto_secretbox_KEYBYTES;    // 32
constexpr size_t NONCE_BYTES = crypto_secretbox_NONCEBYTES;  // 24
constexpr size_t MAC_BYTES   = crypto_secretbox_MACBYTES;    // 16

using Key   = std::array<uint8_t, crypto_secretbox_KEYBYTES>;
using Nonce = std::array<uint8_t, crypto_secretbox_NONCEBYTES>;

inline Key generate_key() {
    Key k;
    crypto_secretbox_keygen(k.data());
    return k;
}

inline Nonce generate_nonce() {
    Nonce n;
    randombytes_buf(n.data(), n.size());
    return n;
}

// Returns nonce (24 bytes) prepended to ciphertext+MAC
inline std::string encrypt(std::string_view plaintext, const Key& key) {
    auto nonce = generate_nonce();
    std::string out(NONCE_BYTES + MAC_BYTES + plaintext.size(), '\0');

    std::copy(nonce.begin(), nonce.end(), reinterpret_cast<uint8_t*>(out.data()));
    crypto_secretbox_easy(
        reinterpret_cast<uint8_t*>(out.data()) + NONCE_BYTES,
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
        nonce.data(), key.data());
    return out;
}

// Decrypts output of encrypt(); returns nullopt on auth failure or short input
inline std::optional<std::string> decrypt(std::string_view ciphertext, const Key& key) {
    if (ciphertext.size() < NONCE_BYTES + MAC_BYTES) return std::nullopt;

    Nonce nonce;
    std::copy_n(reinterpret_cast<const uint8_t*>(ciphertext.data()),
                NONCE_BYTES, nonce.begin());

    std::string out(ciphertext.size() - NONCE_BYTES - MAC_BYTES, '\0');
    if (crypto_secretbox_open_easy(
            reinterpret_cast<uint8_t*>(out.data()),
            reinterpret_cast<const uint8_t*>(ciphertext.data()) + NONCE_BYTES,
            ciphertext.size() - NONCE_BYTES,
            nonce.data(), key.data()) != 0)
        return std::nullopt;
    return out;
}

} // namespace secretbox

// ---------------------------------------------------------------------------
// X25519 + XSalsa20-Poly1305 public-key AEAD (box)
// Asymmetric authenticated encryption between two parties.
// ---------------------------------------------------------------------------
namespace box {

constexpr size_t PUBLIC_KEY_BYTES = crypto_box_PUBLICKEYBYTES;   // 32
constexpr size_t SECRET_KEY_BYTES = crypto_box_SECRETKEYBYTES;   // 32
constexpr size_t NONCE_BYTES      = crypto_box_NONCEBYTES;       // 24
constexpr size_t MAC_BYTES        = crypto_box_MACBYTES;         // 16

using PublicKey = std::array<uint8_t, crypto_box_PUBLICKEYBYTES>;
using SecretKey = std::array<uint8_t, crypto_box_SECRETKEYBYTES>;
using Nonce     = std::array<uint8_t, crypto_box_NONCEBYTES>;

struct KeyPair {
    PublicKey pk;
    SecretKey sk;
};

inline KeyPair generate_keypair() {
    KeyPair kp;
    crypto_box_keypair(kp.pk.data(), kp.sk.data());
    return kp;
}

// Encrypt for recipient_pk using sender_sk; nonce prepended to output
inline std::string encrypt(std::string_view plaintext,
                            const PublicKey& recipient_pk,
                            const SecretKey& sender_sk) {
    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    std::string out(NONCE_BYTES + MAC_BYTES + plaintext.size(), '\0');
    std::copy(nonce.begin(), nonce.end(), reinterpret_cast<uint8_t*>(out.data()));

    crypto_box_easy(
        reinterpret_cast<uint8_t*>(out.data()) + NONCE_BYTES,
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
        nonce.data(), recipient_pk.data(), sender_sk.data());
    return out;
}

inline std::optional<std::string> decrypt(std::string_view ciphertext,
                                           const PublicKey& sender_pk,
                                           const SecretKey& recipient_sk) {
    if (ciphertext.size() < NONCE_BYTES + MAC_BYTES) return std::nullopt;

    Nonce nonce;
    std::copy_n(reinterpret_cast<const uint8_t*>(ciphertext.data()),
                NONCE_BYTES, nonce.begin());

    std::string out(ciphertext.size() - NONCE_BYTES - MAC_BYTES, '\0');
    if (crypto_box_open_easy(
            reinterpret_cast<uint8_t*>(out.data()),
            reinterpret_cast<const uint8_t*>(ciphertext.data()) + NONCE_BYTES,
            ciphertext.size() - NONCE_BYTES,
            nonce.data(), sender_pk.data(), recipient_sk.data()) != 0)
        return std::nullopt;
    return out;
}

} // namespace box

} // namespace pico::crypto

#endif // PICO_ENABLE_CRYPTO
