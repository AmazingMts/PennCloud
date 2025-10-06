#include "admin-handlers.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include "../../Shared/CoordinatorService.h"
#include "../../Shared/IStorageService.h"
#include "../../Shared/KVServerCommand.h"

#define BUFFER_SIZE 8192

static std::string loadBalancerIp = "127.0.0.1";
static int loadBalancerPort = 8880;

CoordinatorService* COORDINATOR_SERVICE = nullptr;


void run(){
    pthread_t coordinatorThread;

    // if (DEBUG)
    //     std::cout << "[DEBUG] Creating thread for CoordinatorService..." << std::endl;
    auto coordinator_service_runner = [](void *arg) -> void *
    {
        CoordinatorService *service = static_cast<CoordinatorService *>(arg);\
        std::atomic<bool> tag(true);
        service->start(tag);
        return nullptr;
    };

    if (pthread_create(&coordinatorThread, nullptr, coordinator_service_runner, COORDINATOR_SERVICE) != 0)
    {
        fprintf(stderr, "Failed to create thread for CoordinatorService\n");
    }

    if (pthread_detach(coordinatorThread) != 0)
    {
        perror("Failed to detach thread for CoordinatorService\n");
        return;
    }

    if (COORDINATOR_SERVICE == nullptr)
    {
        std::cerr << "Failed to create CoordinatorService instance." << std::endl;
        return;
    }
}
std::vector<std::string> getListfromLb(const std::string &lbIp, int lbPort)
{
    std::vector<std::string> list;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return list;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(lbPort);
    if (inet_pton(AF_INET, lbIp.c_str(), &addr.sin_addr) != 1 ||
        connect(sock, (sockaddr *)&addr, sizeof addr) < 0)
    {
        close(sock);
        return list;
    }

    const std::string req = "GET /nodes HTTP/1.0\r\n\r\n";
    send(sock, req.data(), req.size(), 0);

    std::string resp;
    char buf[1024];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof buf, 0)) > 0)
        resp.append(buf, n);
    close(sock);
    if (resp.empty())
        return list;

    size_t p = resp.find("\r\n\r\n");
    std::string body = (p == std::string::npos) ? resp : resp.substr(p + 4);

    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            list.push_back(line);
    }
    return list;
}








std::vector<std::map<std::string, std::string>> fetchAllnode(CoordinatorService* COORDINATOR_SERVICE)
{
    std::vector<std::map<std::string, std::string>> nodes;

    std::string cdaddr = loadBalancerIp + ":500";
    nodes.push_back({{"Address", cdaddr}, {"Role", "Coordinator"},{"Status","Alive"}});

    auto backendMap = COORDINATOR_SERVICE->get_kv_servers_map();
    std::cout << "[Admin] Backend nodelist:\n";
    for (const auto& [groupId, serverList] : backendMap) {
        for (const auto& server : serverList) {
            std::string role = server.is_primary ? "KVS primary" : "KVS replica";
            std::cout << "  [Group " << groupId << "] " << role 
                    << " at " << server.host << ":" << server.port << "\n";
        }
    }
    for (auto const &[groupId, serverList] : backendMap) {
        for (auto const &svc : serverList) {
            nodes.push_back({
                {"Address", svc.host + ":" + std::to_string(svc.port)},
                {"Role", svc.is_primary ? "KVS primary" : "KVS replica"},
                {"Status",svc.is_alive?"Alive":"Not"}
            });
        }
    }

    std::cout << "Fetched " << nodes.size() << " nodes:\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto &info = nodes[i];
        std::cout << "[" << i << "] Address = " << info.at("Address")
                  << ", Role = " << info.at("Role") << "\n";
    }
    return nodes;
}

void handleClient(clientContext *client) {
 IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
     AdminHandler* adminHandler = new AdminHandler(&storage);
    char buffer[BUFFER_SIZE] = {0};
    int bytes = recv(client->conn_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        close(client->conn_fd);
        delete client;
        return;
    }

    std::string request(buffer);
    std::istringstream iss(request);
    std::string method, path;
    iss >> method >> path;

    std::string body;
    size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        body = request.substr(headerEnd + 4);
    }

    if (path == "/admin" || path == "/admin/") {
        std::string html = readFile("admin.html");
        sendResponse(client, html.empty() ? "<h1>Page not found</h1>" : html);
    } else if (path.rfind("/admin", 0) == 0) {
        std::unordered_map<std::string, std::string> header;
        std::vector<std::map<std::string, std::string>> nodeinfo = fetchAllnode(COORDINATOR_SERVICE);
        adminHandler->handle(client, method, path, body, "", header, nodeinfo);
    } else {
        sendResponse(client, "<h1>404 Not Found</h1>", "404 Not Found");
    }

    close(client->conn_fd);
    delete client;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);
    int cs_port = 500;
    if (argc > 2) cs_port = std::stoi(argv[2]);
    COORDINATOR_SERVICE = new CoordinatorService(loadBalancerIp, cs_port, true);
    

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(serverFd, (sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverFd, 10);

    std::cout << "Admin server listening on port " << port << std::endl;
    run();
    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, (sockaddr *)&clientAddr, &addrLen);
        if (clientFd < 0) continue;

        clientContext* client = new clientContext();
        client->conn_fd = clientFd;
        client->clientAddr = clientAddr;
    std::thread([client]() {
        handleClient(client);
    }).detach(); 
    }

    close(serverFd);
    return 0;
}
