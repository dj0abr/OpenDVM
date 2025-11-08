#pragma once
#include <string>

namespace helper {
    /** Returns true if the character should be trimmed at the edges (SPC, CR, LF, ASCII >= 128). */
    [[nodiscard]] bool isTrimChar(unsigned char c) noexcept;

    /** Trims leading and trailing CR/LF/SPC/ASCII>=128 characters of the string in place. */
    void trimEnds(std::string& s) noexcept;

    /** Restart a systemd service; returns true on success. */
    bool restartUnit(const char* unit) noexcept;
}
