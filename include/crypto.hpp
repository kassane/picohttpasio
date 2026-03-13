#pragma once

// ---------------------------------------------------------------------------
// pico::crypto — OpenSSL EVP crypto primitives
//
// Unified under PICO_ENABLE_TLS (no separate libsodium dependency).
// OpenSSL already provides everything libsodium offers; having both would
// duplicate the dependency without gain.
//
// Provides:
//   - Secure random bytes (RAND_bytes)
//   - SHA-256 / SHA-512 hashing (EVP_sha256 / EVP_sha512)
//   - BLAKE2b-512 hashing (EVP_blake2b512)
//   - Ed25519 digital signatures (EVP_PKEY_ED25519)
//   - AES-256-GCM AEAD symmetric encryption (replaces XSalsa20-Poly1305)
//   - X25519 + AES-256-GCM public-key AEAD (replaces X25519+XSalsa20)
//
// Requires: OpenSSL 1.1.1+ (PICO_ENABLE_TLS must be defined via CMake)
//
// Usage:
//   auto hash = pico::crypto::sha256("hello world");
//   auto key  = pico::crypto::secretbox::generate_key();
//   auto enc  = pico::crypto::secretbox::encrypt("secret", key);
//   auto dec  = pico::crypto::secretbox::decrypt(enc, key);
// ---------------------------------------------------------------------------

