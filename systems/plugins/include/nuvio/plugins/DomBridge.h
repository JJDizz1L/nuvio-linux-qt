#pragma once

// Plugin DOM bridge (fork DomBridge parity): cheerio-subset host
// functions over vendored Gumbo (HTML5 parsing) with a small CSS
// selector matcher (tag/.class/#id/[attr[=value]]/descendant/child/
// comma/:contains). Element handles are opaque per-bridge ids;
// text()/attr()/html() follow jsoup (ksoup) semantics, including the
// empty-attr-reads-as-undefined quirk.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include <gumbo.h>

#include "nuvio/plugins/JsEngine.h"

namespace nuvio::plugins {

/// Parses one selector group list; matches in document order.
[[nodiscard]] QList<const GumboNode*> selectNodes(const GumboNode* root,
                                                  const QString& selector);
/// jsoup Element.text() parity: descendant text, whitespace-normalized.
[[nodiscard]] QString nodeText(const GumboNode* node);
/// jsoup inner-html serialization (void elements left unclosed).
[[nodiscard]] QString nodeInnerHtml(const GumboNode* node);
/// jsoup outer-html serialization.
[[nodiscard]] QString nodeOuterHtml(const GumboNode* node);
/// Attribute value or empty (missing AND empty-valued both read "";
/// the bridge maps "" to __UNDEFINED__, fork parity).
[[nodiscard]] QString nodeAttr(const GumboNode* node, const QString& name);

class DomBridge final {
public:
    DomBridge();
    ~DomBridge();

    DomBridge(const DomBridge&) = delete;
    DomBridge& operator=(const DomBridge&) = delete;

    /// Parses html, returns a doc id (fork "doc_<n>_<rand>" shape).
    [[nodiscard]] QString load(const QString& html);
    /// CSS select under a doc; JSON array of element ids ("[]" on any
    /// failure, fork parity).
    [[nodiscard]] QString select(const QString& docId,
                                 const QString& selector) const;
    [[nodiscard]] QString find(const QString& docId,
                               const QString& elementId,
                               const QString& selector) const;
    [[nodiscard]] QString text(const QString& idsCsv) const;
    [[nodiscard]] QString html(const QString& docId,
                               const QString& elementId) const;
    [[nodiscard]] QString innerHtml(const QString& elementId) const;
    [[nodiscard]] QString attr(const QString& elementId,
                               const QString& name) const;
    [[nodiscard]] QString next(const QString& elementId) const;
    [[nodiscard]] QString prev(const QString& elementId) const;

    /// Registers the nine __cheerio_* globals on a JS engine.
    void bind(const std::function<void(const QString& name,
                                       JsEngine::NativeFn fn)>& addFunction);

    void clear();

private:
    [[nodiscard]] QString intern(const GumboNode* node) const;
    [[nodiscard]] const GumboNode* lookup(const QString& id) const;

    struct Doc {
        // Gumbo string pieces (tag names, original text) point INTO the
        // source buffer: it must outlive the parse tree.
        QByteArray source;
        GumboOutput* output = nullptr;
    };
    QHash<QString, Doc> m_docs;
    mutable QHash<QString, const GumboNode*> m_elements;
    mutable qint64 m_counter = 0;
};

} // namespace nuvio::plugins
