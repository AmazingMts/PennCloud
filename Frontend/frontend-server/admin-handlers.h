#pragma once

#include "frontend-server.h"
#include <string>
#include <unordered_map>
#include "../../Shared/IStorageService.h"
class AdminHandler
{
public:
    explicit AdminHandler(IStorageService *storage_) : storage(storage_) {}
    void handle(clientContext *client,
                const std::string &method,
                const std::string &path,
                const std::string &body,
                const std::string &username,
                const std::unordered_map<std::string, std::string> &headers,
                const std::vector<std::map<std::string, std::string>> &nodeinfo);
    static void nodes(clientContext *client,
                      const std::vector<std::map<std::string, std::string>> &nodeinfo);

    static std::string json_escape(const std::string &s);

private:
    IStorageService *storage;
};
