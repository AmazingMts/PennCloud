// #include "admin-handlers.h"
// #include <sstream>
// #include <set>
// #include <iostream>
// #include <netinet/in.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include "../../Shared/SharedStructures.h"
// #include "drive-handlers.h"
// #include "../../Shared/SharedStructures.h"
// #include "utils.h"

// std::string AdminHandler::json_escape(const std::string &s)
// {
//     std::string out;
//     out.reserve(s.size());
//     for (char c : s)
//     {
//         switch (c)
//         {
//         case '\"':
//             out += "\\\"";
//             break;
//         case '\\':
//             out += "\\\\";
//             break;
//         case '\n':
//             out += "\\n";
//             break;
//         case '\r':
//             out += "\\r";
//             break;
//         default:
//             out += c;
//             break;
//         }
//     }
//     return out;
// }
// void AdminHandler::nodes(clientContext *client,
//                          const std::vector<std::map<std::string, std::string>> &nodeinfo)

// {
//     std::ostringstream oss;
//     oss << "[";
//     for (size_t i = 0; i < nodeinfo.size(); ++i)
//     {
//         const auto &n = nodeinfo[i];
//         oss << "{";
//         oss << "\"role\":\"" << AdminHandler::json_escape(n.at("Role")) << "\",";
//         oss << "\"addr\":\"" << AdminHandler::json_escape(n.at("Address")) << "\"";
//         oss << "}";
//         if (i + 1 < nodeinfo.size())
//             oss << ",";
//     }
//     oss << "]";

//     std::string json = oss.str();
//     FrontendServer::sendResponse(json, client, "200 OK");
// }
// void view(clientContext *client)
// {
//     std::string path = "static/account/admin.html";
//     std::string html = FrontendServer::readFile(path);
//     FrontendServer::sendResponse(html, client, "200 OK");
// }

// void AdminHandler::handle(clientContext *client,
//                           const std::string &method,
//                           const std::string &path,
//                           const std::string &body,
//                           const std::string &username,
//                           const std::unordered_map<std::string, std::string> &headers,
//                           const std::vector<std::map<std::string, std::string>>
//                               &nodeinfo)
// {
//     if (method == "GET")
//     {
//         if (path == "/admin")
//         {
//             view(client);
//         }
//         else if (path == "/admin/nodes")
//         {
//             nodes(client, nodeinfo);
//         }
//          else if (path.rfind("/admin/details/", 0) == 0) {
//     std::string addr = path.substr(std::string("/admin/details/").length());

//     std::set<std::string> rows;
//     if (storage->list_rows(rows) != 0) {
//         sendResponse(client, "{\"message\":\"Failed to list rows\"}", "500 Internal Server Error");
//         return;
//     }

//     std::ostringstream oss;
//     oss << "{ \"data\": [";
//     bool first = true;
//     for (const auto &row : rows) {
//         std::set<std::string> columns;
//         if (storage->list_columns(row, columns) != 0)
//             continue;

//         if (!first) oss << ",";
//         oss << "{ \"row\": \"" << AdminHandler::json_escape(row) << "\", \"columns\": [";
//         bool first_col = true;
//         for (const auto &col : columns) {
//             if (!first_col) oss << ",";
//             oss << "\"" << AdminHandler::json_escape(col) << "\"";
//             first_col = false;
//         }
//         oss << "] }";
//         first = false;
//     }
//     oss << "] }";

//     std::string json = oss.str();
//     sendResponse(client, json, "200 OK");
//     return;
// }
//     else if (method == "POST")

//     {
//         std::cout << "[Admin] path is " << path << std::endl;
//         size_t pos = path.rfind("/admin/restart");
//         size_t pos_ = path.rfind("/admin/end");
//         if (pos != std::string::npos)
//         {
//             size_t start = pos + 15;
//             std::string needrequest = path.substr(start);
//             size_t sep = needrequest.find('/');
//             if (sep != std::string::npos)
//             {
//                 std::string encodedAddr = needrequest.substr(0, sep);
//                 std::string role = needrequest.substr(sep + 1);
//                 bool is_primary;
//                 std::string addr = urlDecode(encodedAddr);
//                 size_t pos = addr.rfind(":");
//                 std::string portStr = addr.substr(pos + 1);
//                 int port = std::stoi(portStr);
//                 addr = addr.substr(0, pos);
//                 size_t pos1 = role.rfind("primary");
//                 if (pos1 != std::string::npos)
//                 {
//                     is_primary = true;
//                 }
//                 else
//                 {
//                     is_primary = false;
//                 }

//                 std::cout << "[Admin] addr is " << addr << std::endl;
//                 std::cout << "[Admin] is_primary is " << std::to_string(is_primary) << std::endl;
//                 std::cout << "[Admin] port is " << port << std::endl;
//                 KVServer target;
//                 target.host = addr;
//                 target.is_alive = true;
//                 target.port = port;
//                 target.is_primary = is_primary;
//             }
//         }
//         else if (pos_ != std::string::npos)
//         {
//             size_t start = pos_ + 11;
//             std::string needrequest = path.substr(start);
//             size_t sep = needrequest.find('/');
//             if (sep != std::string::npos)
//             {
//                 std::string encodedAddr = needrequest.substr(0, sep);
//                 std::string role = needrequest.substr(sep + 1);
//                 bool is_primary;
//                 std::string addr = urlDecode(encodedAddr);
//                 size_t pos = addr.rfind(":");
//                 std::string portStr = addr.substr(pos + 1);
//                 int port = std::stoi(portStr);
//                 addr = addr.substr(0, pos);
//                 size_t pos1 = role.rfind("primary");
//                 if (pos1 != std::string::npos)
//                 {
//                     is_primary = true;
//                 }
//                 else
//                 {
//                     is_primary = false;
//                 }

//                 std::cout << "[Admin] addr is " << addr << std::endl;
//                 std::cout << "[Admin] is_primary is " << std::to_string(is_primary) << std::endl;
//                 std::cout << "[Admin] port is " << port << std::endl;
//                 KVServer target;
//                 target.host = addr;
//                 target.is_alive = true;
//                 target.port = port;
//                 target.is_primary = is_primary;
//                 int rc = storage->shut_down(target);
//                 std::cout << "[Admin] rc is " << std::to_string(rc) << std::endl;
//                 if (rc == 0)
//                 {
//                     FrontendServer::sendResponse("{\"message\": \"Shutdown OK\"}", client, "200 OK");
//                     nodes(client, nodeinfo);
//                 }
//                 else
//                 {
//                     FrontendServer::sendResponse("{\"message\": \"Shutdown Fails\"}", client, "500 Internal Server Error");
//                 }
//             }
//         }
//     }
// }
