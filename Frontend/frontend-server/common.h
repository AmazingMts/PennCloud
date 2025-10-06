// common.h
#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <string>
#include "mail-service.h"

struct clientContext {
    int conn_fd;
    sockaddr_in clientAddr;
    std::string requestBuffer;
    std::string responseBuffer;
    std::string username;
    std::string pw;
};

#endif
