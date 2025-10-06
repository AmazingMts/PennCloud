// utils.cpp
#include "utils.h"
#include <string>
#include <cstdio>

/* Common helper functions shared across handlers. */
std::string urlDecode(const std::string &s) {
    std::string result;
    char ch;
    int ii;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '+') {
            result += ' ';
        } else if (s[i] == '%' && i + 2 < s.length()) {
            sscanf(s.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            result += ch;
            i += 2;
        } else {
            result += s[i];
        }
    }
    return result;
}

std::unordered_map<std::string, std::string>
parseFormEncoded(const std::string& body) {
    std::unordered_map<std::string, std::string> parsedData;
    std::string key, value;
    ssize_t pos = 0, amp; // position of the first '&'
    while ((amp = body.find("&", pos)) != std::string::npos) {
        std::string pair = body.substr(pos, amp - pos);
        size_t equalPos = pair.find("=");
        if (equalPos != std::string::npos) {
            parsedData[urlDecode(pair.substr(0, equalPos))] = urlDecode(pair.substr(equalPos + 1));
        }
        pos = amp + 1;
    }
    size_t eq = body.find('=', pos);
    if (eq != std::string::npos)
        parsedData[urlDecode(body.substr(pos, eq - pos))] = urlDecode(body.substr(eq + 1));
    return parsedData;
}