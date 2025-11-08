#pragma once
#include <string>
#include <vector>

/**
 * Reads a text file line by line and trims per line
 * leading/trailing CR, LF, SPC, and ASCII >= 128. On failure, 'lines' remains empty.
 */
class renderConfigFile {
public:
    /** Parsed entry: section, name, value (value kept as string including quotes if present). */
    struct Entry {
        std::string section;
        std::string name;
        std::string value;
    };

    /**
     * Constructor: remembers filename, optionally creates a timestamped .bak of the file
     * BEFORE any other action, then reads and preprocesses lines, then parses entries.
     */
    explicit renderConfigFile(const std::string& filename, bool backupOnConstruct = false);

    // Destructor: no automatic save.
    ~renderConfigFile() = default;

    // Datei wirklich geladen?
    bool isLoaded() const noexcept { return loaded_; }

    /** Returns parsed entries snapshot. */
    [[nodiscard]] const std::vector<Entry>& getEntries() const noexcept;

    /**
     * Sets or inserts a name=value pair.
     * - New value string is passed WITHOUT quotes.
     * - If replacing and original value was quoted, the new value will be written quoted as well.
     * - If inserting a new key, the value is written as-is (no quotes added).
     * - If 'section' is empty, targets the flat namespace; creates section when needed.
     * Returns:
     *   true  -> a value was changed or newly inserted
     *   false -> old and new logical values are identical, nothing changed
     */
    [[nodiscard]] bool setValue(const std::string& name,
                                const std::string& value,
                                const std::string& section = std::string());

    /** Writes the parsed entries back to the original file path (no backup handling). */
    bool saveConfigFile() const;

    std::string findValue(const std::string& section, const std::string& name) const;

    // Public for early testing; will be encapsulated later.
    std::vector<std::string> lines;

private:
    void parser(); // re-parses 'lines' into 'entries_'

    bool loaded_ = false;
    std::string m_filename;
    std::vector<Entry> entries_;
};