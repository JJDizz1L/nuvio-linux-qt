#include "nuvio/plugins/PluginHost.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <stdexcept>

#include "nuvio/plugins/PluginCrypto.h"

namespace nuvio::plugins {

namespace {

constexpr int kMaxHeaderChars = 8 * 1024;   // fork MAX_FETCH_HEADER_VALUE
const QString kTruncSuffix = QStringLiteral("\n...[truncated]");
constexpr auto kDefaultUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

[[nodiscard]] QString truncateHeader(const QString& value)
{
    if (value.size() <= kMaxHeaderChars) return value;
    const int keep = kMaxHeaderChars - kTruncSuffix.size();
    if (keep <= 0) return kTruncSuffix.left(kMaxHeaderChars);
    return value.left(keep) + kTruncSuffix;
}

/// JSON string scalar (QJsonDocument rejects top-level scalars, P1
/// gotcha parity): quoted with backslash/quote/control escapes.
[[nodiscard]] QString jsonStringScalar(const QString& value)
{
    QString out;
    out.reserve(value.size() + 2);
    out.append(u'"');
    for (const QChar c : value) {
        switch (c.unicode()) {
        case u'"': out += QStringLiteral("\\\""); break;
        case u'\\': out += QStringLiteral("\\\\"); break;
        case u'\n': out += QStringLiteral("\\n"); break;
        case u'\r': out += QStringLiteral("\\r"); break;
        case u'\t': out += QStringLiteral("\\t"); break;
        default:
            if (c.unicode() < 0x20)
                out += QStringLiteral("\\u%1")
                           .arg(int(c.unicode()), 4, 16, QChar(u'0'));
            else
                out.append(c);
            break;
        }
    }
    out.append(u'"');
    return out;
}

[[nodiscard]] QString parseUrlJson(const QString& urlString)
{
    const QUrl url(urlString.trimmed(), QUrl::StrictMode);
    const auto empty = [] {
        return QStringLiteral(
            "{\"protocol\":\"\",\"host\":\"\",\"hostname\":\"\","
            "\"port\":\"\",\"pathname\":\"/\",\"search\":\"\",\"hash\":\"\"}");
    };
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return empty();
    const QString scheme = url.scheme().toLower();
    const int defaultPort =
        scheme == QLatin1String("https")  ? 443
        : scheme == QLatin1String("http") ? 80
                                           : -1;
    const int port = url.port(defaultPort);
    const QString host =
        (port != -1 && port != defaultPort)
            ? url.host() + u':' + QString::number(port)
            : url.host();
    QString path = url.path();
    if (path.isEmpty()) path = QStringLiteral("/");
    QString search;
    if (url.hasQuery()) search = u'?' + url.query();
    QString hash;
    if (!url.fragment().isEmpty()) hash = u'#' + url.fragment();
    QJsonObject out;
    out.insert(QStringLiteral("protocol"), scheme + u':');
    out.insert(QStringLiteral("host"), host);
    out.insert(QStringLiteral("hostname"), url.host());
    out.insert(QStringLiteral("port"),
               (port != -1 && port != defaultPort) ? QString::number(port)
                                                   : QString());
    out.insert(QStringLiteral("pathname"), path);
    out.insert(QStringLiteral("search"), search);
    out.insert(QStringLiteral("hash"), hash);
    return QString::fromUtf8(
        QJsonDocument(out).toJson(QJsonDocument::Compact));
}

} // namespace

PluginHost::PluginHost(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_logger = [](const QString& line) { qDebug().noquote() << line; };
}

QString PluginHost::loadPolyfill(const QString& scraperIdJson,
                                 const QString& settingsJson)
{
    QFile file(QStringLiteral(":/nuvio/plugins/resources/polyfill.js"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).arg(scraperIdJson,
                                                 settingsJson);
}

bool PluginHost::setup(const QString& scraperId,
                       const QString& settingsJson, ResultCallback onResult,
                       ResultCallback onSettings, QString* errorOut)
{
    if (!m_engine.valid()) {
        if (errorOut) *errorOut = QStringLiteral("no JS context");
        return false;
    }
    m_onResult = std::move(onResult);
    m_onSettings = std::move(onSettings);
    registerConsole(scraperId);
    m_engine.addFunction(QStringLiteral("__capture_result"),
                         [this](const QVariantList& args) {
                             if (m_onResult)
                                 m_onResult(args.value(0).toString());
                             return QVariant();
                         });
    m_engine.addFunction(QStringLiteral("__capture_settings_result"),
                         [this](const QVariantList& args) {
                             if (m_onSettings)
                                 m_onSettings(args.value(0).toString());
                             return QVariant();
                         });
    registerUrl();
    registerFetch();
    registerCrypto();
    bindDom();
    const QString polyfill =
        loadPolyfill(jsonStringScalar(scraperId), settingsJson);
    if (polyfill.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("polyfill resource missing");
        return false;
    }
    return m_engine.eval(polyfill, QStringLiteral("polyfill.js"), errorOut);
}

bool PluginHost::pumpUntil(const std::function<bool()>& done, int timeoutMs,
                           QString* errorOut)
{
    QDeadlineTimer deadline(timeoutMs);
    QString jobError;
    while (!deadline.hasExpired()) {
        if (done()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        // Drain the whole microtask queue per pump (settled promise
        // continuations must all run before re-checking done()).
        while (m_engine.executePendingJobs(&jobError)) {
            if (done()) return true;
        }
        if (!jobError.isEmpty()) {
            if (errorOut) *errorOut = jobError;
            return false;
        }
        if (done()) return true;
    }
    if (errorOut) *errorOut = QStringLiteral("plugin timed out");
    return false;
}

void PluginHost::registerConsole(const QString& scraperId)
{
    // fork runtime.define("console") { function(log/error/...) } parity.
    const auto line = [this, scraperId](const QString& level,
                                        const QVariantList& args) {
        QStringList bits;
        for (const QVariant& a : args) {
            if (!a.isValid()) {
                bits.append(QStringLiteral("null"));
            } else if (a.typeId() == QMetaType::Double) {
                JSValue v = JsEngine::variantToValue(m_engine.context(), a);
                bits.append(JsEngine::valueToString(m_engine.context(), v));
                JS_FreeValue(m_engine.context(), v);
            } else {
                bits.append(a.toString());
            }
        }
        if (m_logger)
            m_logger(QStringLiteral("Plugin:%1 %2: %3")
                         .arg(scraperId, level, bits.join(u' ')));
    };
    for (const QString& level :
         {QStringLiteral("log"), QStringLiteral("error"),
          QStringLiteral("warn"), QStringLiteral("info"),
          QStringLiteral("debug")}) {
        m_engine.addObjectFunction(
            QStringLiteral("console"), level,
            [line, level](const QVariantList& a) -> QVariant {
                line(level, a);
                return QVariant();
            });
    }
}

void PluginHost::registerUrl()
{
    m_engine.addFunction(QStringLiteral("__parse_url"),
                         [](const QVariantList& args) {
                             return QVariant(
                                 parseUrlJson(args.value(0).toString()));
                         });
}

void PluginHost::registerFetch()
{
    // Async bridge (fork asyncFunction parity): hands the live promise
    // to JS and resolves it from the QNAM slot; the caller pumps
    // jobs+events until the awaiting continuation runs.
    m_engine.addRawFunction(
        QStringLiteral("__native_fetch"),
        [this](JSContext* ctx,
               const QVariantList& args) -> JSValue {
            const QString url = args.value(0).toString();
            const QString method =
                args.value(1).toString().isEmpty()
                    ? QStringLiteral("GET")
                    : args.value(1).toString().toUpper();
            const QString headersJson = args.value(2).toString();
            const QString body = args.value(3).toString();
            const bool followRedirects =
                args.size() < 5 || args.value(4).toBool();
            JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
            JSValue promise = JS_NewPromiseCapability(ctx, resolving);
            if (JS_IsException(promise)) return promise;
            JSValue resolve = resolving[0];
            JS_FreeValue(ctx, resolving[1]);
            auto settle = [ctx, resolve,
                           url](const QString& resultJson) mutable {
                JSValue arg =
                    JS_NewString(ctx, resultJson.toUtf8().constData());
                JSValue r = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &arg);
                JS_FreeValue(ctx, arg);
                JS_FreeValue(ctx, r);
                JS_FreeValue(ctx, resolve);
            };
            QVariantMap headers;
            const QJsonObject parsed =
                QJsonDocument::fromJson(headersJson.toUtf8()).object();
            for (auto it = parsed.begin(); it != parsed.end(); ++it) {
                const QString v = it.value().toString();
                if (!v.isNull()) headers.insert(it.key(), v);
            }
            if (!headers.contains(QStringLiteral("User-Agent")))
                headers.insert(QStringLiteral("User-Agent"),
                               QString::fromLatin1(kDefaultUa));
            QNetworkRequest req{QUrl(url)};
            for (auto it = headers.begin(); it != headers.end(); ++it)
                req.setRawHeader(it.key().toUtf8(),
                                 it.value().toString().toUtf8());
            if (!followRedirects)
                req.setAttribute(
                    QNetworkRequest::RedirectPolicyAttribute,
                    QNetworkRequest::ManualRedirectPolicy);
            const QByteArray payload = body.toUtf8();
            QNetworkReply* rep = payload.isEmpty() && method == "GET"
                                     ? m_nam->get(req)
                                     : m_nam->sendCustomRequest(
                                           req, method.toUtf8(), payload);
            connect(rep, &QNetworkReply::finished, this,
                    [rep, settle, url]() mutable {
                        rep->deleteLater();
                        const int status = rep->attribute(
                                                   QNetworkRequest::
                                                       HttpStatusCodeAttribute)
                                               .toInt();
                        const QByteArray raw = rep->readAll();
                        QJsonObject out;
                        if (rep->error() == QNetworkReply::NoError) {
                            QJsonObject hdrs;
                            const auto pairs = rep->rawHeaderPairs();
                            for (const auto& pair : pairs) {
                                hdrs.insert(
                                    QString::fromLatin1(pair.first).toLower(),
                                    truncateHeader(QString::fromUtf8(
                                        pair.second)));
                            }
                            out.insert(QStringLiteral("ok"),
                                       status >= 200 && status < 300);
                            out.insert(QStringLiteral("status"), status);
                            out.insert(
                                QStringLiteral("statusText"),
                                QString::fromUtf8(
                                    rep->attribute(QNetworkRequest::
                                                       HttpReasonPhraseAttribute)
                                        .toByteArray()));
                            out.insert(QStringLiteral("url"),
                                       rep->url().toString());
                            out.insert(QStringLiteral("body"),
                                       QString::fromUtf8(raw));
                            out.insert(QStringLiteral("headers"), hdrs);
                        } else {
                            out.insert(QStringLiteral("ok"), false);
                            out.insert(QStringLiteral("status"), 0);
                            out.insert(
                                QStringLiteral("statusText"),
                                rep->errorString().isEmpty()
                                    ? QStringLiteral("Fetch failed")
                                    : rep->errorString());
                            out.insert(QStringLiteral("url"), url);
                            out.insert(QStringLiteral("body"), QString());
                            out.insert(QStringLiteral("headers"),
                                       QJsonObject{});
                        }
                        settle(QString::fromUtf8(QJsonDocument(out).toJson(
                            QJsonDocument::Compact)));
                    });
            return promise;   // adopted by JS (rejected never: errors
                              // resolve as ok:false, fork parity)
        });
}

void PluginHost::registerCrypto()
{
    const auto hexArg = [](const QVariantList& a, int i) {
        return pluginHexToBytes(a.value(i).toString());
    };
    const auto numArg = [](const QVariantList& a, int i, int fallback) {
        return a.value(i).isValid() ? a.value(i).toInt() : fallback;
    };
    const auto strArg = [](const QVariantList& a, int i,
                           const QString& fallback = {}) {
        return a.value(i).isValid() ? a.value(i).toString() : fallback;
    };
    m_engine.addFunction(
        QStringLiteral("__crypto_get_random_values_hex"),
        [](const QVariantList& a) {
            const int len = a.value(0).isValid() ? a.value(0).toInt() : 0;
            try {
                return QVariant(pluginBytesToHex(pluginRandomBytes(len)));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_digest_hex_raw"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginDigest(
                    strArg(a, 0, QStringLiteral("SHA256")), hexArg(a, 1))));
            } catch (...) {
                // Fork would throw into JS; a thrown binding error is
                // equally honest here (JS try/catch in the polyfill
                // surfaces it). Rethrow as a JS exception via message.
                throw std::runtime_error("digest failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_hmac_hex_raw"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginHmac(
                    strArg(a, 0, QStringLiteral("SHA256")), hexArg(a, 1),
                    hexArg(a, 2))));
            } catch (...) {
                throw std::runtime_error("hmac failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_pbkdf2_hex"),
        [hexArg, numArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginPbkdf2(
                    hexArg(a, 0), hexArg(a, 1), numArg(a, 2, 1000),
                    numArg(a, 3, 256), strArg(a, 4, QStringLiteral("SHA256")))));
            } catch (...) {
                throw std::runtime_error("pbkdf2 failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_aes_encrypt_hex"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginAesEncrypt(
                    strArg(a, 0, QStringLiteral("AES-CBC")), hexArg(a, 1),
                    hexArg(a, 2), hexArg(a, 3))));
            } catch (...) {
                throw std::runtime_error("aes encrypt failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_aes_decrypt_hex"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginAesDecrypt(
                    strArg(a, 0, QStringLiteral("AES-CBC")), hexArg(a, 1),
                    hexArg(a, 2), hexArg(a, 3))));
            } catch (...) {
                throw std::runtime_error("aes decrypt failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_sign_hex"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginSign(
                    strArg(a, 0), hexArg(a, 1), hexArg(a, 2))));
            } catch (...) {
                throw std::runtime_error("sign failed");
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_verify_hex"),
        [hexArg, strArg](const QVariantList& a) {
            try {
                return QVariant(pluginVerify(strArg(a, 0), hexArg(a, 1),
                                             hexArg(a, 2), hexArg(a, 3)));
            } catch (...) {
                throw std::runtime_error("verify failed");
            }
        });
    // Legacy Hex/String bridges (fork runCatching -> "" parity).
    m_engine.addFunction(
        QStringLiteral("__crypto_digest_hex"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(
                    pluginDigest(strArg(a, 0, QStringLiteral("SHA256")),
                                 a.value(1).toString().toUtf8())));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_hmac_hex"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBytesToHex(pluginHmac(
                    strArg(a, 0, QStringLiteral("SHA256")),
                    a.value(1).toString().toUtf8(),
                    a.value(2).toString().toUtf8())));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_base64_encode"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBase64Encode(strArg(a, 0)));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_base64_decode"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginBase64Decode(strArg(a, 0)));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_utf8_to_hex"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginUtf8ToHex(strArg(a, 0)));
            } catch (...) {
                return QVariant(QString());
            }
        });
    m_engine.addFunction(
        QStringLiteral("__crypto_hex_to_utf8"),
        [strArg](const QVariantList& a) {
            try {
                return QVariant(pluginHexToUtf8(strArg(a, 0)));
            } catch (...) {
                return QVariant(QString());
            }
        });
}

void PluginHost::bindDom()
{
    m_dom.bind([this](const QString& name, JsEngine::NativeFn fn) {
        m_engine.addFunction(name, std::move(fn));
    });
}

} // namespace nuvio::plugins
