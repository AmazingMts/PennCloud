// lbregister.cpp
#include "lbregister.h"
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sstream>
#include <arpa/inet.h>

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

bool registerWithLoadBalancer(const std::string &loadBalancerIp, int loadBalancerPort, int localPort)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Socket creation failed");
        return false;
    }

    sockaddr_in lbAddr;
    lbAddr.sin_family = AF_INET;
    lbAddr.sin_port = htons(loadBalancerPort);
    if (inet_pton(AF_INET, loadBalancerIp.c_str(), &lbAddr.sin_addr) <= 0)
    {
        perror("Invalid Load Balancer IP");
        return false;
    }

    if (connect(sockfd, (sockaddr *)&lbAddr, sizeof(lbAddr)) < 0)
    {
        perror("Connect to Load Balancer failed");
        return false;
    }

    sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    if (getsockname(sockfd, (sockaddr *)&localAddr, &addrLen) < 0)
    {
        perror("getsockname failed");
        return false;
    }

    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf));
    std::string addrStr = "http://" + std::string(ipBuf) + ":" + std::to_string(localPort);
    std::string request = "GET /register?addr=" + addrStr + " HTTP/1.1\r\n\r\n";
    ssize_t sent = write(sockfd, request.c_str(), request.size());
    if (sent < 0)
    {
        perror("write failed");
        close(sockfd);
        return false;
    }

    close(sockfd);
    return true;
}
