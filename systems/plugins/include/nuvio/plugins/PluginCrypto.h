#pragma once

// Plugin crypto primitives (fork PluginCrypto.desktop.kt parity):
// random, digests, HMAC, PBKDF2 (verbatim algorithm), AES
// (CBC/ECB/GCM-128 with PKCS5/NoPadding), RSA/ECDSA-SHA256 sign+verify
// (OpenSSL when available, honest errors otherwise), base64/hex/utf8
// codecs. Raw byte functions throw CryptoError on misuse (fork
// require/error parity); the legacy string bridges catch to "".

#include <QByteArray>
#include <QString>

#include <stdexcept>

namespace nuvio::plugins {

struct CryptoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[nodiscard]] QByteArray pluginRandomBytes(int length);
[[nodiscard]] QByteArray pluginDigest(const QString& algorithm,
                                      const QByteArray& data);
[[nodiscard]] QByteArray pluginHmac(const QString& algorithm,
                                    const QByteArray& key,
                                    const QByteArray& data);
[[nodiscard]] QByteArray pluginPbkdf2(const QByteArray& password,
                                      const QByteArray& salt, int iterations,
                                      int keySizeBits,
                                      const QString& algorithm);
[[nodiscard]] QByteArray pluginAesEncrypt(const QString& mode,
                                          const QByteArray& key,
                                          const QByteArray& iv,
                                          const QByteArray& data);
[[nodiscard]] QByteArray pluginAesDecrypt(const QString& mode,
                                          const QByteArray& key,
                                          const QByteArray& iv,
                                          const QByteArray& data);
[[nodiscard]] QByteArray pluginSign(const QString& algorithm,
                                    const QByteArray& privateKeyDer,
                                    const QByteArray& data);
[[nodiscard]] bool pluginVerify(const QString& algorithm,
                                const QByteArray& publicKeyDer,
                                const QByteArray& signature,
                                const QByteArray& data);

[[nodiscard]] QString pluginBase64Encode(const QString& data);
[[nodiscard]] QString pluginBase64Decode(const QString& data);
[[nodiscard]] QString pluginUtf8ToHex(const QString& value);
[[nodiscard]] QByteArray pluginHexToBytes(const QString& hex);
[[nodiscard]] QString pluginHexToUtf8(const QString& hex);
[[nodiscard]] QString pluginBytesToHex(const QByteArray& data);

} // namespace nuvio::plugins
