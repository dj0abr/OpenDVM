#include <cstdlib>
#include "helper.h"

namespace helper {
    /** Returns true if the character should be trimmed at the edges (SPC, CR, LF, ASCII >= 128). */
    bool isTrimChar(unsigned char c) noexcept {
        return c == ' ' || c == '\r' || c == '\n' || c >= 128;
    }

    /** Trims leading and trailing CR/LF/SPC/ASCII>=128 characters of the string in place. */
    void trimEnds(std::string& s) noexcept {
        size_t i = 0;
        while (i < s.size() && isTrimChar(static_cast<unsigned char>(s[i]))) ++i;
        size_t j = s.size();
        while (j > i && isTrimChar(static_cast<unsigned char>(s[j - 1]))) --j;
        s.erase(j);
        s.erase(0, i);
    }

    /** Restart a systemd service; returns true on success. */
    bool restartUnit(const char* unit) noexcept {
        const std::string cmd = "sudo /bin/systemctl restart " + std::string(unit) + " >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
    }
}
