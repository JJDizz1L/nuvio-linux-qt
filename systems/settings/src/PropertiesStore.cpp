#include "nuvio/settings/PropertiesStore.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace nuvio::settings {
namespace {

// ---- shared low-level helpers ----------------------------------------------

void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int hexVal(const char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ---- load-side decode (per string; local surrogate state) -------------------

struct DecodeState {
    unsigned hi = 0; // pending high surrogate
};

void emitCp(std::string& out, DecodeState& st, unsigned cp)
{
    if (cp >= 0xD800 && cp <= 0xDBFF) { st.hi = cp; return; }
    if (cp >= 0xDC00 && cp <= 0xDFFF && st.hi != 0) {
        cp = 0x10000u + ((st.hi - 0xD800u) << 10) + (cp - 0xDC00u);
        st.hi = 0;
    } else {
        st.hi = 0;
    }
    appendUtf8(out, cp);
}

/// java.util.Properties.loadConvert for one logical string (key or value).
/// Handles \t \n \r \f \\ \uXXXX (with UTF-16 surrogate pairing); any other
/// escaped char decodes to itself.
std::string decodeStr(const std::string& s)
{
    std::string out;
    DecodeState st;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        if (s[i] != '\\' || i + 1 >= n) {
            out += s[i];
            ++i;
            continue;
        }
        const char e = s[++i];
        switch (e) {
        case 't': emitCp(out, st, '\t'); ++i; break;
        case 'n': emitCp(out, st, '\n'); ++i; break;
        case 'r': emitCp(out, st, '\r'); ++i; break;
        case 'f': emitCp(out, st, '\f'); ++i; break;
        case 'u': {
            if (i + 4 >= n) { ++i; continue; }
            unsigned cp = 0;
            bool bad = false;
            for (int k = 1; k <= 4; ++k) {
                const int d = hexVal(s[i + k]);
                if (d < 0) { bad = true; break; }
                cp = cp * 16u + static_cast<unsigned>(d);
            }
            if (bad) { ++i; continue; }
            i += 5;
            emitCp(out, st, cp);
            break;
        }
        default: emitCp(out, st, static_cast<unsigned char>(e)); ++i; break;
        }
    }
    return out;
}

// ---- save-side encode --------------------------------------------------------

void escHex4(std::string& out, const unsigned unit)
{
    char b[8]{};
    std::snprintf(b, sizeof b, "\\u%04x", unit & 0xFFFFu);
    out += b;
}

/// UTF-16-style emission (astral planes arrive as surrogate pairs, matching
/// how Java Properties.store() writes them).
void escUnicode(std::string& out, unsigned cp)
{
    if (cp > 0xFFFF) {
        const unsigned v = cp - 0x10000u;
        escHex4(out, 0xD800u | (v >> 10));
        escHex4(out, 0xDC00u | (v & 0x3FFu));
    } else {
        escHex4(out, cp);
    }
}

/// One UTF-8 code point starting at s[i] -> cp; advances i. Lenient on
/// malformed sequences (lead byte falls through as Latin-1 would).
unsigned nextCp(const std::string& s, size_t& i)
{
    const auto b0 = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    unsigned cp = b0;
    if (b0 >= 0xF0 && i + 3 < s.size()) { cp = b0 & 7u; len = 4; }
    else if (b0 >= 0xE0 && i + 2 < s.size()) { cp = b0 & 15u; len = 3; }
    else if (b0 >= 0xC0 && i + 1 < s.size()) { cp = b0 & 31u; len = 2; }
    bool ok = true;
    for (size_t k = 1; k < len; ++k) {
        const auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { ok = false; break; }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    i += len;
    return ok ? cp : b0;
}

/// java.util.Properties.saveConvert. Keys additionally escape ALL spaces;
/// values escape only a LEADING space.
std::string encodeStr(const std::string& s, const bool isKey)
{
    std::string out;
    out.reserve(s.size() * 2);
    bool firstCp = true;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned cp = nextCp(s, i);
        bool handled = true;
        switch (cp) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\f': out += "\\f"; break;
        case ' ':
            if (isKey || firstCp) out += '\\';
            out += ' ';
            break;
        case '=': [[fallthrough]];
        case ':': [[fallthrough]];
        case '#': [[fallthrough]];
        case '!':
            out += '\\';
            out += static_cast<char>(cp);
            break;
        default: handled = false; break;
        }
        if (!handled) {
            if ((cp >= 0xD800 && cp <= 0xDFFF) || cp < 32 || cp > 126)
                escUnicode(out, cp);
            else
                out += static_cast<char>(cp);
        }
        firstCp = false;
    }
    return out;
}

