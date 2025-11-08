#include "renderConfigFile.h"
#include "helper.h"
#include <fstream>
#include <utility>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>

/** Trim only spaces/CR/LF/ASCII>=128 at string edges using helper::trimEnds. */
static inline void trimEndsSpacesLike(std::string& s) {
    helper::trimEnds(s);
}

/** Returns true if the line is a section header like: [SectionName] */
static inline bool isSectionHeader(const std::string& s) noexcept {
    return s.size() >= 2 && s.front() == '[' && s.back() == ']';
}

/** Extracts the section name from a header line [name]; returns trimmed name. */
static std::string sectionFromLine(std::string s) {
    if (s.size() >= 2) s = s.substr(1, s.size() - 2);
    trimEndsSpacesLike(s);
    return s;
}

/** Splits 'line' at the first '=' into name and value parts (both trimmed). */
static bool splitNameValue(const std::string& line, std::string& nameOut, std::string& valueOut) {
    const auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    std::string name = line.substr(0, pos);
    std::string value = (pos + 1 < line.size()) ? line.substr(pos + 1) : std::string{};
    trimEndsSpacesLike(name);
    trimEndsSpacesLike(value);
    nameOut = std::move(name);
    valueOut = std::move(value);
    return !nameOut.empty();
}

/** Helper: create timestamped backup copy of an existing file (Linux only). */
static void createBackupIfExists(const std::string& filename) {
    std::ifstream src(filename, std::ios::binary);
    if (!src) return; // nothing to backup
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << filename << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".bak";
    std::ofstream dst(oss.str(), std::ios::binary);
    if (!dst) return;
    dst << src.rdbuf();
}

/** Constructor: optionally backup first, then open file, read line by line, trim, and store. Then parse. */
renderConfigFile::renderConfigFile(const std::string& filename, bool backupOnConstruct)
    : m_filename(filename) {
    if (backupOnConstruct) {
        createBackupIfExists(filename);
    }
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) {
        loaded_ = false; // Datei fehlt / nicht lesbar => fatal laut Vorgabe
        return;
    }
    loaded_ = true;

    std::string line;
    while (std::getline(in, line)) {
        helper::trimEnds(line);
        lines.push_back(std::move(line));
    }
    parser();
}

/** Re-parse lines into entries_. Trims spaces around '=', keeps quotes in value. */
void renderConfigFile::parser() {
    entries_.clear();
    std::string currentSection;
    entries_.reserve(lines.size());

    for (const auto& raw : lines) {
        if (raw.empty()) continue;
        if (isSectionHeader(raw)) { currentSection = sectionFromLine(raw); continue; }

        std::string name, value;
        if (!splitNameValue(raw, name, value)) continue;

        entries_.push_back(Entry{currentSection, std::move(name), std::move(value)});
    }
}

/** Returns parsed entries snapshot. */
const std::vector<renderConfigFile::Entry>& renderConfigFile::getEntries() const noexcept {
    return entries_;
}

/** Finds the first matching entry by section+name and returns the value with surrounding quotes removed. */ 
std::string renderConfigFile::findValue(const std::string& section, const std::string& name) const { 
    for (const auto& e : entries_) { 
        if (e.section == section && e.name == name) { 
            const std::string& v = e.value; 
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') { 
                return v.substr(1, v.size() - 2); 
            } 
            return v; 
        } 
    } 
    return {}; 
}

/** Sets or inserts name=value in flat scope or within a section (creates section if missing). */
/** Returns the logical value: without surrounding quotes if present. */
static inline std::string logicalValue(const std::string& v) {
    return (v.size() >= 2 && v.front() == '"' && v.back() == '"') ? v.substr(1, v.size() - 2) : v;
}

/** Sets or inserts name=value; returns true on actual change/insert, false if value already identical. */
bool renderConfigFile::setValue(const std::string& name, const std::string& value, const std::string& section) {
    auto applyQuoting = [&](const std::string& v, bool quote) -> std::string {
        return quote ? std::string("\"") + v + "\"" : v;
    };

    std::string currentSection;
    const bool targetFlat = section.empty();

    // Pass 1: try to replace an existing occurrence in the target scope.
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& raw = lines[i];
        if (raw.empty()) continue;
        if (isSectionHeader(raw)) { currentSection = sectionFromLine(raw); continue; }

        if ((targetFlat && currentSection.empty()) || (!targetFlat && currentSection == section)) {
            std::string n, v;
            if (!splitNameValue(raw, n, v)) continue;
            if (n == name) {
                const bool wasQuoted = (v.size() >= 2 && v.front() == '"' && v.back() == '"');
                const std::string oldLogical = logicalValue(v);
                if (oldLogical == value) return false; // nothing to do

                lines[i] = name + "=" + applyQuoting(value, wasQuoted);
                parser();
                return true;
            }
        }
    }

    // Not found: insert (no quotes added for new keys).
    const std::string normalized = name + "=" + value;

    if (targetFlat) {
        // Insert before the first section header if present, else append.
        size_t insertPos = lines.size();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (isSectionHeader(lines[i])) { insertPos = i; break; }
        }
        lines.insert(lines.begin() + static_cast<long>(insertPos), normalized);
        parser();
        return true;
    }

    // Sectioned insert: locate or create the section.
    size_t secHeader = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isSectionHeader(lines[i]) && sectionFromLine(lines[i]) == section) { 
            secHeader = i; 
            break; 
        }
    }

    if (secHeader == lines.size()) {
        // Section does not exist: append it and then the key.
        if (!lines.empty() && !lines.back().empty()) lines.push_back(std::string{});
        lines.push_back("[" + section + "]");
        lines.push_back(normalized);
        parser();
        return true;
    }

    // Insert after the section header, before the next section or at EOF.
    size_t insertPos = secHeader + 1;
    while (insertPos < lines.size() && !isSectionHeader(lines[insertPos])) ++insertPos;
    lines.insert(lines.begin() + static_cast<long>(insertPos), normalized);
    parser();
    return true;
}

/** Writes the parsed entries back to the original file (no backup handling). */
bool renderConfigFile::saveConfigFile() const {
    std::ofstream out(m_filename, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[renderConfigFile] cannot open for write: %s\n", m_filename.c_str());
        return false;
    }

    // 1) Flat entries
    bool firstFlat = true;
    for (const auto& e : entries_) {
        if (!e.section.empty()) continue;
        out << e.name << "=" << e.value << "\n";
        firstFlat = false;
    }

    // 2) Sectioned entries
    bool needBlank = !firstFlat;
    std::vector<std::string> emitted;
    for (const auto& e : entries_) {
        if (e.section.empty()) continue;
        if (std::find(emitted.begin(), emitted.end(), e.section) == emitted.end()) {
            if (needBlank) out << "\n";
            needBlank = true;
            out << "[" << e.section << "]\n";
            for (const auto& x : entries_) {
                if (x.section == e.section) {
                    out << x.name << "=" << x.value << "\n";
                }
            }
            emitted.push_back(e.section);
        }
    }

    if (!out.good()) {
        std::fprintf(stderr, "[renderConfigFile] write failed: %s\n", m_filename.c_str());
        return false;
    }
    return true;
}