#ifdef PICO_ENABLE_TLS

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pico::crypto {

// ---------------------------------------------------------------------------
// Hex encoding
// ---------------------------------------------------------------------------
inline std::string to_hex(const uint8_t* data, size_t len) {
    static constexpr char h[] = "0123456789abcdef";
    std::string out(len * 2, '\0');
    for (size_t i = 0; i < len; ++i) {
        out[i * 2]     = h[data[i] >> 4];
        out[i * 2 + 1] = h[data[i] & 0x0F];
    }
    return out;
}

template<size_t N>
inline std::string to_hex(const std::array<uint8_t, N>& digest) {
    return to_hex(digest.data(), N);
}

// ---------------------------------------------------------------------------
// Secure random bytes
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> random_bytes(size_t n) {
    std::vector<uint8_t> out(n);
    if (RAND_bytes(out.data(), static_cast<int>(n)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return out;
}

inline std::string random_string(size_t n) {
    std::string out(n, '\0');
    if (RAND_bytes(reinterpret_cast<uint8_t*>(out.data()), static_cast<int>(n)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return out;
}

// ---------------------------------------------------------------------------
// Internal: generic one-shot EVP digest
// ---------------------------------------------------------------------------
namespace detail {

inline std::vector<uint8_t> evp_digest(const EVP_MD* md, const void* data, size_t len) {
    auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new");
    unsigned int out_len = static_cast<unsigned int>(EVP_MD_size(md));
    std::vector<uint8_t> out(out_len);
    if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) ||
        !EVP_DigestUpdate(ctx.get(), data, len)    ||
        !EVP_DigestFinal_ex(ctx.get(), out.data(), &out_len))
        throw std::runtime_error("EVP_Digest failed");
    return out;
}

inline std::vector<uint8_t> evp_digest(const EVP_MD* md, std::string_view sv) {
    return evp_digest(md, sv.data(), sv.size());
}

} // namespace detail

// ---------------------------------------------------------------------------
// SHA-256 / SHA-512 / BLAKE2b
// ---------------------------------------------------------------------------
using SHA256Digest  = std::array<uint8_t, 32>;
using SHA512Digest  = std::array<uint8_t, 64>;
using Blake2bDigest = std::array<uint8_t, 64>;  // BLAKE2b-512

inline SHA256Digest sha256(std::string_view data) {
    auto v = detail::evp_digest(EVP_sha256(), data);
    SHA256Digest out; std::copy(v.begin(), v.end(), out.begin()); return out;
}

inline SHA512Digest sha512(std::string_view data) {
    auto v = detail::evp_digest(EVP_sha512(), data);
    SHA512Digest out; std::copy(v.begin(), v.end(), out.begin()); return out;
}

// Keyless: BLAKE2b-512.  Keyed: HMAC-SHA-512 (OpenSSL EVP_Digest does not
// expose the keyed BLAKE2b interface; HMAC-SHA-512 provides the same
// security properties for a MAC).
inline Blake2bDigest blake2b(std::string_view data, std::string_view key = {}) {
    Blake2bDigest out;
    if (key.empty()) {
        auto v = detail::evp_digest(EVP_blake2b512(), data);
        std::copy(v.begin(), v.end(), out.begin());
    } else {
        unsigned int len = 64;
        HMAC(EVP_sha512(),
             key.data(),  static_cast<int>(key.size()),
             reinterpret_cast<const uint8_t*>(data.data()), data.size(),
             out.data(), &len);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Ed25519 digital signatures
// ---------------------------------------------------------------------------
namespace sign {

constexpr size_t PUBLIC_KEY_BYTES = 32;
constexpr size_t SECRET_KEY_BYTES = 32;  // raw private key (OpenSSL convention)
constexpr size_t SIGNATURE_BYTES  = 64;

using PublicKey = std::array<uint8_t, PUBLIC_KEY_BYTES>;
using SecretKey = std::array<uint8_t, SECRET_KEY_BYTES>;
using Signature = std::array<uint8_t, SIGNATURE_BYTES>;

struct KeyPair { PublicKey pk; SecretKey sk; };

inline KeyPair generate_keypair() {
    auto ctx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>(
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY_keygen_init(ctx.get());
    EVP_PKEY* raw = nullptr;
    EVP_PKEY_keygen(ctx.get(), &raw);
    auto pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(raw, EVP_PKEY_free);

    KeyPair kp;
    size_t pk_len = PUBLIC_KEY_BYTES, sk_len = SECRET_KEY_BYTES;
    EVP_PKEY_get_raw_public_key(pkey.get(),  kp.pk.data(), &pk_len);
    EVP_PKEY_get_raw_private_key(pkey.get(), kp.sk.data(), &sk_len);
    return kp;
}

namespace detail {
inline std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>
sk_to_pkey(const SecretKey& sk) {
    return { EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, sk.data(), sk.size()),
             EVP_PKEY_free };
}
inline std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>
pk_to_pkey(const PublicKey& pk) {
    return { EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pk.data(), pk.size()),
             EVP_PKEY_free };
}
} // namespace detail

// Ed25519 does not support the Init/Update/Final streaming model in OpenSSL 3.
// Use the one-shot EVP_DigestSign / EVP_DigestVerify instead.

// Combined format: signature (64 bytes) prepended to message
inline std::string sign_combined(std::string_view message, const SecretKey& sk) {
    auto pkey = detail::sk_to_pkey(sk);
    auto ctx  = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get());
    size_t sig_len = SIGNATURE_BYTES;
    Signature sig;
    EVP_DigestSign(ctx.get(), sig.data(), &sig_len,
                   reinterpret_cast<const uint8_t*>(message.data()), message.size());
    std::string out(sig.begin(), sig.end());
    out.append(message);
    return out;
}

inline std::optional<std::string> verify_combined(std::string_view signed_msg,
                                                    const PublicKey& pk) {
    if (signed_msg.size() < SIGNATURE_BYTES) return std::nullopt;
    auto pkey = detail::pk_to_pkey(pk);
    auto ctx  = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get());
    std::string_view msg = signed_msg.substr(SIGNATURE_BYTES);
    if (EVP_DigestVerify(ctx.get(),
            reinterpret_cast<const uint8_t*>(signed_msg.data()), SIGNATURE_BYTES,
            reinterpret_cast<const uint8_t*>(msg.data()), msg.size()) != 1)
        return std::nullopt;
    return std::string(msg);
}

inline Signature sign_detached(std::string_view message, const SecretKey& sk) {
    auto pkey = detail::sk_to_pkey(sk);
    auto ctx  = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get());
    size_t sig_len = SIGNATURE_BYTES;
    Signature sig;
    EVP_DigestSign(ctx.get(), sig.data(), &sig_len,
                   reinterpret_cast<const uint8_t*>(message.data()), message.size());
    return sig;
}

inline bool verify_detached(std::string_view message,
                             const Signature& sig,
                             const PublicKey& pk) {
    auto pkey = detail::pk_to_pkey(pk);
    auto ctx  = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get());
    return EVP_DigestVerify(ctx.get(), sig.data(), sig.size(),
                            reinterpret_cast<const uint8_t*>(message.data()),
                            message.size()) == 1;
}

} // namespace sign

// ---------------------------------------------------------------------------
// AES-256-GCM symmetric AEAD (replaces XSalsa20-Poly1305 secretbox)
//
// Wire format: IV(12) || Tag(16) || Ciphertext
// ---------------------------------------------------------------------------
namespace secretbox {

constexpr size_t KEY_BYTES   = 32;  // AES-256
constexpr size_t NONCE_BYTES = 12;  // GCM 96-bit IV
constexpr size_t MAC_BYTES   = 16;  // GCM 128-bit tag

using Key   = std::array<uint8_t, KEY_BYTES>;
using Nonce = std::array<uint8_t, NONCE_BYTES>;

inline Key generate_key() {
    Key k;
    if (RAND_bytes(k.data(), static_cast<int>(k.size())) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return k;
}

inline Nonce generate_nonce() {
    Nonce n;
    if (RAND_bytes(n.data(), static_cast<int>(n.size())) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return n;
}

inline std::string encrypt(std::string_view plaintext, const Key& key) {
    auto iv = generate_nonce();
    std::string out(NONCE_BYTES + MAC_BYTES + plaintext.size(), '\0');

    auto* iv_out  = reinterpret_cast<uint8_t*>(out.data());
    auto* tag_out = iv_out  + NONCE_BYTES;
    auto* ct_out  = tag_out + MAC_BYTES;

    std::copy(iv.begin(), iv.end(), iv_out);

    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);

    EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_BYTES, nullptr);
    EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data());

