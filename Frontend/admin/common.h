#ifndef COMMON_H
#define COMMON_H
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string>
#include <sstream>
#include <unistd.h>
#include <fstream>

#include <iomanip>
struct clientContext {
    int conn_fd;
    sockaddr_in clientAddr;
};
inline void sendResponse(clientContext* client, const std::string& body, const std::string& status = "200 OK") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Content-Type: text/html\r\n\r\n"
        << body;
    std::string response = oss.str();
    send(client->conn_fd, response.c_str(), response.size(), 0);
}
inline std::string urlDecode(const std::string &s) {
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
inline std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

#endif