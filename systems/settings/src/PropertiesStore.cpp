#include "nuvio/settings/PropertiesStore.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace nuvio::settings {

inline bool isSpaceCh(char c) { return c == ' ' || c == '\t' || c == '\f'; }

static void appendUtf8(std::string& out, unsigned int cp)
{
    if (cp < 0x80) { out += char(cp); return; }
    if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

static unsigned int pendHi = 0;

static void flushCp(std::string& out, unsigned int cp)
{
    if (cp >= 0xD800 && cp <= 0xDBFF) { pendHi = cp; return; }
    if (cp >= 0xDC00 && cp <= 0xDFFF && pendHi) {
        cp = 0x10000 + ((pendHi - 0xD800) << 10) + (cp - 0xDC00);
        pendHi = 0;
    } else pendHi = 0;
    appendUtf8(out, cp);
}

static int hexDigit(char h)
{
    return (h >= '0' && h <= '9') ? h - '0'
         : (h >= 'a' && h <= 'f') ? h - 'a' + 10
         : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
}

static std::string decodeOne(const std::string& s, size_t& i)
{
    const char e = s[++i];
    std::string out;
    switch (e) {
    case 't': out += '\t'; break;
    case 'n': out += '\n'; break;
    case 'r': out += '\r'; break;
    case 'f': out += '\f'; break;
    case 'u': {
        bool ok = i + 4 < s.size();
        unsigned cp = 0;
        for (int k = 1; ok && k <= 4; ++k) {
            const int d = hexDigit(s[i + k]);
            if (d < 0) ok = false; else cp = cp * 16 + unsigned(d);
        }
        if (!ok) { out += 'u'; break; }
        i += 4;
        flushCp(out, cp);
        break;
    }
    default: out += e; break;
    }
    return out;
}

static std::string decodeEscapes(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '\\' && i + 1 < s.size()) out += decodeOne(s, i);
        else { out += s[i]; ++i; }
    }
    return out;
}