    int n = 0;
    EVP_EncryptUpdate(ctx.get(), ct_out, &n,
                      reinterpret_cast<const uint8_t*>(plaintext.data()),
                      static_cast<int>(plaintext.size()));
    int fin = 0;
    EVP_EncryptFinal_ex(ctx.get(), ct_out + n, &fin);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, MAC_BYTES, tag_out);
    return out;
}

inline std::optional<std::string> decrypt(std::string_view ciphertext, const Key& key) {
    if (ciphertext.size() < NONCE_BYTES + MAC_BYTES) return std::nullopt;

    const auto* data = reinterpret_cast<const uint8_t*>(ciphertext.data());
    const auto* iv   = data;
    const auto* tag  = data + NONCE_BYTES;
    const auto* ct   = data + NONCE_BYTES + MAC_BYTES;
    int ct_len = static_cast<int>(ciphertext.size() - NONCE_BYTES - MAC_BYTES);

    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);

    EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_BYTES, nullptr);
    EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv);

    std::string out(ct_len, '\0');
    int n = 0;
    EVP_DecryptUpdate(ctx.get(), reinterpret_cast<uint8_t*>(out.data()), &n, ct, ct_len);

    // Set expected tag before calling Final (GCM authentication)
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, MAC_BYTES,
                        const_cast<uint8_t*>(tag));
    int fin = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<uint8_t*>(out.data()) + n, &fin) != 1)
        return std::nullopt;  // authentication failure
    return out;
}

} // namespace secretbox

// ---------------------------------------------------------------------------
// X25519 ECDH + AES-256-GCM public-key AEAD (replaces box)
//
// Shared key derivation: SHA-256(X25519(sender_sk, recipient_pk))
// Ciphertext format: same as secretbox (IV || Tag || CT)
// ---------------------------------------------------------------------------
namespace box {

constexpr size_t PUBLIC_KEY_BYTES = 32;
constexpr size_t SECRET_KEY_BYTES = 32;

using PublicKey = std::array<uint8_t, PUBLIC_KEY_BYTES>;
using SecretKey = std::array<uint8_t, SECRET_KEY_BYTES>;

struct KeyPair { PublicKey pk; SecretKey sk; };

inline KeyPair generate_keypair() {
    auto ctx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>(
        EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY_keygen_init(ctx.get());
    EVP_PKEY* raw = nullptr;
    EVP_PKEY_keygen(ctx.get(), &raw);
    auto pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(raw, EVP_PKEY_free);

    KeyPair kp;
    size_t pk_len = PUBLIC_KEY_BYTES, sk_len = SECRET_KEY_BYTES;
    EVP_PKEY_get_raw_public_key(pkey.get(),  kp.pk.data(), &pk_len);
    EVP_PKEY_get_raw_private_key(pkey.get(), kp.sk.data(), &sk_len);
    return kp;
}

namespace detail {
inline secretbox::Key derive_key(const PublicKey& recipient_pk,
                                  const SecretKey& sender_sk) {
    auto sk_pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
        EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                     sender_sk.data(), sender_sk.size()),
        EVP_PKEY_free);
    auto pk_pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                    recipient_pk.data(), recipient_pk.size()),
        EVP_PKEY_free);

    auto dh_ctx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>(
        EVP_PKEY_CTX_new(sk_pkey.get(), nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY_derive_init(dh_ctx.get());
    EVP_PKEY_derive_set_peer(dh_ctx.get(), pk_pkey.get());

    size_t shared_len = 0;
    EVP_PKEY_derive(dh_ctx.get(), nullptr, &shared_len);
    std::vector<uint8_t> shared(shared_len);
    EVP_PKEY_derive(dh_ctx.get(), shared.data(), &shared_len);

    // KDF: SHA-256(shared_secret) → 32-byte AES-256-GCM key
    auto kdf = pico::crypto::detail::evp_digest(EVP_sha256(),
                   reinterpret_cast<const char*>(shared.data()), shared_len);
    secretbox::Key aes_key;
    std::copy(kdf.begin(), kdf.end(), aes_key.begin());
    return aes_key;
}
} // namespace detail

inline std::string encrypt(std::string_view plaintext,
                            const PublicKey& recipient_pk,
                            const SecretKey& sender_sk) {
    return secretbox::encrypt(plaintext, detail::derive_key(recipient_pk, sender_sk));
}

inline std::optional<std::string> decrypt(std::string_view ciphertext,
                                           const PublicKey& sender_pk,
                                           const SecretKey& recipient_sk) {
    return secretbox::decrypt(ciphertext, detail::derive_key(sender_pk, recipient_sk));
}

} // namespace box

} // namespace pico::crypto
#endif // PICO_ENABLE_TLS
