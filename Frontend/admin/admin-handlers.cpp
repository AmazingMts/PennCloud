#include "admin-handlers.h"
#include <sstream>
#include <set>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../Shared/SharedStructures.h"
#include "../../Shared/CoordinatorService.h"
#include "../../Shared/IStorageService.h"
#include "common.h"
#include <cstring>

std::string AdminHandler::json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '\"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}
std::string extractField(const std::string &json, const std::string &key) {
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";

    pos += pattern.length();

    // Skip whitespace
    while (pos < json.size() && isspace(json[pos])) pos++;

    // If string value
    if (json[pos] == '"') {
        size_t start = ++pos;
        size_t end = json.find('"', start);
        return json.substr(start, end - start);
    }

    // If boolean or number
    size_t end = json.find_first_of(",}", pos);
    return json.substr(pos, end - pos);
}

void AdminHandler::nodes(clientContext *client,
                         const std::vector<std::map<std::string, std::string>> &nodeinfo)

{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < nodeinfo.size(); ++i)
    {
        const auto &n = nodeinfo[i];
        oss << "{";
        oss << "\"role\":\"" << AdminHandler::json_escape(n.at("Role")) << "\",";
        oss << "\"addr\":\"" << AdminHandler::json_escape(n.at("Address")) << "\",";
        oss << "\"status\":\"" << AdminHandler::json_escape(n.at("Status")) << "\"";
        oss << "}";
        if (i + 1 < nodeinfo.size())
            oss << ",";
    }
    oss << "]";

    std::string json = oss.str();
   sendResponse( client, json,"200 OK");
}
void view(clientContext *client)
{
    std::string path = "static/account/admin.html";
    std::string html = readFile(path);
   sendResponse( client,html, "200 OK");
}

