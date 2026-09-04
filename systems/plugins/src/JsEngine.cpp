#include "nuvio/plugins/JsEngine.h"

#include <QDateTime>

namespace nuvio::plugins {

namespace {

int interruptHandler(JSRuntime* rt, void* opaque)
{
    Q_UNUSED(rt);
    return static_cast<JsEngine*>(opaque)->deadlineExceeded() ? 1 : 0;
}

} // namespace

JsEngine::JsEngine()
{
    m_runtime = JS_NewRuntime();
    if (!m_runtime) return;
    JS_SetRuntimeOpaque(m_runtime, this);
    m_ctx = JS_NewContext(m_runtime);
}

JsEngine::~JsEngine()
{
    qDeleteAll(m_bound);
    m_bound.clear();
    qDeleteAll(m_rawBound);
    m_rawBound.clear();
    if (m_ctx) JS_FreeContext(m_ctx);
    if (m_runtime) JS_FreeRuntime(m_runtime);
}

QString JsEngine::valueToString(JSContext* ctx, JSValueConst value)
{
    if (JS_IsNull(value) || JS_IsUndefined(value)) return {};
    const char* cstr = JS_ToCString(ctx, value);
    if (!cstr) return {};
    const QString out = QString::fromUtf8(cstr);
    JS_FreeCString(ctx, cstr);
    return out;
}

QString JsEngine::valueToJson(JSContext* ctx, JSValueConst value)
{
    if (JS_IsUndefined(value)) return {};
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        JS_FreeValue(ctx, json);
        return {};
    }
    const QString out = valueToString(ctx, json);
    JS_FreeValue(ctx, json);
    return out;
}

QVariant JsEngine::valueToVariant(JSContext* ctx, JSValueConst value)
{
    if (JS_IsBool(value)) return QVariant(JS_ToBool(ctx, value) != 0);
    if (JS_IsNumber(value)) {
        double d = 0;
        JS_ToFloat64(ctx, &d, value);
        return QVariant(d);
    }
    if (JS_IsString(value)) return QVariant(valueToString(ctx, value));
    if (JS_IsNull(value) || JS_IsUndefined(value)) return QVariant();
    // Objects/arrays arrive JSON-stringified (bridges stringify
    // structured values on the JS side, fork parity).
    return QVariant(valueToJson(ctx, value));
}

JSValue JsEngine::variantToValue(JSContext* ctx, const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return JS_NewBool(ctx, value.toBool() ? 1 : 0);
    case QMetaType::Double:
    case QMetaType::Float:
        return JS_NewFloat64(ctx, value.toDouble());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return JS_NewInt64(ctx, value.toLongLong());
    case QMetaType::QString:
        return JS_NewString(ctx, value.toString().toUtf8().constData());
    default:
        break;
    }
    if (value.toString().isEmpty() && !value.isValid())
        return JS_UNDEFINED;
    return JS_NewString(ctx, value.toString().toUtf8().constData());
}

QString JsEngine::takeException()
{
    JSValue ex = JS_GetException(m_ctx);
    QString message = valueToString(m_ctx, ex);
    if (message.isEmpty()) message = QStringLiteral("JS exception");
    JSValue stack = JS_GetPropertyStr(m_ctx, ex, "stack");
    if (!JS_IsUndefined(stack)) {
        const QString stackText = valueToString(m_ctx, stack);
        if (!stackText.isEmpty() && stackText != message)
            message += QStringLiteral("\n") + stackText;
    }
    JS_FreeValue(m_ctx, stack);
    JS_FreeValue(m_ctx, ex);
    return message;
}

/* static */
JSValue JsEngine::rawTrampoline(JSContext* ctx, JSValueConst /*thisVal*/,
                                int argc, JSValueConst* argv, int magic)
{
    auto* self = static_cast<JsEngine*>(JS_GetRuntimeOpaque(
        JS_GetRuntime(ctx)));
    if (!self || magic < 0 || magic >= self->m_rawBound.size())
        return JS_ThrowInternalError(ctx, "bad native binding");
    QVariantList args;
    for (int i = 0; i < argc; ++i)
        args.append(valueToVariant(ctx, argv[i]));
    try {
        return self->m_rawBound[magic]->fn(ctx, args);
    } catch (...) {
        return JS_ThrowInternalError(ctx, "native binding threw");
    }
}

/* static */
JSValue JsEngine::callTrampoline(JSContext* ctx, JSValueConst /*thisVal*/,
                                 int argc, JSValueConst* argv, int magic)
{
    auto* self = static_cast<JsEngine*>(JS_GetRuntimeOpaque(
        JS_GetRuntime(ctx)));
    if (!self || magic < 0 || magic >= self->m_bound.size())
        return JS_ThrowInternalError(ctx, "bad native binding");
    QVariantList args;
    for (int i = 0; i < argc; ++i)
        args.append(valueToVariant(ctx, argv[i]));
    QVariant result;
    try {
        result = self->m_bound[magic]->fn(args);
    } catch (...) {
        return JS_ThrowInternalError(ctx, "native binding threw");
    }
    return variantToValue(ctx, result);
}

