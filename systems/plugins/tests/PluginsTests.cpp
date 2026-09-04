// Offline contract for the QuickJS engine wrapper: eval, exceptions,
// native bindings (typed args), JSON, promise pumping, deadline
// interrupt. No network.
#include <nuvio/plugins/JsEngine.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::plugins::JsEngine;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    { // eval + values
        JsEngine js;
        CHECK(js.valid(), "engine constructs");
        QString out;
        CHECK(js.evalToString("1 + 2", "t", &out) && out == "3",
              "arithmetic evals");
        CHECK(js.evalToString("'a' + 'b'", "t", &out) && out == "\"ab\"",
              "strings stringify quoted");
        CHECK(js.evalToString("({x:1,y:[true,null]})", "t", &out) &&
                  out == "{\"x\":1,\"y\":[true,null]}",
              "objects stringify");
        CHECK(js.evalToString("undefined", "t", &out) && out.isEmpty(),
              "undefined stringifies empty");
        QString err;
        CHECK(!js.eval("throw new Error('kaput')", "t", &err) &&
                  err.contains("kaput"),
              "exceptions captured with message");
        CHECK(!js.eval("var 1 = ;", "t", &err) && !err.isEmpty(),
              "syntax errors captured");
        CHECK(js.eval("var consecutive = 40 + 2;", "t") &&
                  js.globalToJson("consecutive") == "42",
              "globals persist across evals");
        CHECK(js.globalToJson("noSuchGlobal").isEmpty(),
              "missing globals read empty");
    }

    { // native bindings with typed args
        JsEngine js;
        js.addFunction("__add", [](const QVariantList& args) {
            return QVariant(args.value(0).toDouble() +
                            args.value(1).toDouble());
        });
        js.addFunction("__describe", [](const QVariantList& args) {
            QStringList bits;
            for (const QVariant& a : args) {
                if (!a.isValid())
                    bits.append("undef");
                else if (a.typeId() == QMetaType::Bool)
                    bits.append(a.toBool() ? "bool:T" : "bool:F");
                else if (a.typeId() == QMetaType::Double)
                    bits.append("num");
                else
                    bits.append("str:" + a.toString());
            }
            return QVariant(bits.join(","));
        });
        js.addFunction("__fail", [](const QVariantList&) {
            return QVariant();
        });
        QString out;
        CHECK(js.evalToString("__add(20, 22)", "t", &out) && out == "42",
              "numeric args + numeric result");
        CHECK(js.evalToString("__describe('x', 1, true, null, undefined)",
                              "t", &out) &&
                  out == "\"str:x,num,bool:T,undef,undef\"",
              "typed args convert");
        CHECK(js.evalToString("typeof __fail()", "t", &out) &&
                  out == "\"undefined\"",
              "invalid variants return undefined");
        QString err;
        CHECK(!js.eval("__add(", "t", &err), "bad call sites error");
    }

    { // promises pump through the job queue
        JsEngine js;
        QString out;
        CHECK(js.eval("var settled = 'no'; Promise.resolve(41 + 1).then("
                      "function(v) { settled = 'yes:' + v; });",
                      "t") &&
                  js.globalToJson("settled") == "\"yes:42\"",
              "microtasks drain inside eval");
    }

    { // deadline interrupts runaway code
        JsEngine js;
        js.setDeadlineMs(300);
        QString err;
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        const bool ok = js.eval("while (true) {}", "t", &err);
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - start;
        js.clearDeadline();
        CHECK(!ok && err.contains("interrupted"), "runaway interrupted");
        CHECK(elapsed < 10000, "interrupt fires promptly");
        // Engine survives the interrupt.
        QString out;
        CHECK(js.evalToString("6 * 7", "t", &out) && out == "42",
              "engine usable after interrupt");
    }

    std::printf(failures ? "PLUGINS SUITE FAILURES=%d\n"
                         : "PLUGINS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
