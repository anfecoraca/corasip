#include "engine/sip_mask.hpp"

#include <strings.h>

#include <cstring>

namespace corasip {

// Authorization and Proxy-Authorization carry digest responses: keep the header name, hide the value.
std::string maskDigests(const std::string& text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find("\r\n", pos);
        std::size_t end = eol == std::string::npos ? text.size() : eol + 2;
        std::string line = text.substr(pos, end - pos);
        for (const char* name : {"Authorization:", "Proxy-Authorization:"}) {
            if (line.size() > std::strlen(name) && strncasecmp(line.c_str(), name, std::strlen(name)) == 0) {
                line = std::string(line.c_str(), std::strlen(name)) + " <hidden>" + (eol == std::string::npos ? "" : "\r\n");
                break;
            }
        }
        out += line;
        pos = end;
    }
    return out;
}

}  // namespace corasip