bool JsEngine::eval(const QString& code, const QString& filename,
                    QString* errorOut)
{
    if (!m_ctx) {
        if (errorOut) *errorOut = QStringLiteral("no JS context");
        return false;
    }
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    JS_SetInterruptHandler(m_runtime, interruptHandler, this);
    const QByteArray utf8 = code.toUtf8();
    const QByteArray name = filename.toUtf8();
    JSValue result =
        JS_Eval(m_ctx, utf8.constData(), static_cast<size_t>(utf8.size()),
                name.constData(), JS_EVAL_TYPE_GLOBAL);
    JS_SetInterruptHandler(m_runtime, nullptr, nullptr);
    if (JS_IsException(result)) {
        if (errorOut) *errorOut = takeException();
        JS_FreeValue(m_ctx, result);
        return false;
    }
    // Drain already-settled promise jobs (microtasks from plain eval).
    JSContext* ctx = nullptr;
    while (JS_ExecutePendingJob(m_runtime, &ctx) > 0)
        ;
    JS_FreeValue(m_ctx, result);
    return true;
}

bool JsEngine::evalToString(const QString& code, const QString& filename,
                            QString* resultOut, QString* errorOut)
{
    if (!m_ctx) {
        if (errorOut) *errorOut = QStringLiteral("no JS context");
        return false;
    }
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    JS_SetInterruptHandler(m_runtime, interruptHandler, this);
    const QByteArray utf8 = code.toUtf8();
    const QByteArray name = filename.toUtf8();
    JSValue result =
        JS_Eval(m_ctx, utf8.constData(), static_cast<size_t>(utf8.size()),
                name.constData(), JS_EVAL_TYPE_GLOBAL);
    JS_SetInterruptHandler(m_runtime, nullptr, nullptr);
    if (JS_IsException(result)) {
        if (errorOut) *errorOut = takeException();
        JS_FreeValue(m_ctx, result);
        return false;
    }
    if (resultOut) {
        // Promise results stringify only once settled; plain values now.
        *resultOut = valueToJson(m_ctx, result);
    }
    JS_FreeValue(m_ctx, result);
    return true;
}

void JsEngine::addFunction(const QString& name, NativeFn fn)
{
    if (!m_ctx) return;
    const int magic = m_bound.size();
    m_bound.append(new BoundFn{std::move(fn)});
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue func = JS_NewCFunctionMagic(m_ctx, callTrampoline,
                                        name.toUtf8().constData(), 0,
                                        JS_CFUNC_generic_magic, magic);
    JS_SetPropertyStr(m_ctx, global, name.toUtf8().constData(), func);
    JS_FreeValue(m_ctx, global);
}

void JsEngine::addObjectFunction(const QString& object,
                                 const QString& method, NativeFn fn)
{
    if (!m_ctx) return;
    const int magic = m_bound.size();
    m_bound.append(new BoundFn{std::move(fn)});
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue obj =
        JS_GetPropertyStr(m_ctx, global, object.toUtf8().constData());
    if (JS_IsUndefined(obj) || JS_IsNull(obj)) {
        JS_FreeValue(m_ctx, obj);
        obj = JS_NewObject(m_ctx);
        JS_SetPropertyStr(m_ctx, global, object.toUtf8().constData(), obj);
        obj = JS_GetPropertyStr(m_ctx, global,
                                object.toUtf8().constData());
    }
    JSValue func = JS_NewCFunctionMagic(m_ctx, callTrampoline,
                                        method.toUtf8().constData(), 0,
                                        JS_CFUNC_generic_magic, magic);
    JS_SetPropertyStr(m_ctx, obj, method.toUtf8().constData(), func);
    JS_FreeValue(m_ctx, obj);
    JS_FreeValue(m_ctx, global);
}

void JsEngine::addRawFunction(const QString& name, RawFn fn)
{
    if (!m_ctx) return;
    const int magic = m_rawBound.size();
    m_rawBound.append(new BoundRaw{std::move(fn)});
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue func = JS_NewCFunctionMagic(m_ctx, rawTrampoline,
                                        name.toUtf8().constData(), 0,
                                        JS_CFUNC_generic_magic, magic);
    JS_SetPropertyStr(m_ctx, global, name.toUtf8().constData(), func);
    JS_FreeValue(m_ctx, global);
}

QString JsEngine::globalToJson(const QString& name) const
{
    if (!m_ctx) return {};
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue value =
        JS_GetPropertyStr(m_ctx, global, name.toUtf8().constData());
    const QString out = valueToJson(m_ctx, value);
    JS_FreeValue(m_ctx, value);
    JS_FreeValue(m_ctx, global);
    return out;
}

bool JsEngine::executePendingJobs(QString* errorOut)
{
    if (!m_ctx) return false;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    JS_SetInterruptHandler(m_runtime, interruptHandler, this);
    JSContext* ctx = nullptr;
    const int rc = JS_ExecutePendingJob(m_runtime, &ctx);
    JS_SetInterruptHandler(m_runtime, nullptr, nullptr);
    if (rc < 0) {
        if (errorOut && errorOut->isEmpty())
            *errorOut = ctx ? takeException()
                            : QStringLiteral("promise job failed");
        return false;
    }
    return rc > 0;
}

void JsEngine::setDeadlineMs(qint64 budgetMs)
{
    m_deadlineMs = budgetMs;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    JS_SetInterruptHandler(m_runtime, interruptHandler, this);
}

void JsEngine::clearDeadline()
{
    m_deadlineMs = 0;
    JS_SetInterruptHandler(m_runtime, nullptr, nullptr);
}

bool JsEngine::deadlineExceeded() const
{
    return m_deadlineMs > 0 &&
           QDateTime::currentMSecsSinceEpoch() - m_startMs > m_deadlineMs;
}

} // namespace nuvio::plugins