std::string nowIsoUtc()
{
    const auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[40]{};
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // anonymous namespace


PropertiesCodec::Map PropertiesCodec::parse(const std::string& raw)
{
    Map out;

    // 1) physical -> logical lines: odd trailing backslashes join the next one
    std::vector<std::string> lines;
    {
        std::istringstream in(raw);
        std::string phys, cur;
        while (std::getline(in, phys)) {
            if (!phys.empty() && phys.back() == '\r') phys.pop_back();
            size_t bs = 0;
            while (bs < phys.size() && phys[phys.size() - 1 - bs] == '\\')
                ++bs;
            const bool cont = (bs % 2) == 1;
            cur.append(phys, 0, cont ? phys.size() - 1 : phys.size());
            if (cont) continue;
            lines.push_back(cur);
            cur.clear();
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    // 2) logical line -> (keyRaw, valRaw), java.util.Properties split rules
    for (const auto& lg : lines) {
        size_t i = 0;
        while (i < lg.size() &&
               (lg[i] == ' ' || lg[i] == '\t' || lg[i] == '\f'))
            ++i;
        if (i >= lg.size() || lg[i] == '#' || lg[i] == '!') continue;

        std::string keyRaw;
        bool haveSep = false;
        size_t afterSep = lg.size();
        while (i < lg.size()) {
            const char c = lg[i];
            if (c == '\\' && i + 1 < lg.size()) {
                keyRaw += c;
                keyRaw += lg[i + 1];
                i += 2;
                continue;
            }
            if (c == '=' || c == ':') {
                haveSep = true;
                afterSep = i + 1;
                break;
            }
            if (c == ' ' || c == '\t' || c == '\f') {
                size_t j = i;
                while (j < lg.size() && (lg[j] == ' ' || lg[j] == '\t' ||
                                         lg[j] == '\f'))
                    ++j;
                if (j < lg.size() && (lg[j] == '=' || lg[j] == ':')) {
                    haveSep = true;
                    afterSep = j + 1;
                } else {
                    afterSep = j; // bare-ws split: value runs to EOL
                }
                break;
            }
            keyRaw += c;
            ++i;
        }
        if (keyRaw.empty()) continue;

        std::string valRaw;
        if (haveSep) {
            size_t v = afterSep;
            while (v < lg.size() && (lg[v] == ' ' || lg[v] == '\t' ||
                                     lg[v] == '\f'))
                ++v; // Java skips ws AFTER an explicit separator only
            valRaw = lg.substr(v);
        } else {
            valRaw = lg.substr(afterSep);
        }
        out.emplace(decodeStr(keyRaw), decodeStr(valRaw));
    }
    return out;
}

std::string PropertiesCodec::serialize(
    const Map& props,
    const std::string& comment,
    const std::string& dateLine)
{
    std::ostringstream o;
    o << '#' << comment << '\n'
      << '#' << (dateLine.empty() ? nowIsoUtc() : dateLine) << '\n';
    for (const auto& kv : props)
        o << encodeStr(kv.first, true) << '='
          << encodeStr(kv.second, false) << '\n';
    return o.str();
}


PropertiesStore::PropertiesStore(std::filesystem::path file)
    : m_file(std::move(file))
{
}

std::filesystem::path PropertiesStore::defaultPath(std::string_view name)
{
    namespace fs = std::filesystem;
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const fs::path base = (cfg && *cfg && cfg[0] == '/')
                              ? fs::path(cfg)
                              : fs::path(home != nullptr ? home : "") / ".config";
    return base / "nuvio-linux" / (std::string(name) + ".properties");
}

void PropertiesStore::ensureLoaded()
{
    if (m_loaded) return;
    m_loaded = true;
    std::ifstream f(m_file, std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    m_props = PropertiesCodec::parse(ss.str());
}

bool PropertiesStore::contains(const std::string_view key)
{
    ensureLoaded();
    return m_props.find(key) != m_props.end();
}

std::optional<std::string> PropertiesStore::getString(const std::string_view key)
{
    ensureLoaded();
    const auto it = m_props.find(key);
    if (it == m_props.end()) return std::nullopt;
    return it->second;
}

void PropertiesStore::putString(std::string_view key,
                                const std::optional<std::string>& value)
{
    ensureLoaded();
    if (value)
        m_props.insert_or_assign(std::string(key), *value);
    else
        m_props.erase(std::string(key));
    persist();
}

std::optional<bool> PropertiesStore::getBoolean(const std::string_view key)
{
    const auto s = getString(key);
    if (!s) return std::nullopt;
    if (*s == "true") return true;
    if (*s == "false") return false;
    return std::nullopt;
}

void PropertiesStore::putBoolean(std::string_view key, const bool value)
{
    putString(key, value ? "true" : "false");
}

std::optional<int32_t> PropertiesStore::getInt(const std::string_view key)
{
    const auto s = getString(key);
    if (!s) return std::nullopt;
    try {
        size_t used = 0;
        const long long v = std::stoll(*s, &used);
        if (used == s->size()) return static_cast<int32_t>(v);
    } catch (...) {
    }
    return std::nullopt;
}

void PropertiesStore::putInt(std::string_view key, const int32_t value)
{
    putString(key, std::to_string(value));
}

std::optional<float> PropertiesStore::getFloat(const std::string_view key)
{
    const auto s = getString(key);
    if (!s) return std::nullopt;
    try {
        size_t used = 0;
        const float v = std::stof(*s, &used);
        if (used == s->size()) return v;
    } catch (...) {
    }
    return std::nullopt;
}

void PropertiesStore::putFloat(std::string_view key, const float value)
{
    // Java's Float.toString produces the shortest repr that round-trips; the
    // C++ ostream default matches it for all typical preference values.
    std::ostringstream o;
    o << value;
    putString(key, o.str());
}

// ---- Set<String>: kotlinx-style JSON array riding in one String value -------

namespace {

// Defined in the unnamed-namespace block above; same unnamed namespace, so
// this declaration binds to the existing definition.
void escHex4(std::string& out, unsigned unit);

std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (const char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 32) {
                escHex4(o, static_cast<unsigned char>(c));
            } else {
                o += c;
            }
        }
    }
    return o;
}

bool jsonSkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    return i < s.size();
}

} // namespace

