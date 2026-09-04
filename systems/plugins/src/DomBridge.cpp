#include "nuvio/plugins/DomBridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <gumbo.h>

#include "nuvio/plugins/JsEngine.h"

namespace nuvio::plugins {

namespace {

// :contains("x")/:contains('x') -> :contains(x) (fork rewrite parity).
[[nodiscard]] QString rewriteContains(const QString& selector)
{
    static const QRegularExpression kContains(
        QStringLiteral(":contains\\([\"']([^\"']+)[\"']\\)"));
    QString out = selector;
    return out.replace(kContains, QStringLiteral(":contains(\\1)"));
}

bool isElement(const GumboNode* node)
{
    return node && node->type == GUMBO_NODE_ELEMENT;
}

QString tagName(const GumboNode* node)
{
    if (!isElement(node)) return {};
    const GumboElement& el = node->v.element;
    if (el.original_tag.data) {
        // original_tag spans the raw source tag INCLUDING angle
        // brackets ("<div ...>"); the name ends at the first
        // whitespace, slash, or closing bracket.
        QString raw = QString::fromUtf8(el.original_tag.data,
                                        int(el.original_tag.length));
        if (raw.startsWith(u'<')) raw = raw.mid(1);
        const int end = raw.indexOf(
            QRegularExpression(QStringLiteral("[\\s/>]")));
        if (end >= 0) raw = raw.left(end);
        if (!raw.isEmpty()) return raw;
    }
    const char* name = gumbo_normalized_tagname(el.tag);
    return name ? QString::fromLatin1(name) : QString();
}

QStringList classList(const GumboNode* node)
{
    return nodeAttr(node, QStringLiteral("class"))
        .split(QRegularExpression(QStringLiteral("\\s+")),
               Qt::SkipEmptyParts);
}

/// Attribute presence (distinct from value: present-but-empty counts).
bool nodeHasAttr(const GumboNode* node, const QString& name)
{
    if (!isElement(node)) return false;
    const GumboVector& attrs = node->v.element.attributes;
    for (unsigned i = 0; i < attrs.length; ++i) {
        const auto* attr = static_cast<const GumboAttribute*>(attrs.data[i]);
        if (QString::fromUtf8(attr->name).compare(name,
                                                  Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

struct Compound {
    QString tag;          // "" = any ("*" too)
    QString id;
    QStringList classes;
    struct AttrCond {
        QString name;
        QString value;    // "" = presence only
        bool hasValue = false;
    };
    QList<AttrCond> attrs;
    QString contains;     // :contains(text), "" = none
    bool valid = true;
};

// Splits "a > b c" into compounds + combinators (' ' or '>').
struct Selector {
    QList<Compound> compounds;
    QList<QChar> combinators;   // size = compounds-1
};

Compound parseCompound(const QString& token)
{
    Compound c;
    int i = 0;
    const int n = token.size();
    // Optional leading tag/*.
    if (i < n && (token[i].isLetter() || token[i] == u'*')) {
        while (i < n && token[i] != u'.' && token[i] != u'#' &&
               token[i] != u'[' && token[i] != u':')
            ++i;
        c.tag = token.left(i);
        if (c.tag == u"*") c.tag.clear();
    }
    while (i < n) {
        const QChar ch = token[i];
        if (ch == u'.') {
            int j = ++i;
            while (j < n && token[j] != u'.' && token[j] != u'#' &&
                   token[j] != u'[' && token[j] != u':')
                ++j;
            c.classes.append(token.mid(i, j - i));
            i = j;
        } else if (ch == u'#') {
            int j = ++i;
            while (j < n && token[j] != u'.' && token[j] != u'#' &&
                   token[j] != u'[' && token[j] != u':')
                ++j;
            c.id = token.mid(i, j - i);
            i = j;
        } else if (ch == u'[') {
            const int close = token.indexOf(u']', i);
            if (close < 0) {
                c.valid = false;
                return c;
            }
            const QString body = token.mid(i + 1, close - i - 1);
            Compound::AttrCond cond;
            const int eq = body.indexOf(u'=');
            if (eq < 0) {
                cond.name = body.trimmed();
            } else {
                QString name = body.left(eq).trimmed();
                QString v = body.mid(eq + 1).trimmed();
                // jsoup suffix operators ([a^="v"], [a$="v"],
                // [a*="v"]): encoded as "base\x01op\x01value".
                QString op;
                if ((name.endsWith(u'^') || name.endsWith(u'$') ||
                     name.endsWith(u'*')) &&
                    name.size() > 1) {
                    op = name.right(1);
                    name = name.left(name.size() - 1);
                }
                if ((v.startsWith(u'"') && v.endsWith(u'"') && v.size() >= 2) ||
                    (v.startsWith(u'\'') && v.endsWith(u'\'') &&
                     v.size() >= 2))
                    v = v.mid(1, v.size() - 2);
                cond.name = name;
                cond.value = op.isEmpty() ? v
                                          : name + u'\x01' + op + u'\x01' + v;
                cond.hasValue = true;
            }
            c.attrs.append(cond);
            i = close + 1;
        } else if (ch == u':') {
            if (token.mid(i, 10) == QLatin1String(":contains(")) {
                const int close = token.indexOf(u')', i);
                if (close < 0) {
                    c.valid = false;
                    return c;
                }
                c.contains = token.mid(i + 10, close - i - 10);
                i = close + 1;
            } else {
                // Unknown pseudo-class: unmatchable (fork would throw
                // inside ksoup and return "[]"; invalid == empty here).
                c.valid = false;
                return c;
            }
        } else {
            c.valid = false;
            return c;
        }
    }
    return c;
}

Selector parseSelectorGroup(const QString& group)
{
    Selector sel;
    // Tokenize on spaces and '>' (quoted segments already rewritten).
    QString spaced = group;
    spaced.replace(u'>', QStringLiteral(" > "));
    const QStringList parts =
        spaced.split(QRegularExpression(QStringLiteral("\\s+")),
                     Qt::SkipEmptyParts);
    QChar pending = u' ';
    for (const QString& part : parts) {
        if (part == u">") {
            pending = u'>';
            continue;
        }
        sel.compounds.append(parseCompound(part));
        if (sel.compounds.size() > 1) sel.combinators.append(pending);
        pending = u' ';
    }
    return sel;
}

bool matchCompound(const GumboNode* node, const Compound& c)
{
    if (!c.valid || !isElement(node)) return false;
    if (!c.tag.isEmpty() &&
        tagName(node).compare(c.tag, Qt::CaseInsensitive) != 0)
        return false;
    if (!c.id.isEmpty() &&
        nodeAttr(node, QStringLiteral("id")) != c.id)
        return false;
    const QStringList classes = classList(node);
    for (const QString& want : c.classes) {
        if (!classes.contains(want, Qt::CaseSensitive)) return false;
    }
    for (const auto& cond : c.attrs) {
        if (!cond.hasValue) {
            if (!nodeHasAttr(node, cond.name)) return false;
            continue;
        }
        // Operator forms encoded as "base\x01op\x01value" above.
        const int sep = cond.value.indexOf(u'\x01');
        if (sep >= 0) {
            const QStringList bits = cond.value.split(u'\x01');
            if (bits.size() != 3) return false;
            const QString real = nodeAttr(node, bits[0]);
            const QString op = bits[1];
            const QString want = bits[2];
            if (op == u"^" && !real.startsWith(want)) return false;
            if (op == u"$" && !real.endsWith(want)) return false;
            if (op == u"*" && !real.contains(want)) return false;
            continue;
        }
        if (nodeAttr(node, cond.name) != cond.value) return false;
    }
    if (!c.contains.isEmpty()) {
        // jsoup :contains is case-insensitive over element text.
        if (!nodeText(node).contains(c.contains, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void collectDescendants(const GumboNode* node, QList<const GumboNode*>& out)
{
    if (!node) return;
    // Template contents stay inert per spec (fork never surfaces them).
    const GumboVector* kids = nullptr;
    if (node->type == GUMBO_NODE_ELEMENT)
        kids = &node->v.element.children;
    else if (node->type == GUMBO_NODE_DOCUMENT)
        kids = &node->v.document.children;
    else
        return;
    for (unsigned i = 0; i < kids->length; ++i) {
        const auto* kid = static_cast<const GumboNode*>(kids->data[i]);
        if (isElement(kid)) out.append(kid);
        collectDescendants(kid, out);
    }
}

QList<const GumboNode*> matchSelector(const GumboNode* root,
                                      const Selector& sel)
{
    QList<const GumboNode*> current;
    if (sel.compounds.isEmpty()) return current;
    // Seed: the root itself (jsoup matches it too) + document order.
    QList<const GumboNode*> all;
    if (isElement(root)) all.append(root);
    collectDescendants(root, all);
    for (const GumboNode* node : all) {
        if (matchCompound(node, sel.compounds.first()))
            current.append(node);
    }
    for (int step = 1; step < sel.compounds.size(); ++step) {
        QList<const GumboNode*> next;
        const QChar comb = sel.combinators[step - 1];
        for (const GumboNode* node : current) {
            if (comb == u'>') {
                if (!isElement(node)) continue;
                const GumboVector& kids = node->v.element.children;
                for (unsigned i = 0; i < kids.length; ++i) {
                    const auto* kid =
                        static_cast<const GumboNode*>(kids.data[i]);
                    if (matchCompound(kid, sel.compounds[step]) &&
                        !next.contains(kid))
                        next.append(kid);
                }
            } else {
                QList<const GumboNode*> desc;
                collectDescendants(node, desc);
                for (const GumboNode* kid : desc) {
                    if (matchCompound(kid, sel.compounds[step]) &&
                        !next.contains(kid))
                        next.append(kid);
                }
            }
        }
        current = next;
    }
    return current;
}

bool isVoidTag(const QString& tag)
{
    static const QStringList voids{
        QStringLiteral("area"),  QStringLiteral("base"),
        QStringLiteral("br"),    QStringLiteral("col"),
        QStringLiteral("embed"), QStringLiteral("hr"),
        QStringLiteral("img"),   QStringLiteral("input"),
        QStringLiteral("link"),  QStringLiteral("meta"),
        QStringLiteral("param"), QStringLiteral("source"),
        QStringLiteral("track"), QStringLiteral("wbr"),
    };
    return voids.contains(tag.toLower());
}

QString escapeAttr(const QString& value)
{
    QString out = value;
    out.replace(u'&', QStringLiteral("&amp;"));
    out.replace(u'"', QStringLiteral("&quot;"));
    return out;
}

void serializeChildren(const GumboNode* node, QString& out);

void serializeNode(const GumboNode* node, QString& out)
{
    if (!node) return;
    if (node->type == GUMBO_NODE_TEXT ||
        node->type == GUMBO_NODE_WHITESPACE) {
        out += QString::fromUtf8(node->v.text.text);
        return;
    }
    if (node->type == GUMBO_NODE_CDATA) {
        out += QString::fromUtf8(node->v.text.text);
        return;
    }
    if (node->type == GUMBO_NODE_COMMENT) return;
    if (!isElement(node)) return;
    const QString tag = tagName(node).toLower();
    if (tag.isEmpty()) {
        serializeChildren(node, out);
        return;
    }
    out += u'<' + tag;
    const GumboVector& attrs = node->v.element.attributes;
    for (unsigned i = 0; i < attrs.length; ++i) {
        const auto* attr = static_cast<const GumboAttribute*>(attrs.data[i]);
        out += u' ' + QString::fromUtf8(attr->name) + QStringLiteral("=\"") +
               escapeAttr(QString::fromUtf8(attr->value)) + u'"';
    }
    if (isVoidTag(tag)) {
        out += u'>';
        return;
    }
    out += u'>';
    serializeChildren(node, out);
    out += QStringLiteral("</") + tag + u'>';
}

void serializeChildren(const GumboNode* node, QString& out)
{
    if (!node) return;
    if (node->type == GUMBO_NODE_ELEMENT) {
        const GumboVector& kids = node->v.element.children;
        for (unsigned i = 0; i < kids.length; ++i)
            serializeNode(static_cast<const GumboNode*>(kids.data[i]), out);
    } else if (node->type == GUMBO_NODE_DOCUMENT) {
        const GumboVector& kids = node->v.document.children;
        for (unsigned i = 0; i < kids.length; ++i)
            serializeNode(static_cast<const GumboNode*>(kids.data[i]), out);
    }
}

} // namespace

QList<const GumboNode*> selectNodes(const GumboNode* root,
                                    const QString& selector)
{
    QList<const GumboNode*> out;
    if (!root) return out;
    const QString rewritten = rewriteContains(selector);
    for (const QString& group : rewritten.split(u',')) {
        const QString trimmed = group.trimmed();
        if (trimmed.isEmpty()) continue;
        const Selector sel = parseSelectorGroup(trimmed);
        if (sel.compounds.isEmpty()) continue;
        bool valid = true;
        for (const Compound& c : sel.compounds) valid &= c.valid;
        if (!valid) continue;   // unknown pseudo-classes match nothing
        for (const GumboNode* node : matchSelector(root, sel)) {
            if (!out.contains(node)) out.append(node);
        }
    }
    return out;
}

QString nodeText(const GumboNode* node)
{
    if (!node) return {};
    if (node->type == GUMBO_NODE_TEXT ||
        node->type == GUMBO_NODE_WHITESPACE)
        return QString::fromUtf8(node->v.text.text);
    if (!isElement(node) && node->type != GUMBO_NODE_DOCUMENT &&
        node->type != GUMBO_NODE_TEMPLATE)
        return {};
    QStringList bits;
    std::function<void(const GumboNode*)> walk = [&](const GumboNode* n) {
        if (!n) return;
        if (n->type == GUMBO_NODE_TEXT ||
            n->type == GUMBO_NODE_WHITESPACE) {
            bits.append(QString::fromUtf8(n->v.text.text));
            return;
        }
        if (n->type == GUMBO_NODE_ELEMENT) {
            // jsoup skips script/style/data content in text().
            const QString tag = tagName(n).toLower();
            if (tag == QLatin1String("script") ||
                tag == QLatin1String("style"))
                return;
            const GumboVector& kids = n->v.element.children;
            for (unsigned i = 0; i < kids.length; ++i)
                walk(static_cast<const GumboNode*>(kids.data[i]));
        }
    };
    if (node->type == GUMBO_NODE_ELEMENT) {
        walk(node);
    } else {
        const GumboVector& kids = node->v.document.children;
        for (unsigned i = 0; i < kids.length; ++i)
            walk(static_cast<const GumboNode*>(kids.data[i]));
    }
    // jsoup normalizes runs of whitespace to single spaces and trims.
    return bits.join(QStringLiteral(" "))
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
        .join(QStringLiteral(" "));
}

QString nodeInnerHtml(const GumboNode* node)
{
    QString out;
    // jsoup Element.html() is INNER html; Document.html() is the whole
    // document serialization.
    if (node && node->type == GUMBO_NODE_DOCUMENT) {
        serializeChildren(node, out);
        return out;
    }
    serializeChildren(node, out);
    return out;
}

QString nodeAttr(const GumboNode* node, const QString& name)
{
    if (!isElement(node)) return {};
    const GumboVector& attrs = node->v.element.attributes;
    for (unsigned i = 0; i < attrs.length; ++i) {
        const auto* attr = static_cast<const GumboAttribute*>(attrs.data[i]);
        if (QString::fromUtf8(attr->name).compare(name,
                                                  Qt::CaseInsensitive) == 0)
            return QString::fromUtf8(attr->value);
    }
    return {};
}

DomBridge::DomBridge() = default;

DomBridge::~DomBridge() { clear(); }

QString DomBridge::load(const QString& html)
{
    Doc doc;
    doc.source = html.toUtf8();
    doc.output = gumbo_parse_with_options(
        &kGumboDefaultOptions, doc.source.constData(),
        size_t(doc.source.size()));
    const QString id = QStringLiteral("doc_%1_%2")
                           .arg(m_counter++)
                           .arg(QRandomGenerator::system()->generate() &
                                0x7fffffff);
    m_docs.insert(id, std::move(doc));
    return id;
}

QString DomBridge::intern(const GumboNode* node) const
{
    const QString id =
        QStringLiteral("e%1").arg(m_counter++, 0, 36);
    m_elements.insert(id, node);
    return id;
}

const GumboNode* DomBridge::lookup(const QString& id) const
{
    return m_elements.value(id, nullptr);
}

QString DomBridge::select(const QString& docId,
                          const QString& selector) const
{
    const auto it = m_docs.constFind(docId);
    if (it == m_docs.constEnd() || !it->output) return QStringLiteral("[]");
    QStringList ids;
    for (const GumboNode* node :
         selectNodes(it->output->document, selector))
        ids.append(intern(node));
    QJsonArray arr;
    for (const QString& id : ids) arr.append(id);
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString DomBridge::find(const QString& docId, const QString& elementId,
                        const QString& selector) const
{
    const GumboNode* scope = lookup(elementId);
    if (!scope) return QStringLiteral("[]");
    Q_UNUSED(docId);
    QStringList ids;
    for (const GumboNode* node : selectNodes(scope, selector)) {
        if (node != scope) ids.append(intern(node));
    }
    QJsonArray arr;
    for (const QString& id : ids) arr.append(id);
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString DomBridge::text(const QString& idsCsv) const
{
    QStringList bits;
    for (const QString& id :
         idsCsv.split(u',', Qt::SkipEmptyParts)) {
        if (const GumboNode* node = lookup(id.trimmed())) {
            const QString t = nodeText(node);
            if (!t.isEmpty()) bits.append(t);
        }
    }
    return bits.join(u' ');
}

QString DomBridge::html(const QString& docId,
                        const QString& elementId) const
{
    if (elementId.isEmpty()) {
        const auto it = m_docs.constFind(docId);
        if (it == m_docs.constEnd() || !it->output) return {};
        return nodeInnerHtml(it->output->document);
    }
    if (const GumboNode* node = lookup(elementId))
        return nodeInnerHtml(node);
    return {};
}

QString DomBridge::innerHtml(const QString& elementId) const
{
    if (const GumboNode* node = lookup(elementId))
        return nodeInnerHtml(node);
    return {};
}

QString DomBridge::attr(const QString& elementId,
                        const QString& name) const
{
    const GumboNode* node = lookup(elementId);
    if (!node) return QStringLiteral("__UNDEFINED__");
    const QString value = nodeAttr(node, name);
    // Empty-valued attributes read as undefined (fork quirk parity).
    return value.isEmpty() ? QStringLiteral("__UNDEFINED__") : value;
}

QString DomBridge::next(const QString& elementId) const
{
    const GumboNode* node = lookup(elementId);
    if (!node || !node->parent) return QStringLiteral("__NONE__");
    bool after = false;
    const GumboVector* siblings = nullptr;
    if (node->parent->type == GUMBO_NODE_ELEMENT)
        siblings = &node->parent->v.element.children;
    else
        return QStringLiteral("__NONE__");
    for (unsigned i = 0; i < siblings->length; ++i) {
        const auto* kid = static_cast<const GumboNode*>(siblings->data[i]);
        if (kid == node) {
            after = true;
            continue;
        }
        if (after && isElement(kid)) return intern(kid);
    }
    return QStringLiteral("__NONE__");
}

QString DomBridge::prev(const QString& elementId) const
{
    const GumboNode* node = lookup(elementId);
    if (!node || !node->parent) return QStringLiteral("__NONE__");
    if (node->parent->type != GUMBO_NODE_ELEMENT)
        return QStringLiteral("__NONE__");
    const GumboVector& siblings = node->parent->v.element.children;
    const GumboNode* candidate = nullptr;
    for (unsigned i = 0; i < siblings.length; ++i) {
        const auto* kid = static_cast<const GumboNode*>(siblings.data[i]);
        if (kid == node) break;
        if (isElement(kid)) candidate = kid;
    }
    return candidate ? intern(candidate) : QStringLiteral("__NONE__");
}

void DomBridge::bind(
    const std::function<void(const QString& name, JsEngine::NativeFn fn)>&
        addFunction)
{
    // All cheerio args are strings; numbers stringify losslessly here.
    const auto str = [](const QVariantList& a, int i) {
        return a.value(i).toString();
    };
    addFunction(QStringLiteral("__cheerio_load"),
                [this, str](const QVariantList& a) -> QVariant {
                    return load(str(a, 0));
                });
    addFunction(QStringLiteral("__cheerio_select"),
                [this, str](const QVariantList& a) -> QVariant {
                    return select(str(a, 0), str(a, 1));
                });
    addFunction(QStringLiteral("__cheerio_find"),
                [this, str](const QVariantList& a) -> QVariant {
                    return find(str(a, 0), str(a, 1), str(a, 2));
                });
    addFunction(QStringLiteral("__cheerio_text"),
                [this, str](const QVariantList& a) -> QVariant {
                    return text(str(a, 1));
                });
    addFunction(QStringLiteral("__cheerio_html"),
                [this, str](const QVariantList& a) -> QVariant {
                    return html(str(a, 0), str(a, 1));
                });
    addFunction(QStringLiteral("__cheerio_inner_html"),
                [this, str](const QVariantList& a) -> QVariant {
                    return innerHtml(str(a, 1));
                });
    addFunction(QStringLiteral("__cheerio_attr"),
                [this, str](const QVariantList& a) -> QVariant {
                    return attr(str(a, 1), str(a, 2));
                });
    addFunction(QStringLiteral("__cheerio_next"),
                [this, str](const QVariantList& a) -> QVariant {
                    return next(str(a, 1));
                });
    addFunction(QStringLiteral("__cheerio_prev"),
                [this, str](const QVariantList& a) -> QVariant {
                    return prev(str(a, 1));
                });
}

void DomBridge::clear()
{
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        if (it->output) gumbo_destroy_output(&kGumboDefaultOptions,
                                             it->output);
    }
    m_docs.clear();
    m_elements.clear();
}

} // namespace nuvio::plugins
