// Headless-pure suites: cache math, hwdec chain selection matrix, keymap
// coverage. No display server, no GPU, no libmpv handle required.
#include <QtTest/QtTest>

#include <nuvio/mpv/HwdecPolicy.h>
#include <nuvio/mpv/MpvKeyMap.h>
#include <nuvio/mpv/MpvTypes.h>

#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>

using namespace nuvio::mpv;

namespace {
constexpr qint64 MiB = 1024 * 1024;
}

class MpvPolicyTests final : public QObject {
    Q_OBJECT

private slots:
    // ---- cache-limit math (regression guard for the doubling bug) ---------
    void backBufferClamps()
    {
        QCOMPARE(demuxerBackBufferBytes(256 * MiB), 64ll * MiB);   // cap
        QCOMPARE(demuxerBackBufferBytes(2048 * MiB), 64ll * MiB);  // hard cap
        QCOMPARE(demuxerBackBufferBytes(64 * MiB), 16 * MiB);      // quarter
        QCOMPARE(demuxerBackBufferBytes(16 * MiB), 8 * MiB);       // floor
        QCOMPARE(demuxerBackBufferBytes(8 * MiB), 8 * MiB);        // floor holds
    }

    // ---- hwdec chain selection --------------------------------------------
    void overrideAlwaysWins()
    {
        qputenv("NUVIO_MPV_HWDEC", "vaapi-copy");
        QCOMPARE(HwdecPolicy::selectChain("nvidia", true),
                 QStringLiteral("vaapi-copy"));
        qunsetenv("NUVIO_MPV_HWDEC");
    }

    void nvidiaSessionPrefersNvdec()
    {
        qputenv("NUVIO_MPV_HWDEC", "");
        QVERIFY(HwdecPolicy::selectChain("", /*nvidiaViaFiles*/true)
                    .startsWith("nvdec"));
        QVERIFY(HwdecPolicy::selectChain("nvidia corporation", false)
                    .startsWith("nvdec"));
        // Never VAAPI-first on NVIDIA GL sessions (GLX/EGL conflict doctrine)
        QVERIFY(!HwdecPolicy::selectChain("nvidia corporation", true)
                     .startsWith("vaapi"));
        qunsetenv("NUVIO_MPV_HWDEC");
    }

    void mesaAmdAndIntelFavorVaapi()
    {
        QVERIFY(HwdecPolicy::selectChain("amd open source driver", false)
                    .startsWith("vaapi"));
        QVERIFY(HwdecPolicy::selectChain("mesa intel(r) graphics", false)
                    .startsWith("vaapi"));
    }

    void unknownFallsToAutoCopy()
    {
        QCOMPARE(HwdecPolicy::selectChain("", false),
                 QStringLiteral("auto-copy"));
    }

    // ---- keymap translation ------------------------------------------------
    void basicKeysTranslate()
    {
        const QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QCOMPARE(MpvKeyMap::textFor(&right).value_or(QString()),
                 QStringLiteral("right"));

        const QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QCOMPARE(MpvKeyMap::textFor(&space).value_or(QString()),
                 QStringLiteral("space"));
    }

    void modifiersComposeMpvStyle()
    {
        const QKeyEvent shiftA(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier);
        QCOMPARE(MpvKeyMap::textFor(&shiftA).value_or(QString()),
                 QStringLiteral("Shift+A"));

        const QKeyEvent plainA(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
        QCOMPARE(MpvKeyMap::textFor(&plainA).value_or(QString()),
                 QStringLiteral("a"));

        const QKeyEvent ctrlLeft(QEvent::KeyPress, Qt::Key_Left,
                                 Qt::ControlModifier);
        QCOMPARE(MpvKeyMap::textFor(&ctrlLeft).value_or(QString()),
                 QStringLiteral("Ctrl+left"));
    }

    void wheelMapsToWheelTokens()
    {
        const QPointF p(0, 0);
        const QWheelEvent up(p, p, QPoint(), QPoint(0, 120), Qt::NoButton,
                             Qt::NoModifier, Qt::NoScrollPhase, false);
        QCOMPARE(MpvKeyMap::textFor(&up).value_or(QString()),
                 QStringLiteral("WHEEL_UP"));
    }

    // ---- drm enumeration is environment-honest ------------------------------
    void drmEnumerationNeverThrowsAndIsAbsolute()
    {
        const auto nodes = HwdecPolicy::enumerateDrmRenderNodes();
        for (const QString& n : nodes)
            QVERIFY(n.startsWith("/dev/dri/renderD"));
    }
};

QTEST_MAIN(MpvPolicyTests)
#include "MpvPolicyTests.moc"