std::optional<std::vector<std::string>> PropertiesStore::getStringSet(
    const std::string_view key)
{
    const auto s = getString(key);
    if (!s) return std::nullopt;

    std::vector<std::string> out;
    size_t i = 0;
    if (!jsonSkipWs(*s, i) || (*s)[i] != '[') return std::nullopt;
    ++i;
    if (!jsonSkipWs(*s, i)) return std::nullopt;
    if ((*s)[i] == ']') return out; // empty set

    while (true) {
        if (!jsonSkipWs(*s, i) || (*s)[i] != '"') return std::nullopt;
        ++i;
        std::string item;
        bool closed = false;
        while (i < s->size()) {
            const char c = (*s)[i];
            if (c == '\\' && i + 1 < s->size()) {
                const char e = (*s)[i + 1];
                switch (e) {
                case '"': item += '"'; break;
                case '\\': item += '\\'; break;
                case '/': item += '/'; break;
                case 'n': item += '\n'; break;
                case 't': item += '\t'; break;
                case 'r': item += '\r'; break;
                default: item += e; break;
                }
                i += 2;
                continue;
            }
            if (c == '"') { closed = true; ++i; break; }
            item += c;
            ++i;
        }
        if (!closed) return std::nullopt;
        out.push_back(std::move(item));
        if (!jsonSkipWs(*s, i)) return std::nullopt;
        if ((*s)[i] == ',') { ++i; continue; }
        if ((*s)[i] == ']') return out;
        return std::nullopt;
    }
}

void PropertiesStore::putStringSet(std::string_view key,
                                   const std::vector<std::string>& values)
{
    std::string j = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) j += ',';
        j += '"';
        j += jsonEscape(values[i]);
        j += '"';
    }
    j += "]";
    putString(key, j);
}

void PropertiesStore::remove(std::string_view key)
{
    ensureLoaded();
    m_props.erase(std::string(key));
    persist();
}

void PropertiesStore::persist()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);

    const std::string data = PropertiesCodec::serialize(m_props);
    const fs::path tmp =
        m_file.parent_path() / (m_file.filename().string() + ".part");

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    ::chmod(tmp.c_str(), 0600);
    fs::rename(tmp, m_file, ec); // POSIX rename replaces atomically
    ::chmod(m_file.c_str(), 0600);
}

} // namespace nuvio::settings