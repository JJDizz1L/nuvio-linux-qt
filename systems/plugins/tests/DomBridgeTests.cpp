// Offline contract for the DOM bridge: selector matching, text,
// attribute, html, sibling traversal. No network, no JS engine.
#include <nuvio/plugins/DomBridge.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::plugins::DomBridge;
using nuvio::plugins::nodeAttr;
using nuvio::plugins::nodeText;
using nuvio::plugins::selectNodes;

static QStringList idsOf(const QString& json)
{
    QStringList out;
    for (const QJsonValue& v :
         QJsonDocument::fromJson(json.toUtf8()).array())
        out.append(v.toString());
    return out;
}

static const char* kHtml = R"HTML(
<html><head><title>T</title></head>
<body>
<div id="main" class="wrap wide">
  <h1 class="title">Hello <span>World</span></h1>
  <ul class="links">
    <li><a href="https://a.example/x" data-k="1">Alpha</a></li>
    <li><a href="https://b.example/y">Beta</a></li>
    <li><a href="/rel" data-empty="">Gamma</a></li>
  </ul>
  <p class="note">First</p><p class="note">Second</p>
  <div class="nested"><p>Deep <b>bold</b> text</p></div>
  <script>var hidden = 1;</script>
</div>
</body></html>
)HTML";

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    DomBridge dom;
    const QString doc = dom.load(QString::fromLatin1(kHtml));
    CHECK(!doc.isEmpty(), "load returns a doc id");

    { // selectors
        CHECK(idsOf(dom.select(doc, "h1")).size() == 1, "tag");
        CHECK(idsOf(dom.select(doc, ".note")).size() == 2, "class");
        CHECK(idsOf(dom.select(doc, "#main")).size() == 1, "id");
        CHECK(idsOf(dom.select(doc, "div.wrap")).size() == 1,
              "tag+class");
        CHECK(idsOf(dom.select(doc, "ul.links a")).size() == 3,
              "descendant");
        CHECK(idsOf(dom.select(doc, "ul > li")).size() == 3,
              "child combinator");
        CHECK(idsOf(dom.select(doc, "div > p")).size() == 3,
              "child across nesting");
        CHECK(idsOf(dom.select(doc, "h1, p.note")).size() == 3,
              "comma group");
        CHECK(idsOf(dom.select(doc, "a[href]")).size() == 3,
              "attr presence");
        CHECK(idsOf(dom.select(doc, "a[data-k=\"1\"]")).size() == 1,
              "attr exact value");
        CHECK(idsOf(dom.select(doc, "a[href^=\"https://a\"]")).size() == 1,
              "attr prefix match");
        CHECK(idsOf(dom.select(doc, "h1:contains(\"Hello\")")).size() == 1,
              "contains rewrite");
        CHECK(idsOf(dom.select(doc, "h1:contains(hello)")).size() == 1,
              "contains case-insensitive");
        CHECK(idsOf(dom.select(doc, "nope")).isEmpty(), "no match empty");
        CHECK(dom.select(doc, "div:unknown-pseudo()") == "[]",
              "unknown pseudo matches nothing");
        CHECK(dom.select("bogus-doc", "div") == "[]",
              "unknown doc matches nothing");
    }

    { // text / attr / html
        const QStringList h1 = idsOf(dom.select(doc, "h1.title"));
        CHECK(h1.size() == 1, "h1 located");
        CHECK(dom.text(h1.join(",")) == "Hello World", "text flattens");
        const QStringList deep =
            idsOf(dom.select(doc, "div.nested p"));
        CHECK(dom.text(deep.join(",")) == "Deep bold text",
              "nested text flattens");
        const QStringList links = idsOf(dom.select(doc, "ul.links a"));
        CHECK(links.size() == 3, "links located");
        CHECK(dom.attr(links[0], "href") == "https://a.example/x",
              "attr reads");
        CHECK(dom.attr(links[0], "HREF") == "https://a.example/x",
              "attr case-insensitive");
        CHECK(dom.attr(links[0], "missing") == "__UNDEFINED__",
              "missing attr undefined");
        CHECK(dom.attr(links[2], "data-empty") == "__UNDEFINED__",
              "empty attr reads undefined (fork quirk)");
        CHECK(dom.attr("bogus", "href") == "__UNDEFINED__",
              "bogus id undefined");
        CHECK(dom.innerHtml(deep[0]).contains("<b>bold</b>"),
              "inner html serializes");
        CHECK(dom.html(doc, "").contains("<h1"), "doc html serializes");
        CHECK(dom.html(doc, deep[0]).contains("Deep"),
              "element html is inner");
    }

    { // find + siblings
        const QStringList main = idsOf(dom.select(doc, "#main"));
        const QStringList found =
            idsOf(dom.find(doc, main[0], "p.note"));
        CHECK(found.size() == 2, "find scopes to the element");
        const QStringList first =
            idsOf(dom.find(doc, main[0], "ul.links"));
        CHECK(first.size() == 1, "find single");
        CHECK(dom.find(doc, "bogus", "p") == "[]", "find bogus empty");
        const QStringList notes = idsOf(dom.select(doc, "p.note"));
        CHECK(dom.text(dom.next(notes[0])) == "Second", "next sibling");
        CHECK(dom.text(dom.prev(notes[1])) == "First", "prev sibling");
        // Element ids are minted fresh per call: compare by content.
        CHECK(dom.text(dom.prev(notes[0])) == "Alpha Beta Gamma",
              "prev crosses to ul");
        CHECK(dom.next("bogus") == "__NONE__", "next bogus none");
    }

    std::printf(failures ? "DOM SUITE FAILURES=%d\n"
                         : "DOM SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
