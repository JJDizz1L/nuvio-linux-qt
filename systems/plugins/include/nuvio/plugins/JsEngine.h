#pragma once

// Minimal RAII wrapper over vendored QuickJS (fork JsRuntime parity:
// one runtime + context per use). Offers eval with exception capture,
// typed native globals (bool/double/string, like the quickjs-kt
// bindings), JSON helpers, promise-job pumping, and a deadline
// interrupt (fork PLUGIN_TIMEOUT_MS parity).

#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

#include <quickjs.h>

namespace nuvio::plugins {

class JsEngine final {
public:
    /// Native global: typed args (bool/double/string from JS;
    /// objects arrive JSON-stringified by the caller, null/undefined
    /// as invalid), QVariant result (string/bool/double/invalid).
    using NativeFn = std::function<QVariant(const QVariantList& args)>;

    JsEngine();
    ~JsEngine();

    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    [[nodiscard]] bool valid() const { return m_ctx != nullptr; }

    /// Evaluates code; false + errorOut on exception. errorOut carries
    /// message + stack when available (fork console.error parity).
    bool eval(const QString& code, const QString& filename,
              QString* errorOut = nullptr);
    /// Evaluates an expression and JSON-stringifies a non-undefined
    /// result (empty when undefined).
    bool evalToString(const QString& code, const QString& filename,
                      QString* resultOut, QString* errorOut = nullptr);

    /// Defines a global sync native function.
    void addFunction(const QString& name, NativeFn fn);
    /// Defines a method on a global object, creating `{}` when absent
    /// (fork runtime.define("console") { function(...) } parity).
    void addObjectFunction(const QString& object, const QString& method,
                           NativeFn fn);
    /// Defines a global with raw JSValue control (for promises: the
    /// only bridge that must hand a live object back to JS).
    using RawFn = std::function<JSValue(JSContext* ctx,
                                        const QVariantList& args)>;
    void addRawFunction(const QString& name, RawFn fn);
    /// JSON.stringify(value) of a global (empty when missing).
    [[nodiscard]] QString globalToJson(const QString& name) const;
    /// Runs one pending promise job; true = more remain, false = queue
    /// drained or a job threw (errorOut carries the first failure).
    bool executePendingJobs(QString* errorOut = nullptr);

    /// Deadline interrupt for eval + job pumping (0 = none). Past the
    /// budget the engine throws "interrupted" (fork timeout parity).
    void setDeadlineMs(qint64 budgetMs);
    void clearDeadline();
    [[nodiscard]] bool deadlineExceeded() const;

    // Low-level surface for the async bridges (fetch resolves promises
    // from Qt slots; the engine stays single-threaded by contract).
    [[nodiscard]] JSContext* context() const { return m_ctx; }
    [[nodiscard]] static QString valueToString(JSContext* ctx,
                                               JSValueConst value);
    [[nodiscard]] static QString valueToJson(JSContext* ctx,
                                             JSValueConst value);
    [[nodiscard]] static QVariant valueToVariant(JSContext* ctx,
                                                 JSValueConst value);
    [[nodiscard]] static JSValue variantToValue(JSContext* ctx,
                                                const QVariant& value);

private:
    [[nodiscard]] QString takeException();
    static JSValue callTrampoline(JSContext* ctx, JSValueConst thisVal,
                                  int argc, JSValueConst* argv, int magic);
    static JSValue rawTrampoline(JSContext* ctx, JSValueConst thisVal,
                                 int argc, JSValueConst* argv, int magic);

    struct BoundFn {
        NativeFn fn;
    };
    struct BoundRaw {
        RawFn fn;
    };

    JSRuntime* m_runtime = nullptr;
    JSContext* m_ctx = nullptr;
    QList<BoundFn*> m_bound;
    QList<BoundRaw*> m_rawBound;
    qint64 m_deadlineMs = 0;
    qint64 m_startMs = 0;
};

} // namespace nuvio::plugins