static std::string saveConvert(const std::string& s, bool isKey)
{
    unsigned hi = 0;
    std::string out;
    out.reserve(s.size() * 2);
    struct EscHelper {
        std::string* o;
        void operator()(unsigned v) const {
            char b[8]{};
            std::snprintf(b, sizeof b, "\\u%04x", v & 0xFFFF);
            *o += b;
        }
    };
    EscHelper escU{&out};
    for (size_t i = 0; i < s.size(); ) {
        const auto b0 = unsigned char(s[i]);
        unsigned cp = b0; size_t len = 1;
        if (b0 >= 0xF0 && i + 3 < s.size()) { cp = b0 & 7u; len = 4; }
        else if (b0 >= 0xE0 && i + 2 < s.size()) { cp = b0 & 15u; len = 3; }
        else if (b0 >= 0xC0 && i + 1 < s.size()) { cp = b0 & 31u; len = 2; }
        bool bad = len > 1;
        for (size_t k = 1; k < len; ++k) {
            const auto cc = unsigned char(s[i+k]);
            if ((cc & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (bad) cp = b0;
        i += len;
        if (hi) {
            escU(hi); escU(cp); hi = 0; continue;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) { hi = cp; continue; }
        switch (cp) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\f': out += "\\f"; break;
        case ' ': out += isKey ? "\\ " : (out.empty() || out.back()=='\\' ? "\\ " : " "); break;
        case '=': [[fallthrough]];
        case ':': [[fallthrough]];
        case '#': [[fallthrough]];
        case '!': out += '\\'; appendUtf8(out, cp); break;
        default:
            if (cp < 0x20 || cp > 0x7E) escU(cp); else out += char(cp);
        }
    }
    return out;
}


// ---- PropertiesCodec -------------------------------------------------------

PropertiesCodec::Map PropertiesCodec::parse(const std::string& raw)
{
    Map out;
    std::vector<std::string> logicals;
    std::istringstream in(raw);
    std::string phys, cur;
    while (std::getline(in, phys)) {
        if (!phys.empty() && phys.back() == '\r') phys.pop_back();
        size_t bs = 0;
        while (bs < phys.size() && phys[phys.size()-1-bs] == '\\') ++bs;
        const bool cont = (bs % 2) == 1;
        cur += cont ? phys.substr(0, phys.size()-1) : phys;
        if (cont) continue;
        size_t k = 0;
        while (k < cur.size() && isSpaceCh(cur[k])) ++k;
        logicals.push_back(cur.substr(k));
        cur.clear();
    }
    if (!cur.empty()) logicals.push_back(cur);

    for (const auto& lg : logicals) {
        size_t i = 0;
        while (i < lg.size() && isSpaceCh(lg[i])) ++i;
        if (i >= lg.size() || lg[i] == '#' || lg[i] == '!') continue;
        const size_t keyStart = i;
        size_t sep = std::string::npos;
        while (i < lg.size()) {
            const char c = lg[i];
            if (c == '\\') { i += 2; continue; }
            if (c == '=' || c == ':') { sep = i; break; }
            if (isSpaceCh(c)) {
                size_t j = i;
                while (j < lg.size() && isSpaceCh(lg[j])) ++j;
                if (j < lg.size() && (lg[j] == '=' || lg[j] == ':')) sep = j;
                break;
            }
            ++i;
        }
        if (sep == std::string::npos || sep <= keyStart) continue;
        std::string k = decodeEscapes(lg.substr(keyStart, sep - keyStart));
        std::string v = decodeEscapes(lg.substr(sep + 1));
        out.emplace(std::move(k), std::move(v));
    }
    return out;
}

std::string PropertiesCodec::serialize(const Map& props,
                                       const std::string& comment,
                                       const std::string& dateLine)
{
    std::ostringstream o;
    o << '#' << comment << '\n'
      << (dateLine.empty() ? nowDateLine() : dateLine) << '\n';
    for (const auto& [k, v] : props)
        o << saveConvert(k, true) << '=' << saveConvert(v, false) << '\n';
    return o.str();
}

// ---- PropertiesStore -------------------------------------------------------

PropertiesStore::PropertiesStore(std::filesystem::path f) : m_file(std::move(f)) {}

std::filesystem::path PropertiesStore::defaultPath(std::string_view name)
{
    namespace fs = std::filesystem;
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = (cfg && *cfg && cfg[0]=='/')
        ? fs::path(cfg)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".config";
    return base / "nuvio-linux" / (std::string(name) + ".properties");
}

void PropertiesStore::ensureLoaded()
{
    if (m_loaded) return;
    m_loaded = true;
    std::ifstream f(m_file, std::ios::binary);
    if (!f) return;
    std::ostringstream ss; ss << f.rdbuf();
    m_props = PropertiesCodec::parse(ss.str());
}

bool PropertiesStore::contains(std::string_view k)
{ ensureLoaded(); return m_props.count(std::string(k)) > 0; }

std::optional<std::string> PropertiesStore::getString(std::string_view k)
{
    ensureLoaded();
    auto it = m_props.find(std::string(k));
    return it == m_props.end() ? std::optional<std::string>{} : it->second;
}

void PropertiesStore::putString(std::string_view k,
                                const std::optional<std::string>& value)
{
    ensureLoaded();
    if (value) m_props.insert_or_assign(std::string(k), *value);
    else m_props.erase(std::string(k));
    persist();
}

std::optional<bool> PropertiesStore::getBoolean(std::string_view k)
{
    auto s = getString(k);
    if (!s) return std::nullopt;
    if (*s == "true") return true;
    if (*s == "false") return false;
    return std::nullopt;
}

void PropertiesStore::putBoolean(std::string_view k, bool v)
{ putString(k, v ? "true" : "false"); }

std::optional<int32_t> PropertiesStore::getInt(std::string_view k)
{
    auto s = getString(k);
    if (!s) return std::nullopt;
    try { size_t u=0; long long v=std::stoll(*s,&u); if(u==s->size()) return int32_t(v); }
    catch(...) {}
    return std::nullopt;
}

void PropertiesStore::putInt(std::string_view k, int32_t v)
{ putString(k, std::to_string(v)); }

std::optional<float> PropertiesStore::getFloat(std::string_view k)
{
    auto s = getString(k);
    if (!s) return std::nullopt;
    try { size_t u=0; float v=std::stof(*s,&u); if(u==s->size()) return v; }
    catch(...) {}
    return std::nullopt;
}

void PropertiesStore::putFloat(std::string_view k, float v)
{ std::ostringstream o; o<<v; putString(k,o.str()); }

std::optional<std::vector<std::string>>
PropertiesStore::getStringSet(std::string_view) { return std::nullopt; }

void PropertiesStore::putStringSet(std::string_view,
                                   const std::vector<std::string>&) {}

void PropertiesStore::remove(std::string_view k)
{
    ensureLoaded();
    m_props.erase(std::string(k));
    persist();
}

void PropertiesStore::persist()
{
    std::error_code ec;
    std::filesystem::create_directories(m_file.parent_path(), ec);
    const std::string data = PropertiesCodec::serialize(m_props);
    const auto tmp = m_file.parent_path() /
                     (m_file.filename().string()+".part");
    { std::ofstream f(tmp, std::ios::binary|std::ios::trunc);
      f.write(data.data(), std::streamsize(data.size())); }
    ::chmod(tmp.c_str(), 0600);
    std::filesystem::rename(tmp, m_file, ec);
    if (ec) { std::filesystem::remove(m_file, ec);
              std::filesystem::rename(tmp, m_file); }
    ::chmod(m_file.c_str(), 0600);
}

} // namespace nuvio::settings
