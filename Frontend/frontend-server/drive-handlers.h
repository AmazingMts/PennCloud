#pragma once

#include "frontend-server.h"
#include <string>
#include <unordered_map>
#include "../../Shared/IStorageService.h"

class DriveHandler
{
public:
    DriveHandler(IStorageService *storage_) : storage(storage_) {}

    void handle(clientContext *client,
                const std::string &method,
                const std::string &path,
                const std::string &body,
                std::string username,
                std::unordered_map<std::string, std::string> header);

private:
    IStorageService *storage;

    void handleDownload(clientContext *client,
                        const std::string &path,
                        const std::string &username);

    void handleView(clientContext *client,
                    const std::string &path,
                    const std::unordered_map<std::string, std::string> &header,
                    const std::string &username);

    void handleUpload(clientContext *client,
                      const std::string &path,
                      const std::string &body,
                      const std::string &username);

    void handleDelete(clientContext *client,
                      const std::string &path,
                      const std::string &username);

    void handleRename(clientContext *client,
                      const std::string &path,
                      const std::string &body,
                      const std::string &username);
};
