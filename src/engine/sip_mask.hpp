#pragma once
#include <string>

namespace corasip {

// Hides the value of Authorization and Proxy-Authorization headers in SIP text. The header name stays, so the trace
// still shows that credentials were sent, but digest responses never reach the trace or the logs.
std::string maskDigests(const std::string& text);

}  // namespace corasip
