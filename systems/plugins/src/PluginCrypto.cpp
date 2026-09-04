#include "nuvio/plugins/PluginCrypto.h"

#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QRandomGenerator>

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/x509.h>
#endif

extern "C" {
// Per-size tiny-AES views (third_party/tiny-aes symbol prefixes).
// Context layouts match aes.h exactly (RoundKey[N] + Iv[16]).
void aes128_init_ctx(void* ctx, const uint8_t* key);
void aes128_init_ctx_iv(void* ctx, const uint8_t* key, const uint8_t* iv);
void aes128_ECB_encrypt(const void* ctx, uint8_t* buf);
void aes128_ECB_decrypt(const void* ctx, uint8_t* buf);
void aes128_CBC_encrypt_buffer(void* ctx, uint8_t* buf, size_t length);
void aes128_CBC_decrypt_buffer(void* ctx, uint8_t* buf, size_t length);
void aes192_init_ctx(void* ctx, const uint8_t* key);
void aes192_init_ctx_iv(void* ctx, const uint8_t* key, const uint8_t* iv);
void aes192_ECB_encrypt(const void* ctx, uint8_t* buf);
void aes192_ECB_decrypt(const void* ctx, uint8_t* buf);
void aes192_CBC_encrypt_buffer(void* ctx, uint8_t* buf, size_t length);
void aes192_CBC_decrypt_buffer(void* ctx, uint8_t* buf, size_t length);
void aes256_init_ctx(void* ctx, const uint8_t* key);
void aes256_init_ctx_iv(void* ctx, const uint8_t* key, const uint8_t* iv);
void aes256_ECB_encrypt(const void* ctx, uint8_t* buf);
void aes256_ECB_decrypt(const void* ctx, uint8_t* buf);
void aes256_CBC_encrypt_buffer(void* ctx, uint8_t* buf, size_t length);
void aes256_CBC_decrypt_buffer(void* ctx, uint8_t* buf, size_t length);
}

namespace nuvio::plugins {

namespace {

// Token normalization (fork normalizedAlgorithmToken parity:
// uppercase, strip -/_/space//).
[[nodiscard]] QString tokenOf(const QString& algorithm)
{
    QString t = algorithm.toUpper();
    t.remove(u'-');
    t.remove(u'_');
    t.remove(u'/');
    t.remove(u' ');
    return t;
}

[[nodiscard]] QCryptographicHash::Algorithm hashAlgo(const QString& token)
{
    if (token == QLatin1String("MD5")) return QCryptographicHash::Md5;
    if (token == QLatin1String("SHA1")) return QCryptographicHash::Sha1;
    if (token == QLatin1String("SHA256")) return QCryptographicHash::Sha256;
    if (token == QLatin1String("SHA384")) return QCryptographicHash::Sha384;
    if (token == QLatin1String("SHA512")) return QCryptographicHash::Sha512;
    throw CryptoError("Unsupported digest algorithm: " +
                      token.toStdString());
}

[[nodiscard]] QCryptographicHash::Algorithm hmacAlgo(
    const QString& token)
{
    QString t = token;
    if (t.startsWith(QLatin1String("HMAC"))) t = t.mid(4);
    if (t == QLatin1String("MD5")) return QCryptographicHash::Md5;
    if (t == QLatin1String("SHA1")) return QCryptographicHash::Sha1;
    if (t == QLatin1String("SHA256")) return QCryptographicHash::Sha256;
    if (t == QLatin1String("SHA384")) return QCryptographicHash::Sha384;
    if (t == QLatin1String("SHA512")) return QCryptographicHash::Sha512;
    throw CryptoError("Unsupported HMAC algorithm: " +
                      token.toStdString());
}

enum class AesMode { Cbc, Ecb, Gcm };

struct AesSpec {
    AesMode mode = AesMode::Cbc;
    bool noPadding = false;
};

[[nodiscard]] AesSpec aesSpec(const QString& mode)
{
    const QString t = tokenOf(mode);
    AesSpec spec;
    spec.noPadding = t.contains(QLatin1String("NOPADDING"));
    if (t.contains(QLatin1String("GCM")))
        spec.mode = AesMode::Gcm;
    else if (t.contains(QLatin1String("ECB")))
        spec.mode = AesMode::Ecb;
    else
        spec.mode = AesMode::Cbc;   // default (fork parity)
    return spec;
}

[[nodiscard]] QByteArray pkcs7Pad(const QByteArray& data)
{
    const int pad = 16 - (data.size() % 16);
    return data + QByteArray(pad, char(pad));
}

[[nodiscard]] QByteArray pkcs7Unpad(const QByteArray& data)
{
    if (data.isEmpty() || data.size() % 16 != 0)
        throw CryptoError("Invalid PKCS5 padding");
    const int pad = static_cast<unsigned char>(data.back());
    if (pad < 1 || pad > 16 || pad > data.size())
        throw CryptoError("Invalid PKCS5 padding");
    for (int i = data.size() - pad; i < data.size(); ++i) {
        if (static_cast<unsigned char>(data[i]) != pad)
            throw CryptoError("Invalid PKCS5 padding");
    }
    return data.left(data.size() - pad);
}

// Single-block ECB encrypt for the active key size (GCM + ECB modes).
struct AesKey {
    const QByteArray key;
    int bits = 0;   // 128/192/256

