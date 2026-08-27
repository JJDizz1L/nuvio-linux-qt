#pragma once

// Byte-compatible re-implementation of the Compose line's DesktopStorage
// format: java.util.Properties files ("<store>.properties") under
// <configHome>/nuvio-linux (+ .stateDir variants), private 0600 perms,
// atomic tmp-rename persistence. Spec notes live in nuvio-linux-qt.md §P1.
//
// Known divergence (documented): our writes emit keys sorted; Java order is
// hash-arbitrary. Files remain mutually readable — ordering was never part
// of the contract.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>

namespace nuvio::settings {

/// Pure codec helpers (unit-tested directly).
class PropertiesCodec {
public:
    using Map = std::map<std::string, std::string, std::less<>>;

    /// java.util.Properties.load()-compatible parser (continuations,
    /// comments #/!, key separators '='/':'/whitespace, loadConvert escapes
    /// incl. \\uXXXX with surrogate pairing).
    [[nodiscard]] static Map parse(const std::string& raw);

    /// properties.store()-compatible serializer: '#'-comment header line +
    /// '#'+date line + sorted entries. Date ISO-8601 UTC to stay deterministic.
    [[nodiscard]] static std::string serialize(
        const Map& props,
        const std::string& comment = "Nuvio Linux desktop preferences",
        const std::string& dateLine = {});
};

class PropertiesStore {
public:
    explicit PropertiesStore(std::filesystem::path file);

    /// <configHome>/nuvio-linux/<name>.properties (matches DesktopStorage.store()).
    [[nodiscard]] static std::filesystem::path defaultPath(std::string_view name);

    bool contains(std::string_view key);
    std::optional<std::string> getString(std::string_view key);
    void putString(std::string_view key, const std::optional<std::string>& value);

    std::optional<bool> getBoolean(std::string_view key);
    void putBoolean(std::string_view key, bool value);

    std::optional<int32_t> getInt(std::string_view key);
    void putInt(std::string_view key, int32_t value);

    std::optional<float> getFloat(std::string_view key);
    void putFloat(std::string_view key, float value);

    /// Set<String> rides as a kotlinx-style JSON array String value.
    std::optional<std::vector<std::string>> getStringSet(std::string_view key);
    void putStringSet(std::string_view key,
                      const std::vector<std::string>& values);

    void remove(std::string_view key);

    /// Atomic temp-file + rename with 0600 perms (mkdir -p on parents).
    void persist();

private:
    void ensureLoaded();

    std::filesystem::path m_file;
    PropertiesCodec::Map m_props;
    bool m_loaded = false;
};

} // namespace nuvio::settings
