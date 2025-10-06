#include <unistd.h>
#include <mutex>
#include <pthread.h>
#include <signal.h> // Used for signal message types
#include "Server.h" // Used for DEBUG flag and signal handlers declared in Server.h
#include "ClientHandler.h"
#include "SocketReader.h"
#include "SocketWriter.h"
#include "ICommandDispatcher.h"
#include "POP3/Pop3CommandDispatcher.h"
#include "KVStorage/KVStorageCommandDispatcher.h"
#include "SMTP/SmtpCommandDispatcher.h"
#include "Coordinator/CoordinatorCommandDispatcher.h"
// TODO: Include other command dispatchers for servers
#include <sys/socket.h>

using namespace std;

ClientHandler::ClientHandler(NewClientArgs *nca)
{
    this->socket_fd = nca->socket_fd;
    this->server_type_ = nca->server_type;
    delete nca;
}

void *ClientHandler::handleClientThreadWrapper(void *arg)
{
    NewClientArgs *nca = static_cast<NewClientArgs *>(arg);

    // Create an instance of ClientHandler and call handleClient
    ClientHandler handler(nca);

    if (DEBUG)
    {
        fprintf(stderr, "New client thread (%ld)\n", pthread_self());
    }

    handler.handleClient();

    return nullptr;
}

void *ClientHandler::readerThreadFunction_(void *arg)
{
    // Block all signals from being received to reader threads.
    // ClientHandler will be the only one to receive SIGUSR1.
    sigset_t sigset;
    sigfillset(&sigset);                       // Add all signals to the set
    pthread_sigmask(SIG_BLOCK, &sigset, NULL); // Block all signals for this thread

    // Call reader
    ClientHandler *handler = static_cast<ClientHandler *>(arg);

    if (DEBUG)
    {
        fprintf(stderr, "New reader thread (%ld)\n", pthread_self());
    }

    SocketReader reader(handler->socket_fd, handler->message_queue);
    reader.read_data();
    return nullptr;
}

// Helper function to split string by a delimiter
vector<string> ClientHandler::splitByDelimiter_(const string &str, const string &delimiter)
{
    // In no delimiter is found, return the string as a single element vector
    if (str.find(delimiter) == string::npos)
    {
        return {str};
    }

    vector<string> result;
    size_t pos = 0;
    size_t prev_pos = 0;
    while ((pos = str.find(delimiter, prev_pos)) != string::npos)
    {
        result.push_back(str.substr(prev_pos, pos - prev_pos));
        prev_pos = pos + delimiter.length();
    }
    result.push_back(str.substr(prev_pos)); // Add the last segment
    return result;
}

void ClientHandler::handleClient()
{
    unique_lock lock(mtx);
    THREAD2SOCKET[pthread_self()] = this->socket_fd;
    lock.unlock();

    if (DEBUG)
    {
        fprintf(stderr, "[%d] New connection\n", this->socket_fd);
    }

    SocketWriter writer = SocketWriter(this->socket_fd);

    // TODO: Refactor this to use CommandDispatcher
    string welcome_message;
    switch (this->server_type_)
    {
    case ServerType::SMTP:
        welcome_message = "220 [penncloud]";
        break;
    case ServerType::POP3:
        welcome_message = "+OK POP3 ready [penncloud]";
        break;
    case ServerType::KVSTORE:
        welcome_message = "+OK KVStore ready [penncloud]";
        break;
    case ServerType::COORDINATOR:
        welcome_message = "+OK Coordinator ready [penncloud]";
        break;
    }
    writer.write_message(welcome_message);

    // Start the reader thread to read data using pthread_create. Note how we pass an
    // instance of the current class as an argumen (this) so it can access member variables.
    pthread_create(&this->reader_thread_, nullptr, this->readerThreadFunction_, this);

    ICommandDispatcher *dispatcher = getDispatcher_(this->server_type_);

    // Process the messages in the main clientHandler thread
    while (true)
    {
        // Read from ThreadSafeQueeu or block until a message is available
        string message = this->message_queue.pop();

        if (DEBUG) truncated_print("C:", message, this->socket_fd);

        // Dispatch to corresponding dispatcher and get response message
        DispatcherResponse response = dispatcher->dispatch(message);

        if (response.second != "")
        {
            writer.write_message(response.second);
            if (DEBUG) truncated_print("S:", response.second, this->socket_fd);
        }

        if (response.first == DispatcherStatusCode::QUIT)
        {         
            delete dispatcher;
            this->closeClient_();
            return;
        }
    }
    return;
}

ICommandDispatcher *ClientHandler::getDispatcher_(ServerType server_type)
{
    switch (server_type)
    {
    case ServerType::POP3:
        return new Pop3CommandDispatcher();
    case ServerType::KVSTORE:
        return new KvStorageCommandDispatcher();
    case ServerType::SMTP:
        return new SmtpCommandDispatcher();
    case ServerType::COORDINATOR:
        return new CoordinatorCommandDispatcher();
    }
}

void ClientHandler::closeClient_()
{
    // Gracefully shut down reading and writing
    if (this->socket_fd != -1)
    {
        shutdown(this->socket_fd, SHUT_RDWR);
        close(this->socket_fd);
        if (DEBUG) fprintf(stderr, "[%d] Socket closed\n", this->socket_fd);
    }

    // Wait for reader thread to exit
    pthread_join(this->reader_thread_, NULL);

    if (DEBUG)
    {
        fprintf(stderr, "[%d] Connection Closed\n", this->socket_fd);
    }

    // Clean up the THREAD2SOCKET dictionary
    unique_lock lock(mtx);
    THREAD2SOCKET.erase(pthread_self());
}
