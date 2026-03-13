#include <catch2/catch_test_macros.hpp>

#ifdef PICO_ENABLE_TLS
#include "crypto.hpp"
#include <cstring>
#include <set>

using namespace pico::crypto;

// ---------------------------------------------------------------------------
// SHA-256 / SHA-512 known vectors (NIST FIPS 180-4)
// ---------------------------------------------------------------------------

TEST_CASE("sha256: NIST vector for empty string", "[crypto][hash]") {
    auto h = sha256("");
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    REQUIRE(to_hex(h) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("sha256: NIST vector for 'abc'", "[crypto][hash]") {
    auto h = sha256("abc");
    REQUIRE(to_hex(h) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // full expected: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    // Note: crypto_hash_sha256_BYTES = 32, hex = 64 chars
    REQUIRE(to_hex(h).size() == 64);
}

TEST_CASE("sha256 produces different digests for different inputs", "[crypto][hash]") {
    REQUIRE(sha256("hello") != sha256("world"));
}

TEST_CASE("sha512: empty string known vector", "[crypto][hash]") {
    auto h = sha512("");
    // SHA-512("") starts with cf83e135...
    std::string hex = to_hex(h);
    REQUIRE(hex.size() == 128);
    REQUIRE(hex.substr(0, 8) == "cf83e135");
}

TEST_CASE("blake2b produces 64-byte digest", "[crypto][hash]") {
    auto h = blake2b("test");
    REQUIRE(h.size() == 64);
}

TEST_CASE("blake2b differs with and without key", "[crypto][hash]") {
    auto h1 = blake2b("data");
    auto h2 = blake2b("data", "mykey");
    REQUIRE(h1 != h2);
}

// ---------------------------------------------------------------------------
// Random bytes
// ---------------------------------------------------------------------------

TEST_CASE("random_bytes returns requested length", "[crypto][random]") {
    auto r = random_bytes(32);
    REQUIRE(r.size() == 32);
}

TEST_CASE("two random_bytes calls produce different results", "[crypto][random]") {
    auto a = random_bytes(16);
    auto b = random_bytes(16);
    REQUIRE(a != b);
}

TEST_CASE("random_string returns requested length", "[crypto][random]") {
    auto s = random_string(64);
    REQUIRE(s.size() == 64);
}

// ---------------------------------------------------------------------------
// Ed25519 signatures
// ---------------------------------------------------------------------------

TEST_CASE("sign::generate_keypair produces distinct keys", "[crypto][sign]") {
    auto kp1 = sign::generate_keypair();
    auto kp2 = sign::generate_keypair();
    REQUIRE(kp1.pk != kp2.pk);
    REQUIRE(kp1.sk != kp2.sk);
}

TEST_CASE("sign: combined sign + verify round-trip", "[crypto][sign]") {
    auto kp     = sign::generate_keypair();
    auto signed_msg = sign::sign_combined("hello, world", kp.sk);
    auto result     = sign::verify_combined(signed_msg, kp.pk);
    REQUIRE(result.has_value());
    REQUIRE(*result == "hello, world");
}

TEST_CASE("sign: verify fails with wrong public key", "[crypto][sign]") {
    auto kp1 = sign::generate_keypair();
    auto kp2 = sign::generate_keypair();
    auto signed_msg = sign::sign_combined("data", kp1.sk);
    REQUIRE_FALSE(sign::verify_combined(signed_msg, kp2.pk).has_value());
}

TEST_CASE("sign: verify fails with tampered message", "[crypto][sign]") {
    auto kp = sign::generate_keypair();
    auto signed_msg = sign::sign_combined("original", kp.sk);
    // Flip a byte in the payload region (past the 64-byte signature prefix)
    if (signed_msg.size() > 64)
        signed_msg[64] ^= 0xFF;
    REQUIRE_FALSE(sign::verify_combined(signed_msg, kp.pk).has_value());
}

TEST_CASE("sign: detached sign + verify round-trip", "[crypto][sign]") {
    auto kp  = sign::generate_keypair();
    auto sig = sign::sign_detached("message", kp.sk);
    REQUIRE(sign::verify_detached("message", sig, kp.pk));
}

TEST_CASE("sign: detached verify fails with modified message", "[crypto][sign]") {
    auto kp  = sign::generate_keypair();
    auto sig = sign::sign_detached("original", kp.sk);
    REQUIRE_FALSE(sign::verify_detached("modified", sig, kp.pk));
}

// ---------------------------------------------------------------------------
// secretbox symmetric AEAD
// ---------------------------------------------------------------------------

TEST_CASE("secretbox: generate_key produces KEY_BYTES-byte key", "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    REQUIRE(key.size() == secretbox::KEY_BYTES);
}

TEST_CASE("secretbox: encrypt + decrypt round-trip", "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    auto ct  = secretbox::encrypt("hello, secret world", key);
    auto pt  = secretbox::decrypt(ct, key);
    REQUIRE(pt.has_value());
    REQUIRE(*pt == "hello, secret world");
}

TEST_CASE("secretbox: decrypting with wrong key returns nullopt", "[crypto][secretbox]") {
    auto key1 = secretbox::generate_key();
    auto key2 = secretbox::generate_key();
    auto ct   = secretbox::encrypt("data", key1);
    REQUIRE_FALSE(secretbox::decrypt(ct, key2).has_value());
}

TEST_CASE("secretbox: tampered ciphertext fails authentication", "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    auto ct  = secretbox::encrypt("data", key);
    // Flip a byte in the MAC region (right after the nonce)
    ct[secretbox::NONCE_BYTES] ^= 0xFF;
    REQUIRE_FALSE(secretbox::decrypt(ct, key).has_value());
}

TEST_CASE("secretbox: two encryptions of same plaintext produce different ciphertexts",
          "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    auto ct1 = secretbox::encrypt("same", key);
    auto ct2 = secretbox::encrypt("same", key);
    // Different random nonces → different ciphertexts
    REQUIRE(ct1 != ct2);
}

TEST_CASE("secretbox: empty plaintext encrypts and decrypts", "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    auto ct  = secretbox::encrypt("", key);
    auto pt  = secretbox::decrypt(ct, key);
    REQUIRE(pt.has_value());
    REQUIRE(pt->empty());
}

TEST_CASE("secretbox: decrypt short input returns nullopt", "[crypto][secretbox]") {
    auto key = secretbox::generate_key();
    REQUIRE_FALSE(secretbox::decrypt("short", key).has_value());
}

// ---------------------------------------------------------------------------
// box public-key AEAD
// ---------------------------------------------------------------------------

TEST_CASE("box: encrypt + decrypt round-trip", "[crypto][box]") {
    auto alice = box::generate_keypair();
    auto bob   = box::generate_keypair();

    auto ct = box::encrypt("secret message", bob.pk, alice.sk);
    auto pt = box::decrypt(ct, alice.pk, bob.sk);

    REQUIRE(pt.has_value());
    REQUIRE(*pt == "secret message");
}

TEST_CASE("box: decrypt with wrong key returns nullopt", "[crypto][box]") {
    auto alice = box::generate_keypair();
    auto bob   = box::generate_keypair();
    auto eve   = box::generate_keypair();

    auto ct = box::encrypt("data", bob.pk, alice.sk);
    // Eve tries to decrypt with her own key — must fail
    REQUIRE_FALSE(box::decrypt(ct, alice.pk, eve.sk).has_value());
}

// ---------------------------------------------------------------------------
// to_hex utility
// ---------------------------------------------------------------------------

TEST_CASE("to_hex produces correct lowercase hex", "[crypto][util]") {
    uint8_t data[] = {0x00, 0xAB, 0xFF};
    REQUIRE(to_hex(data, 3) == "00abff");
}

#else
TEST_CASE("crypto module skipped (PICO_ENABLE_TLS not set)", "[crypto]") {
    SUCCEED("TLS/OpenSSL not compiled — skipping crypto tests");
}
#endif