    void ecbEncrypt(uint8_t block[16]) const
    {
        const auto* k =
            reinterpret_cast<const uint8_t*>(key.constData());
        if (bits == 128) {
            alignas(4) uint8_t ctx[176 + 16];
            aes128_init_ctx(ctx, k);
            aes128_ECB_encrypt(ctx, block);
        } else if (bits == 192) {
            alignas(4) uint8_t ctx[208 + 16];
            aes192_init_ctx(ctx, k);
            aes192_ECB_encrypt(ctx, block);
        } else {
            alignas(4) uint8_t ctx[240 + 16];
            aes256_init_ctx(ctx, k);
            aes256_ECB_encrypt(ctx, block);
        }
    }
};

[[nodiscard]] AesKey checkedKey(const QByteArray& key)
{
    if (key.size() != 16 && key.size() != 24 && key.size() != 32)
        throw CryptoError("AES key must be 16, 24, or 32 bytes");
    return AesKey{key, int(key.size() * 8)};
}

[[nodiscard]] QByteArray ecbCrypt(const AesKey& k, const QByteArray& data,
                                  bool encrypt)
{
    if (data.size() % 16 != 0)
        throw CryptoError("ECB input must be a multiple of 16 bytes");
    QByteArray out = data;
    for (int i = 0; i < out.size(); i += 16) {
        uint8_t block[16];
        memcpy(block, out.constData() + i, 16);
        if (encrypt) {
            k.ecbEncrypt(block);
        } else {
            const auto* key =
                reinterpret_cast<const uint8_t*>(k.key.constData());
            if (k.bits == 128) {
                alignas(4) uint8_t ctx[176 + 16];
                aes128_init_ctx(ctx, key);
                aes128_ECB_decrypt(ctx, block);
            } else if (k.bits == 192) {
                alignas(4) uint8_t ctx[208 + 16];
                aes192_init_ctx(ctx, key);
                aes192_ECB_decrypt(ctx, block);
            } else {
                alignas(4) uint8_t ctx[240 + 16];
                aes256_init_ctx(ctx, key);
                aes256_ECB_decrypt(ctx, block);
            }
        }
        memcpy(out.data() + i, block, 16);
    }
    return out;
}

[[nodiscard]] QByteArray cbcCrypt(const AesKey& k, const QByteArray& iv,
                                  const QByteArray& data, bool encrypt)
{
    if (data.size() % 16 != 0)
        throw CryptoError("CBC input must be a multiple of 16 bytes");
    QByteArray out = data;
    auto* buf = reinterpret_cast<uint8_t*>(out.data());
    const auto* key = reinterpret_cast<const uint8_t*>(k.key.constData());
    const auto* ivec = reinterpret_cast<const uint8_t*>(iv.constData());
    if (k.bits == 128) {
        alignas(4) uint8_t ctx[176 + 16];
        aes128_init_ctx_iv(ctx, key, ivec);
        if (encrypt)
            aes128_CBC_encrypt_buffer(ctx, buf, size_t(out.size()));
        else
            aes128_CBC_decrypt_buffer(ctx, buf, size_t(out.size()));
    } else if (k.bits == 192) {
        alignas(4) uint8_t ctx[208 + 16];
        aes192_init_ctx_iv(ctx, key, ivec);
        if (encrypt)
            aes192_CBC_encrypt_buffer(ctx, buf, size_t(out.size()));
        else
            aes192_CBC_decrypt_buffer(ctx, buf, size_t(out.size()));
    } else {
        alignas(4) uint8_t ctx[240 + 16];
        aes256_init_ctx_iv(ctx, key, ivec);
        if (encrypt)
            aes256_CBC_encrypt_buffer(ctx, buf, size_t(out.size()));
        else
            aes256_CBC_decrypt_buffer(ctx, buf, size_t(out.size()));
    }
    return out;
}

// ---- AES-GCM (128-bit tags; AAD unsupported, fork parity) ---------------
// GF(2^128) multiply, big-endian words (NIST SP 800-38D Algorithm 1).
void ghashMul(const uint8_t x[16], const uint8_t y[16], uint8_t out[16])
{
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, y, 16);
    for (int i = 0; i < 128; ++i) {
        if (x[i >> 3] & (0x80 >> (i & 7))) {
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        }
        const bool lsb = v[15] & 1;
        for (int j = 15; j > 0; --j)
            v[j] = uint8_t((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(out, z, 16);
}

void ghashUpdate(const uint8_t h[16], const QByteArray& data, uint8_t y[16])
{
    const auto* p = reinterpret_cast<const uint8_t*>(data.constData());
    int remaining = data.size();
    while (remaining > 0) {
        uint8_t block[16] = {0};
        const int take = std::min(remaining, 16);
        memcpy(block, p, size_t(take));
        for (int j = 0; j < 16; ++j) y[j] ^= block[j];
        uint8_t next[16];
        ghashMul(y, h, next);
        memcpy(y, next, 16);
        p += take;
        remaining -= take;
    }
}

void ghashLenBlock(uint64_t aBits, uint64_t cBits, uint8_t out[16])
{
    for (int i = 0; i < 8; ++i) {
        out[i] = uint8_t(aBits >> (56 - 8 * i));
        out[8 + i] = uint8_t(cBits >> (56 - 8 * i));
    }
}

// CTR keystream over inc32(counter) blocks (GCM jut: J0 never encrypts
// data; the first data block uses inc32(J0)).
[[nodiscard]] QByteArray gcmCtr(const AesKey& k, const uint8_t j0[16],
                                const QByteArray& input)
{
    QByteArray out(input.size(), Qt::Uninitialized);
    uint8_t counter[16];
    memcpy(counter, j0, 16);
    const auto* in = reinterpret_cast<const uint8_t*>(input.constData());
    auto* dest = reinterpret_cast<uint8_t*>(out.data());
    int remaining = input.size();
    int offset = 0;
    while (remaining > 0) {
        // inc32 (big-endian low word).
        for (int i = 15; i >= 12; --i) {
            if (++counter[i] != 0) break;
        }
        uint8_t keystream[16];
        memcpy(keystream, counter, 16);
        k.ecbEncrypt(keystream);
        const int take = std::min(remaining, 16);
        for (int i = 0; i < take; ++i)
            dest[offset + i] = uint8_t(in[offset + i] ^ keystream[i]);
        offset += take;
        remaining -= take;
    }
    return out;
}

[[nodiscard]] QByteArray gcmCrypt(const AesKey& k, const QByteArray& iv,
                                  const QByteArray& input, bool encrypt,
                                  const QByteArray* tagIn = nullptr)
{
    if (iv.size() != 12)
        throw CryptoError("AES-GCM needs a 12-byte IV in this build");
    uint8_t h[16] = {0};
    k.ecbEncrypt(h);
    uint8_t j0[16] = {0};
    memcpy(j0, iv.constData(), 12);
    j0[15] = 1;
    const QByteArray text = gcmCtr(k, j0, input);
    // GHASH over (AAD empty, ciphertext): full blocks of C, then lengths.
    const QByteArray& cipher = encrypt ? text : input;
    uint8_t y[16] = {0};
    ghashUpdate(h, cipher, y);
    uint8_t lenBlock[16];
    ghashLenBlock(0, uint64_t(cipher.size()) * 8, lenBlock);
    for (int j = 0; j < 16; ++j) y[j] ^= lenBlock[j];
    uint8_t s[16];
    ghashMul(y, h, s);
    uint8_t eJ0[16];
    memcpy(eJ0, j0, 16);
    k.ecbEncrypt(eJ0);
    uint8_t tag[16];
    for (int j = 0; j < 16; ++j) tag[j] = uint8_t(s[j] ^ eJ0[j]);
    if (encrypt) {
        return text + QByteArray(reinterpret_cast<const char*>(tag), 16);
    }
    if (!tagIn || tagIn->size() != 16)
        throw CryptoError("AES-GCM ciphertext needs a 16-byte tag");
    uint8_t diff = 0;
    const auto* want = reinterpret_cast<const uint8_t*>(tagIn->constData());
    for (int j = 0; j < 16; ++j) diff |= uint8_t(tag[j] ^ want[j]);
    if (diff != 0) throw CryptoError("AES-GCM tag mismatch");
    return text;
}

} // namespace

QByteArray pluginRandomBytes(int length)
{
    if (length < 0)
        throw CryptoError("Random byte length must be non-negative");
    if (length == 0) return {};
    QByteArray out((length + 3) / 4 * 4, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(out.data()),
        size_t(out.size() / 4));
    out.truncate(length);
    return out;
}

QByteArray pluginDigest(const QString& algorithm, const QByteArray& data)
{
    QCryptographicHash hash(hashAlgo(tokenOf(algorithm)));
    hash.addData(data);
    return hash.result();
}

QByteArray pluginHmac(const QString& algorithm, const QByteArray& key,
                      const QByteArray& data)
{
    return QMessageAuthenticationCode::hash(data, key,
                                            hmacAlgo(tokenOf(algorithm)));
}

QByteArray pluginPbkdf2(const QByteArray& password, const QByteArray& salt,
                        int iterations, int keySizeBits,
                        const QString& algorithm)
{
    // Verbatim port of the fork's PBKDF2 (single-shot HMAC per U).
    if (iterations <= 0)
        throw CryptoError("PBKDF2 iterations must be positive");
    if (keySizeBits <= 0 || keySizeBits % 8 != 0)
        throw CryptoError(
            "PBKDF2 key size must be a positive byte-aligned bit length");
    QString prfToken = tokenOf(algorithm);   // validated by hmacAlgo
    const auto prf = hmacAlgo(prfToken);
    if (prfToken.startsWith(QLatin1String("HMAC")))
        prfToken = prfToken.mid(4);
    // HMAC output length == hash length (no probe call needed).
    const int hLen = prfToken == "MD5"      ? 16
                     : prfToken == "SHA1"   ? 20
                     : prfToken == "SHA384" ? 48
                     : prfToken == "SHA512" ? 64
                                            : 32;   // SHA256 default
    const int dkLen = keySizeBits / 8;
    QByteArray dk(dkLen, Qt::Uninitialized);
    const int blocks = (dkLen + hLen - 1) / hLen;
    QByteArray u(hLen, Qt::Uninitialized);
    QByteArray t(hLen, Qt::Uninitialized);
    for (int i = 1; i <= blocks; ++i) {
        QByteArray first = salt;
        first.append(char((i >> 24) & 0xff));
        first.append(char((i >> 16) & 0xff));
        first.append(char((i >> 8) & 0xff));
        first.append(char(i & 0xff));
        QByteArray u1 =
            QMessageAuthenticationCode::hash(first, password, prf);
        t = u1;
        u = u1;
        for (int j = 2; j <= iterations; ++j) {
            const QByteArray uj =
                QMessageAuthenticationCode::hash(u, password, prf);
            u = uj;
            for (int k = 0; k < hLen; ++k)
                t[k] = char(t[k] ^ uj[k]);
        }
        const int offset = (i - 1) * hLen;
        const int len = std::min(hLen, dkLen - offset);
        memcpy(dk.data() + offset, t.constData(), size_t(len));
    }
    return dk;
}

QByteArray pluginAesEncrypt(const QString& mode, const QByteArray& key,
                            const QByteArray& iv, const QByteArray& data)
{
    const AesSpec spec = aesSpec(mode);
    const AesKey k = checkedKey(key);
    if (spec.mode == AesMode::Gcm) {
        if (iv.isEmpty())
            throw CryptoError("AES mode " + mode.toStdString() +
                              " requires an IV");
        // GCM never pads (JCA parity: the transformation is always
        // AES/GCM/NoPadding); arbitrary lengths are native to CTR.
        return gcmCrypt(k, iv, data, true);
    }
    if (spec.mode == AesMode::Ecb) {
        const QByteArray padded =
            spec.noPadding ? data : pkcs7Pad(data);
        return ecbCrypt(k, padded, true);
    }
    if (iv.isEmpty())
        throw CryptoError("AES mode " + mode.toStdString() +
                          " requires an IV");
    const QByteArray padded = spec.noPadding ? data : pkcs7Pad(data);
    return cbcCrypt(k, iv, padded, true);
}

QByteArray pluginAesDecrypt(const QString& mode, const QByteArray& key,
                            const QByteArray& iv, const QByteArray& data)
{
    const AesSpec spec = aesSpec(mode);
    const AesKey k = checkedKey(key);
    if (spec.mode == AesMode::Gcm) {
        if (iv.isEmpty())
            throw CryptoError("AES mode " + mode.toStdString() +
                              " requires an IV");
        if (data.size() < 16)
            throw CryptoError("AES-GCM ciphertext needs a 16-byte tag");
        const QByteArray body = data.left(data.size() - 16);
        const QByteArray tag = data.right(16);
        // No unpadding: GCM never pads (JCA NoPadding parity with encrypt).
        return gcmCrypt(k, iv, body, false, &tag);
    }
    if (spec.mode == AesMode::Ecb) {
        const QByteArray plain = ecbCrypt(k, data, false);
        return spec.noPadding ? plain : pkcs7Unpad(plain);
    }
    if (iv.isEmpty())
        throw CryptoError("AES mode " + mode.toStdString() +
                          " requires an IV");
    const QByteArray plain = cbcCrypt(k, iv, data, false);
    return spec.noPadding ? plain : pkcs7Unpad(plain);
}

QByteArray pluginSign(const QString& algorithm,
                      const QByteArray& privateKeyDer,
                      const QByteArray& data)
{
#ifdef HAVE_OPENSSL
    const QString t = tokenOf(algorithm);
    const bool rsa = t.startsWith(QLatin1String("RSASSA")) ||
                     !(t.startsWith(QLatin1String("ECDSA")));
    EVP_PKEY* key = nullptr;
    if (rsa) {
        const auto* p =
            reinterpret_cast<const unsigned char*>(privateKeyDer.constData());
        key = d2i_PrivateKey(EVP_PKEY_RSA, nullptr, &p,
                             long(privateKeyDer.size()));
    } else {
        const auto* p =
            reinterpret_cast<const unsigned char*>(privateKeyDer.constData());
        key = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &p,
                             long(privateKeyDer.size()));
    }
    if (!key)
        throw CryptoError("Unparseable private key (PKCS8 DER expected)");
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    QByteArray sig;
    if (ctx &&
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
        EVP_DigestSignUpdate(ctx, data.constData(),
                             size_t(data.size())) == 1) {
        size_t len = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &len) == 1) {
            sig.resize(int(len));
            if (EVP_DigestSignFinal(
                    ctx, reinterpret_cast<unsigned char*>(sig.data()),
                    &len) == 1)
                sig.truncate(int(len));
            else
                sig.clear();
        }
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    if (sig.isEmpty()) throw CryptoError("Signing failed");
    return sig;
#else
    Q_UNUSED(algorithm);
    Q_UNUSED(privateKeyDer);
    Q_UNUSED(data);
    throw CryptoError("Asymmetric signing unavailable in this build "
                      "(OpenSSL not found at compile time)");
#endif
}

bool pluginVerify(const QString& algorithm, const QByteArray& publicKeyDer,
                  const QByteArray& signature, const QByteArray& data)
{
#ifdef HAVE_OPENSSL
    Q_UNUSED(algorithm);
    // SubjectPublicKeyInfo DER (JCA X509EncodedKeySpec parity);
    // the key itself selects RSA vs EC.
    const auto* p =
        reinterpret_cast<const unsigned char*>(publicKeyDer.constData());
    EVP_PKEY* key = d2i_PUBKEY(nullptr, &p, long(publicKeyDer.size()));
    if (!key) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx &&
        EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key) ==
            1 &&
        EVP_DigestVerifyUpdate(ctx, data.constData(),
                               size_t(data.size())) == 1) {
        ok = EVP_DigestVerifyFinal(
                 ctx,
                 reinterpret_cast<const unsigned char*>(
                     signature.constData()),
                 size_t(signature.size())) == 1;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
#else
    Q_UNUSED(algorithm);
    Q_UNUSED(publicKeyDer);
    Q_UNUSED(signature);
    Q_UNUSED(data);
    throw CryptoError("Asymmetric verification unavailable in this build "
                      "(OpenSSL not found at compile time)");
#endif
}