void AdminHandler::handle(clientContext *client,
                          const std::string &method,
                          const std::string &path,
                          const std::string &body,
                          const std::string &username,
                          const std::unordered_map<std::string, std::string> &headers,
                          const std::vector<std::map<std::string, std::string>>
                              &nodeinfo)
{
    if (method == "GET")
    {
        if (path == "/admin")
        {
            view(client);
        }
        else if (path == "/admin/nodes")
        {
            nodes(client, nodeinfo);
        } else if (path == "/admin/fe")
        {
            std::string path = "./admin-fe.html";
            std::string html = readFile(path);
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html\r\n"
                     << "Connection: keep-alive\r\n"
                     << "Content-Length: " << html.size() << "\r\n\r\n"
                     << html;
            std::string fullResponse = response.str();
            send(client->conn_fd, fullResponse.c_str(), fullResponse.size(), 0);
        }
        else if (path.rfind("/admin/details/", 0) == 0) {
            std::cout<<"[Admin details] path is "<<path<<std::endl;
            std::string full = path.substr(strlen("/admin/details/"));
            size_t qm = full.find('?');
            std::string addrPart = (qm != std::string::npos) ? full.substr(0, qm) : full;
            std::string query = (qm != std::string::npos) ? full.substr(qm + 1) : "";
            
            std::string addr = urlDecode(addrPart);
            std::string host, portStr;
            size_t colon = addr.rfind(':');
            if (colon == std::string::npos) {
                sendResponse(client,"{\"message\": \"Invalid address\"}", "400 Bad Request");
                return;
            }
            host = addr.substr(0, colon);
            portStr = addr.substr(colon + 1);
            std::set<std::string> rows_test;
            storage->list_rows(rows_test) != 0;
            for (const auto &rows : rows_test) {
                std::cout<<"[Admin test] all rows are "<<rows<<std::endl;
            }
               
            
            auto getQueryValue = [](const std::string &query, const std::string &key) -> std::string {
                size_t start = query.find(key + "=");
                if (start == std::string::npos) return "";
                start += key.length() + 1;
                size_t end = query.find('&', start);
                return query.substr(start, end - start);
            };

            std::string is_primary_str = getQueryValue(query, "primary");
            std::string is_alive_str = getQueryValue(query, "alive");
            std::cout<<"[Admin details] is_primary "<<is_primary_str<<std::endl;
            std::cout<<"[Admin details] is_alive_str "<<is_alive_str<<std::endl;
            std::cout<<"[Admin details] host "<<host<<std::endl;
            std::cout<<"[Admin details] port "<<portStr<<std::endl;


            KVServer server;
            server.host = host;
            server.port = std::stoi(portStr);
            server.is_primary = (is_primary_str == "true");
            server.is_alive = (is_alive_str == "true");
            std::cout << "[Admin Row] server = { "
                    << "host: " << server.host << ", "
                    << "port: " << server.port << ", "
                    << "is_primary: " << std::boolalpha << server.is_primary << ", "
                    << "is_alive: " << std::boolalpha << server.is_alive
                    << " }" << std::endl;       
            std::set<std::string> rows;
            if (storage->list_rows(server, rows) != 0) {
                sendResponse( client,"{\"message\": \"Failed to list rows\"}", "500 Internal Server Error");
                return;
            }

            std::cout<<"[Admin details] row finished"<<std::endl;
            std::ostringstream oss;
            oss << "{ \"data\": [";
            bool first = true;
            for (const auto &row : rows) {
                if (row.empty()) continue;

                std::set<std::string> columns;
                if (storage->list_columns(server, row, columns) != 0) continue;

                if (!first) oss << ",";
                oss << "{ \"row\": \"" << AdminHandler::json_escape(row) << "\", \"columns\": [";

                bool first_col = true;
                for (const auto &col : columns) {
                    if (!first_col) oss << ",";
                    oss << "\"" << AdminHandler::json_escape(col) << "\"";
                    first_col = false;
                }

                oss << "] }";
                first = false;
            }
                oss << "] }";
                first = false;
            
            std::string responseStr = oss.str();
            std::cout << "[Admin details] response JSON = " << responseStr << std::endl;
            sendResponse( client,oss.str(), "200 OK");
        }

   
    }
    else if (method == "POST")

    {

        bool isRestart = false, isEnd = false;
        size_t baseLen = 0;

        if (path.rfind("/admin/restart", 0) == 0) {
            isRestart = true;
            baseLen = strlen("/admin/restart");
        } else if (path.rfind("/admin/end", 0) == 0) {
            isEnd = true;
            baseLen = strlen("/admin/end");
        }
        if (isRestart || isEnd) {
            std::cout<<"[ADMIN END] body is "<<body<<std::endl;
        std::string host = extractField(body, "host");
        std::string portStr = extractField(body, "port");
        std::string primaryStr = extractField(body, "is_primary");
        std::string aliveStr = extractField(body, "is_alive");

        if (host.empty() || portStr.empty() || primaryStr.empty() || aliveStr.empty()) {
            sendResponse(client, "{\"message\":\"Missing field(s)\"}", "400 Bad Request");
            return;
        }

        int port = std::stoi(portStr);
        bool is_primary = (primaryStr == "true");
        bool is_alive = (aliveStr == "true");

        KVServer target;
        target.host = host;
        target.port = port;
        target.is_primary = is_primary;
        target.is_alive = is_alive;

        std::cout << "[Admin] " << (isRestart ? "RESTART" : "END")
                << " " << host << ":" << port
                << ", is_primary=" << is_primary
                << ", is_alive=" << is_alive << std::endl;

        int rc = isRestart ? storage->bring_up(target) : storage->shut_down(target);

        if (rc == 0) {
            sendResponse(client, "{\"message\": \"" + std::string(isRestart ? "Restart OK" : "Shutdown OK") + "\"}", "200 OK");
        } else {
            sendResponse(client, "{\"message\": \"" + std::string(isRestart ? "Restart Failed" : "Shutdown Failed") + "\"}", "500 Internal Server Error");
        }
        return;
        }
    
       if (path.find("/admin/fe-command", 0) == 0) {
            std::string requestBody = body;  // this should contain the JSON body

            // Simple JSON parse (if you use nlohmann/json, better)
            std::string action, addr;
            size_t actionPos = requestBody.find("\"action\"");
            if (actionPos != std::string::npos) {
                size_t colon = requestBody.find(":", actionPos);
                size_t quote1 = requestBody.find("\"", colon + 1);
                size_t quote2 = requestBody.find("\"", quote1 + 1);
                action = requestBody.substr(quote1 + 1, quote2 - quote1 - 1);
            }

            size_t addrPos = requestBody.find("\"addr\"");
            if (addrPos != std::string::npos) {
                size_t colon = requestBody.find(":", addrPos);
                size_t quote1 = requestBody.find("\"", colon + 1);
                size_t quote2 = requestBody.find("\"", quote1 + 1);
                addr = requestBody.substr(quote1 + 1, quote2 - quote1 - 1);
            }

            if (!action.empty() && !addr.empty()) {
                std::string command = action + " " + addr + "\n";

                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    perror("socket");
                } else {
                    sockaddr_in lbAddr{};
                    lbAddr.sin_family = AF_INET;
                    lbAddr.sin_port = htons(8880); // LB's listening port for commands
                    inet_pton(AF_INET, "127.0.0.1", &lbAddr.sin_addr); // LB IP

                    if (connect(sockfd, (sockaddr *)&lbAddr, sizeof(lbAddr)) < 0) {
                        perror("connect to LB failed");
                    } else {
                        send(sockfd, command.c_str(), command.size(), 0);
                        std::cout << "[DEBUG] Sent command to LB: " << command << std::endl;
                    }
                    close(sockfd);
                }

                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                write(client->conn_fd, response.c_str(), response.size());
            } else {
                std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
                write(client->conn_fd, response.c_str(), response.size());
            }
            return;
        }
        else {
            std::cout << "[Admin] path is " << path << std::endl;
            size_t pos = path.rfind("/admin/restart");
            size_t pos_ = path.rfind("/admin/end");
            if (pos != std::string::npos)
            {
                size_t start = pos + 15;
                std::string needrequest = path.substr(start);
                size_t sep = needrequest.find('/');
                if (sep != std::string::npos)
                {
                    std::string encodedAddr = needrequest.substr(0, sep);
                    std::string role = needrequest.substr(sep + 1);
                    bool is_primary;
                    std::string addr = urlDecode(encodedAddr);
                    size_t pos = addr.rfind(":");
                    std::string portStr = addr.substr(pos + 1);
                    int port = std::stoi(portStr);
                    addr = addr.substr(0, pos);
                    size_t pos1 = role.rfind("primary");
                    if (pos1 != std::string::npos)
                    {
                        is_primary = true;
                    }
                    else
                    {
                        is_primary = false;
                    }

                    std::cout << "[Admin] addr is " << addr << std::endl;
                    std::cout << "[Admin] is_primary is " << std::to_string(is_primary) << std::endl;
                    std::cout << "[Admin] port is " << port << std::endl;
                    KVServer target;
                    target.host = addr;
                    target.is_alive = true;
                    target.port = port;
                    target.is_primary = is_primary;
                }
            }
            else if (pos_ != std::string::npos)
            {
                size_t start = pos_ + 11;
                std::string needrequest = path.substr(start);
                size_t sep = needrequest.find('/');
                if (sep != std::string::npos)
                {
                    std::string encodedAddr = needrequest.substr(0, sep);
                    std::string role = needrequest.substr(sep + 1);
                    bool is_primary;
                    std::string addr = urlDecode(encodedAddr);
                    size_t pos = addr.rfind(":");
                    std::string portStr = addr.substr(pos + 1);
                    int port = std::stoi(portStr);
                    addr = addr.substr(0, pos);
                    size_t pos1 = role.rfind("primary");
                    if (pos1 != std::string::npos)
                    {
                        is_primary = true;
                    }
                    else
                    {
                        is_primary = false;
                    }

                    std::cout << "[Admin] addr is " << addr << std::endl;
                    std::cout << "[Admin] is_primary is " << std::to_string(is_primary) << std::endl;
                    std::cout << "[Admin] port is " << port << std::endl;
                    KVServer target;
                    target.host = addr;
                    target.is_alive = true;
                    target.port = port;
                    target.is_primary = is_primary;
                    int rc = storage->shut_down(target);
                    std::cout << "[Admin] rc is " << std::to_string(rc) << std::endl;
                    if (rc == 0)
                    {
                        sendResponse( client,"{\"message\": \"Shutdown OK\"}", "200 OK");
                        nodes(client, nodeinfo);
                    }
                    else
                    {
                        sendResponse(client,"{\"message\": \"Shutdown Fails\"}",  "500 Internal Server Error");
                    }
                }
            }
        }
    }
}
