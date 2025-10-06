#ifndef NEWCLIENTARGS_H
#define NEWCLIENTARGS_H

#include <string>
#include "Server.h"

using namespace std;

struct NewClientArgs
{
    // Type of server we are running
    ServerType server_type;

    int socket_fd;

    // Other necessary args for other types of server
    // string mailbox_path;
};

#endif