QString pluginBase64Encode(const QString& data)
{
    return QString::fromLatin1(data.toUtf8().toBase64());
}

QString pluginBase64Decode(const QString& data)
{
    // Forgiving decode (fork parity): trims, strips whitespace,
    // accepts base64url, re-pads.
    QString normalized = data.trimmed();
    normalized.remove(u'\n');
    normalized.remove(u'\r');
    normalized.remove(u' ');
    normalized.replace(u'-', u'+');
    normalized.replace(u'_', u'/');
    const int pad = (4 - (normalized.size() % 4)) % 4;
    normalized += QString(pad, u'=');
    const QByteArray decoded =
        QByteArray::fromBase64(normalized.toLatin1());
    return QString::fromUtf8(decoded);
}

QString pluginUtf8ToHex(const QString& value)
{
    return pluginBytesToHex(value.toUtf8());
}

QByteArray pluginHexToBytes(const QString& hex)
{
    // trim, lowercase, strip spaces + 0x, odd-length left-padded.
    QString normalized = hex.trimmed().toLower();
    normalized.remove(u' ');
    if (normalized.startsWith(QLatin1String("0x")))
        normalized = normalized.mid(2);
    if (normalized.isEmpty()) return {};
    if (normalized.size() % 2 != 0) normalized.prepend(u'0');
    QByteArray out(normalized.size() / 2, Qt::Uninitialized);
    for (int i = 0; i < out.size(); ++i) {
        bool ok = false;
        const int byte = normalized.mid(i * 2, 2).toInt(&ok, 16);
        out[i] = char(ok ? byte : 0);
    }
    return out;
}

QString pluginHexToUtf8(const QString& hex)
{
    return QString::fromUtf8(pluginHexToBytes(hex));
}

QString pluginBytesToHex(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex());
}

} // namespace nuvio::plugins
