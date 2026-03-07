// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/escape_processing.hpp"

#include <cctype>

namespace etil::core {

std::optional<std::string> read_escaped_string(std::istringstream& iss,
                                                char delimiter,
                                                std::ostream& err) {
    // Strip one leading space (same convention as ." / s")
    if (iss.peek() == ' ') {
        iss.get();
    }

    std::string result;
    char ch;
    while (iss.get(ch)) {
        if (ch == delimiter) {
            return result;
        }
        if (ch == '\\') {
            char next;
            if (!iss.get(next)) {
                err << "Error: unexpected end of input after backslash\n";
                return std::nullopt;
            }
            switch (next) {
            case 'n':  result += '\n'; break;
            case 'r':  result += '\r'; break;
            case 't':  result += '\t'; break;
            case '0':  result += '\0'; break;
            case 'a':  result += '\a'; break;
            case 'b':  result += '\b'; break;
            case 'f':  result += '\f'; break;
            case '\\': result += '\\'; break;
            case '%': {
                char h1, h2;
                if (!iss.get(h1) || !iss.get(h2)) {
                    err << "Error: incomplete \\% hex escape\n";
                    return std::nullopt;
                }
                if (!std::isxdigit(static_cast<unsigned char>(h1)) ||
                    !std::isxdigit(static_cast<unsigned char>(h2))) {
                    err << "Error: invalid hex digits in \\%"
                        << h1 << h2 << "\n";
                    return std::nullopt;
                }
                auto hex_val = [](char c) -> unsigned char {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                    return 10 + (c - 'A');
                };
                result += static_cast<char>((hex_val(h1) << 4) | hex_val(h2));
                break;
            }
            default:
                if (next == delimiter) {
                    result += delimiter;
                } else {
                    err << "Error: unknown escape sequence \\"
                        << next << "\n";
                    return std::nullopt;
                }
                break;
            }
        } else {
            result += ch;
        }
    }

    err << "Error: unterminated string (missing closing "
        << delimiter << ")\n";
    return std::nullopt;
}

} // namespace etil::core
