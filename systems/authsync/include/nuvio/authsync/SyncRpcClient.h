#pragma once

// Postgrest RPC transport for profile sync (P4). Wire contract matches the
// Compose Supabase client exactly:
//   POST {base}/rest/v1/rpc/<fn>
//   apikey:        <anon key>          (always)
//   Authorization: Bearer <user JWT>   (falls back to anon key when signed out)
//   Content-Type / Accept: application/json
//   body: JSON object of p_* params
//
// Async only - never call from a QML entry point expecting blocking results
// (gotcha #7); orchestration wires finished() to state machines. OFFLINE
// TESTED against a local TCP fake endpoint asserting headers/body byte-level.

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"

class QNetworkAccessManager;

namespace nuvio::authsync {

/// Canonical sync function names (Protocol map, nuvio-linux-qt.md).
namespace SyncFn {
inline constexpr auto kPullProfileBlob = "sync_pull_profile_settings_blob";
inline constexpr auto kPushProfileBlob = "sync_push_profile_settings_blob";
} // namespace SyncFn

class SyncRpcClient final : public QObject {
    Q_OBJECT

public:
    /// Returns the current USER jwt; empty = anonymous (anon-key fallback).
    using TokenProvider = std::function<QByteArray()>;

    SyncRpcClient(AuthConfig cfg, TokenProvider token,
                  QObject* parent = nullptr);
    ~SyncRpcClient() override;

    /// Fires exactly one finished() per call.
    void call(const QString& fnName, const QJsonObject& params);

signals:
    /// ok=false carries the HTTP status; response holds the decoded body -
    /// postgrest functions commonly answer with a JSON ARRAY root (or a
    /// BARE SCALAR like the delta-cursor Long, which QJsonDocument cannot
    /// represent) so rawBody rides along for such cases.
    void finished(bool ok, int httpStatus, QJsonDocument response,
                  QByteArray rawBody);

private:
    AuthConfig m_cfg;
    TokenProvider m_token;
    class QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::authsync