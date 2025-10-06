#pragma once
#include "frontend-server.h"
#include "common.h"
#include <string>
#include "../../Shared/IStorageService.h"

class UserHandler
{
public:
    UserHandler(IStorageService *_Storage)
    {
        storage = _Storage;
    }

    void handle(clientContext *client, const std::string &method, const std::string &path, const std::string &body, const std::string &username="");

private:
    IStorageService *storage;
};
