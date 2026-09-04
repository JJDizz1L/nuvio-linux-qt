// Offline contract for plugin crypto: digests, HMAC, PBKDF2, AES
// modes, codecs (pinned vectors), plus RSA/ECDSA round-trips when the
// build carries OpenSSL. No network.
#include <nuvio/plugins/PluginCrypto.h>

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::plugins::CryptoError;
using nuvio::plugins::pluginAesDecrypt;
using nuvio::plugins::pluginAesEncrypt;
using nuvio::plugins::pluginBase64Decode;
using nuvio::plugins::pluginBase64Encode;
using nuvio::plugins::pluginBytesToHex;
using nuvio::plugins::pluginDigest;
using nuvio::plugins::pluginHexToBytes;
using nuvio::plugins::pluginHexToUtf8;
using nuvio::plugins::pluginHmac;
using nuvio::plugins::pluginPbkdf2;
using nuvio::plugins::pluginRandomBytes;
using nuvio::plugins::pluginSign;
using nuvio::plugins::pluginUtf8ToHex;
using nuvio::plugins::pluginVerify;

#ifdef HAVE_OPENSSL
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

// In-test key generation (ephemeral, never stored). Note: the legacy
// EVP_PKEY_CTX EC keygen path returns success with a NULL key on this
// OpenSSL build (verified); the Q_keygen one-shot API works.
QByteArray testGenRsaPrivateKey()
{
    QByteArray out;
    EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", 2048);
    if (key) {
        unsigned char* der = nullptr;
        const int len = i2d_PrivateKey(key, &der);
        if (len > 0) out = QByteArray(reinterpret_cast<char*>(der), len);
        OPENSSL_free(der);
    }
    EVP_PKEY_free(key);
    return out;
}

QByteArray testDerPublicKey(EVP_PKEY* key)
{
    // SubjectPublicKeyInfo (what the bridge contract requires).
    QByteArray out;
    unsigned char* der = nullptr;
    const int len = i2d_PUBKEY(key, &der);
    if (len > 0) out = QByteArray(reinterpret_cast<char*>(der), len);
    OPENSSL_free(der);
    return out;
}

QByteArray testGenRsaPublicKey(const QByteArray& priv)
{
    const auto* p =
        reinterpret_cast<const unsigned char*>(priv.constData());
    EVP_PKEY* key = d2i_PrivateKey(EVP_PKEY_RSA, nullptr, &p,
                                   long(priv.size()));
    const QByteArray out = key ? testDerPublicKey(key) : QByteArray();
    EVP_PKEY_free(key);
    return out;
}

QByteArray testGenEcPrivateKey()
{
    QByteArray out;
    EVP_PKEY* key =
        EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1");
    if (key) {
        unsigned char* der = nullptr;
        const int len = i2d_PrivateKey(key, &der);
        if (len > 0) out = QByteArray(reinterpret_cast<char*>(der), len);
        OPENSSL_free(der);
    }
    EVP_PKEY_free(key);
    return out;
}

QByteArray testGenEcPublicKey(const QByteArray& priv)
{
    const auto* p =
        reinterpret_cast<const unsigned char*>(priv.constData());
    EVP_PKEY* key =
        d2i_PrivateKey(EVP_PKEY_EC, nullptr, &p, long(priv.size()));
    const QByteArray out = key ? testDerPublicKey(key) : QByteArray();
    EVP_PKEY_free(key);
    return out;
}
#endif

