#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

#include "NewClientArgs.h"
#include "ThreadSafeQueue.h"
#include "ICommandDispatcher.h"

/*
 * Class for handling each individual client
 */
class ClientHandler
{
public:
    ClientHandler(NewClientArgs *nca);
    ~ClientHandler() = default;
    void handleClient();

    // Static wrapper instanciates new ClientHandler class inside each thread
    static void *handleClientThreadWrapper(void *arg);

    // Creates a new thread that will read data from the client and push to thread safe queue
    static void *readerThreadFunction_(void *arg);

    // NOTE: The below member variables are public because
    // we need to use them in the readerThreadFunction_
    int socket_fd;
    ThreadSafeQueue message_queue;

private:
    pthread_t reader_thread_;
    ServerType server_type_;

    // Other types of arguments expected for each type of server
    // might vary depending on the server type
    // string mailbox_path_;

    // Close client connection on the server side and removes the socket from the global THREAD2SOCKET map
    void closeClient_();

    // Uses server_type_ to retreive corresponding dispatcher
    ICommandDispatcher *getDispatcher_(ServerType server_type);

    // Split response message from dispatcher by provided delimeter to support
    // multiline responses to connected client.
    vector<string> splitByDelimiter_(const string &str, const string &delimiter);
};

#endif
