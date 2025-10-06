#ifndef EMAIL_HANDLERS_H
#define EMAIL_HANDLERS_H

#include "frontend-server.h"
#include "common.h"
#include "../../Shared/IStorageService.h"

struct clientContext;

struct Email {
    std::string id;
    std::string sender;
    std::string subject;
    std::string timestamp;
    std::string body;
};

void handleEmailRequests(clientContext *client, std::string method, std::string path, std::string body, IStorageService *storage, std::string username, std::string password);
std::string listEmails(clientContext *client, std::string method, std::string path);
std::string retrieveEmail(clientContext *client, std::string method, std::string path);
std::string getQueryParam(const std::string &url, const std::string &param);
// std::string replace(const std::string &str, const std::string &from, const std::string &to);
std::string replacePlaceholder(const std::string& html, const std::string& placeholder, const std::string& value);
#endif