static QString hex(const QByteArray& b) { return pluginBytesToHex(b); }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    { // digests (pinned vectors)
        CHECK(hex(pluginDigest("MD5", "abc")) ==
                  "900150983cd24fb0d6963f7d28e17f72",
              "md5");
        CHECK(hex(pluginDigest("SHA-1", "abc")) ==
                  "a9993e364706816aba3e25717850c26c9cd0d89d",
              "sha1 dash-name");
        CHECK(hex(pluginDigest("sha256", "abc")) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256");
        CHECK(hex(pluginDigest("SHA384", "abc")) ==
                  "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b6"
                  "05a43ff5bed8086072ba1e7cc2358baeca134c825a7",
              "sha384");
        CHECK(hex(pluginDigest("SHA_512", "abc")).startsWith("ddaf35a193617ab"),
              "sha512 underscore-name");
        bool threw = false;
        try {
            pluginDigest("SHA999", "abc");
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw, "unknown digest throws");
    }

    { // HMAC-SHA256 RFC 4231 TC1 + MD5 spot
        const QByteArray key(20, char(0x0b));
        CHECK(hex(pluginHmac("SHA256", key, "Hi There")) ==
                  "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e93"
                  "76c2e32cff7",
              "hmac-sha256 rfc4231");
        CHECK(hex(pluginHmac("HmacMD5", "key",
                             "The quick brown fox jumps over the lazy dog")) ==
                  "80070713463e7749b90c2dc24911e275",
              "hmac-md5 prefixed name");
    }

    { // PBKDF2-HMAC-SHA1 RFC 6070 vectors
        CHECK(hex(pluginPbkdf2("password", "salt", 1, 160, "SHA1")) ==
                  "0c60c80f961f0e71f3a9b524af6012062fe037a6",
              "pbkdf2 c=1");
        CHECK(hex(pluginPbkdf2("password", "salt", 2, 160, "SHA1")) ==
                  "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957",
              "pbkdf2 c=2");
        CHECK(hex(pluginPbkdf2("password", "salt", 4096, 160, "SHA1")) ==
                  "4b007901b765489abead49d926f721d065a429c1",
              "pbkdf2 c=4096");
    }

    { // AES-CBC NIST SP 800-38A (encrypt exact + round-trips)
        const QByteArray k128 = pluginHexToBytes("2b7e151628aed2a6abf7158809cf4f3c");
        const QByteArray k192 = pluginHexToBytes(
            "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b");
        // Full F.2.5 key (authoritative, 64 hex chars).
        const QByteArray k256 = pluginHexToBytes(
            "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a"
            "30914dff4");
        const QByteArray iv =
            pluginHexToBytes("000102030405060708090a0b0c0d0e0f");
        const QByteArray pt =
            pluginHexToBytes("6bc1bee22e409f96e93d7e117393172a");
        CHECK(hex(pluginAesEncrypt("AES-CBC-NoPadding", k128, iv, pt)) ==
                  "7649abac8119b246cee98e9b12e9197d",
              "cbc-128 encrypt (exact block)");
        CHECK(hex(pluginAesEncrypt("AES-CBC-NoPadding", k192, iv, pt)) ==
                  "4f021db243bc633d7178183a9fa071e8",
              "cbc-192 encrypt");
        CHECK(hex(pluginAesEncrypt("AES-CBC-NoPadding", k256, iv, pt)) ==
                  "f58c4c04d6e5f1ba779eabfb5f7bfbd6",
              "cbc-256 encrypt");
        // Default mode string ("AES-CBC") pads PKCS5: decrypt strips.
        const QByteArray ct =
            pluginAesEncrypt("AES-CBC", k128, iv, "hello world, padded!");
        CHECK(pluginAesDecrypt("AES-CBC", k128, iv, ct) ==
                  "hello world, padded!",
              "cbc pkcs5 round-trip");
        CHECK(hex(pluginAesDecrypt("AES-CBC-NoPadding", k128, iv,
                                   pluginHexToBytes(
                                       "7649abac8119b246cee98e9b12e9197d"))) ==
                  hex(pt),
              "cbc nopadding decrypt");
        bool threw = false;
        try {
            pluginAesEncrypt("AES-CBC", QByteArray("short"), iv, pt);
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw, "bad key length throws");
    }

    { // AES-ECB NIST F.1 + GCM all-zero vectors (NIST 800-38D spirit)
        const QByteArray k128 =
            pluginHexToBytes("2b7e151628aed2a6abf7158809cf4f3c");
        const QByteArray k256 = pluginHexToBytes(
            "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a"
            "30914dff4");
        const QByteArray pt =
            pluginHexToBytes("6bc1bee22e409f96e93d7e117393172a");
        CHECK(hex(pluginAesEncrypt("AES-ECB-NoPadding", k128, "", pt)) ==
                  "3ad77bb40d7a3660a89ecaf32466ef97",
              "ecb-128 encrypt");
        CHECK(hex(pluginAesEncrypt("AES-ECB-NoPadding", k256, "", pt)) ==
                  "f3eed1bdb5d2a03c064b5a7e3db181f8",
              "ecb-256 encrypt");
        const QByteArray ecbCt =
            pluginAesEncrypt("AES-ECB", k128, "", "ecb padded round trip");
        CHECK(pluginAesDecrypt("AES-ECB", k128, "", ecbCt) ==
                  "ecb padded round trip",
              "ecb pkcs5 round-trip");
        CHECK(hex(pluginAesDecrypt("AES-ECB-NoPadding", k128, "",
                                   pluginHexToBytes(
                                       "3ad77bb40d7a3660a89ecaf32466ef97"))) ==
                  hex(pt),
              "ecb nopadding decrypt");
        const QByteArray zeroKey(16, char(0));
        const QByteArray zeroIv(12, char(0));
        // Empty plaintext: tag = E_K(J0) xor GHASH(empty) = 58e2fcce...
        CHECK(hex(pluginAesEncrypt("AES-GCM", zeroKey, zeroIv, "")) ==
                  "58e2fccefa7e3061367f1d57a4e7455a",
              "gcm empty tag vector");
        // 16 zero bytes: C=0388dace60b6a392, T=ab6e47d4...
        const QByteArray zeroPt(16, char(0));
        CHECK(hex(pluginAesEncrypt("AES-GCM", zeroKey, zeroIv, zeroPt)) ==
                  "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a"
                  "67b21257bddf",
              "gcm zero vector");
        const QByteArray blob = pluginAesEncrypt(
            "AES-GCM", zeroKey, zeroIv, "gcm round trip, any length!");
        CHECK(pluginAesDecrypt("AES-GCM", zeroKey, zeroIv, blob) ==
                  "gcm round trip, any length!",
              "gcm unpadded-length round-trip");
        QByteArray tampered = blob;
        tampered[0] = char(tampered[0] ^ 1);
        bool threw = false;
        try {
            pluginAesDecrypt("AES-GCM", zeroKey, zeroIv, tampered);
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw, "gcm tag tampering throws");
    }

    { // codecs + random
        CHECK(pluginBase64Encode("hello") == "aGVsbG8=", "b64 encode");
        CHECK(pluginBase64Decode("aGVsbG8=") == "hello", "b64 decode");
        CHECK(pluginBase64Decode("aGVsbG8") == "hello", "b64 unpadded");
        CHECK(!pluginBase64Decode("aGVs_bG8=").isEmpty(),
              "b64 url-safe forgiving");
        CHECK(pluginUtf8ToHex("Hi") == "4869", "utf8 to hex");
        CHECK(pluginHexToUtf8("4869") == "Hi", "hex to utf8");
        CHECK(pluginHexToBytes("0x4869abc").size() == 4, "0x + odd pad");
        CHECK(pluginHexToBytes("").isEmpty(), "empty hex");
        CHECK(pluginRandomBytes(0).isEmpty(), "zero random");
        const QByteArray r1 = pluginRandomBytes(16);
        const QByteArray r2 = pluginRandomBytes(16);
        CHECK(r1.size() == 16 && r1 != r2, "random differs");
        CHECK(pluginRandomBytes(7).size() == 7, "odd random length exact");
    }

#ifdef HAVE_OPENSSL
    { // RSA + ECDSA round-trips (generated in-test, never stored)
        extern QByteArray testGenRsaPrivateKey();
        extern QByteArray testGenRsaPublicKey(const QByteArray& priv);
        extern QByteArray testGenEcPrivateKey();
        extern QByteArray testGenEcPublicKey(const QByteArray& priv);
        const QByteArray rsaPriv = testGenRsaPrivateKey();
        const QByteArray rsaPub = testGenRsaPublicKey(rsaPriv);
        const QByteArray sig = pluginSign("RSASSA-PKCS1-V1_5-SHA256",
                                          rsaPriv, "data-to-sign");
        CHECK(!sig.isEmpty() &&
                  pluginVerify("RSASSA-PKCS1-V1_5-SHA256", rsaPub, sig,
                               "data-to-sign"),
              "rsa sign/verify round-trip");
        CHECK(!pluginVerify("RSASSA-PKCS1-V1_5-SHA256", rsaPub, sig,
                            "tampered"),
              "rsa tamper fails");
        const QByteArray ecPriv = testGenEcPrivateKey();
        const QByteArray ecPub = testGenEcPublicKey(ecPriv);
        const QByteArray esig =
            pluginSign("ECDSA-SHA256", ecPriv, "data-to-sign");
        CHECK(!esig.isEmpty() &&
                  pluginVerify("ECDSA-SHA256", ecPub, esig, "data-to-sign"),
              "ecdsa sign/verify round-trip");
        bool threw = false;
        try {
            pluginSign("RSA", QByteArray("garbage"), "x");
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw, "garbage private key throws");
    }
#else
    { // without OpenSSL the asymmetric bridges error honestly
        bool threw = false;
        try {
            pluginSign("RSA", QByteArray("x"), "y");
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw, "sign without openssl throws");
    }
#endif

    std::printf(failures ? "CRYPTO SUITE FAILURES=%d\n"
                         : "CRYPTO SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
