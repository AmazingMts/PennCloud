#include "cookie-handler.h"
#include "frontend-server.h"
#include "email-handlers.h"
#include "common.h"
#include "drive-handlers.h"
#include "user-handlers.h"
#include "lbregister.h"
#include "admin-handlers.h"
#include "../../Shared/IStorageService.h"
#include "../../Shared/CoordinatorService.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <set>
#include <algorithm>
#include <arpa/inet.h>
#include <fstream>
#include <unordered_map>
#include <atomic>
#include <signal.h>

// #define PORT 8000
#define MAX_THREADS 10
static std::string loadBalancerIp = "127.0.0.1";
static int loadBalancerPort = 8880;

FrontendServer *globalFrontendServer;
bool DEBUG = false;
std::atomic<bool> isActive(true); // Atomic variable to control the running state of the server
std::atomic<bool> stopRunning(false); // Atomic variable to control the running state of the server

// Need to create an instance of coordinator server.
CoordinatorService *COORDINATOR_SERVICE = nullptr; // Store the service in a global variable

// worker loop
void *workerLoop(void *arg)
{
    FrontendServer *server = static_cast<FrontendServer *>(arg);
    while (!stopRunning.load())
    {
        // Wait for tasks in the queue.
        pthread_mutex_lock(&server->taskQueueMutex); // Acquire the lock before checking the queue.
        while (server->taskQueue.empty() && !stopRunning.load())
        {
            // Wait for a task to be added to the queue.
            // pthread_cond_wait atomically:
            // 1. Unlocks the mutex.
            // 2. Waits for the condition variable to be signaled.
            // 3. Reacquires the mutex before returning.

            // Notes: locks on the queue. This is done.
            pthread_cond_wait(&server->taskQueueCond, &server->taskQueueMutex);
        }

        if (stopRunning.load()) {
            pthread_mutex_unlock(&server->taskQueueMutex);
            break;  // Exit loop if stop is requested
        }

        // Get the task from the queue.
        clientContext *client = server->taskQueue.front();
        server->taskQueue.pop_front();
        printf("Worker thread %lu processing client %d. Now there are %d remaining tasks in the queue.\n", pthread_self(), client->conn_fd, server->taskQueue.size());

        pthread_mutex_unlock(&server->taskQueueMutex);

        server->handleClient(client);
        delete client; // Clean up the client context after handling.
    }
}

FrontendServer::FrontendServer(const std::string &ip, int port)
: serverIp(ip), serverPort(port)
{
    // Initialize thread pool.
    for (int i = 0; i < MAX_THREADS; ++i)
    {
        pthread_t thread;
        pthread_create(&thread, NULL, workerLoop, this);
        threadPool.push_back(thread);
    }
    pthread_mutex_init(&taskQueueMutex, NULL);
    pthread_cond_init(&taskQueueCond, NULL);

    if (registerWithLoadBalancer(loadBalancerIp, loadBalancerPort, serverPort))
    {
        std::cout << "successfully connect to Lb" << std::endl;
    }
    else
    {
        std::cout << "fail to connect to Lb" << std::endl;
        exit(1);
    }
}

FrontendServer::~FrontendServer()
{
    stop();
}

/* Functions below handles browser-server interactions */

void FrontendServer::run()
{
    // Create a thread running coordinator service.
    // This thread will be running in the background.
    pthread_t coordinatorThread;
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

    int serverSocket, clientSocket;
    sockaddr_in serverAddr, clientAddr;
    socklen_t serverSize = sizeof(serverAddr);

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("Socket failed");
        exit(1);
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(serverPort);

    int optval = 1;
    int ret = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (ret < 0)
    {
        perror("Failed to set socket options\n");
        close(1);
    }

    // Bind to the server socket and listen for incoming connections.
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, serverSize) < 0)
    {
        perror("bind failed");
        exit(1);
    };
    listen(serverSocket, 20);
    while (true)
    {
        // Accept connection from client and fills in clientAddr with client's ip and port.
        // if (DEBUG)
        //     std::cout << "[DEBUG] Listening for client connection..." << std::endl;
        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &serverSize);
        if (clientSocket < 0)
        {
            perror("Accept failed");
            exit(1);
        }
        // if (DEBUG)
        //     std::cout << "[DEBUG] Accepted connection from client." << std::endl;

        // Create a new client context struct to pass to the thread.
        clientContext *client = new clientContext;
        client->conn_fd = clientSocket;
        client->clientAddr = clientAddr;
        client->requestBuffer.clear();
        client->responseBuffer.clear();
        std::cout << "[" << client->conn_fd << "] Accepted connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "\n";

        // Add the client context to the task queue.

        pthread_mutex_lock(&taskQueueMutex); // Acquire the lock before accessing the queue.
        taskQueue.push_front(client);
        std::cout << "[" << client->conn_fd << "] Client added to task queue. Now there are " << taskQueue.size() << " tasks in queue\n";
        pthread_mutex_unlock(&taskQueueMutex); // Release the lock after accessing the queue.
        pthread_cond_signal(&taskQueueCond);   // Signal the worker thread that a new task is available.
    }
    close(serverSocket);
}

/* Thread function that listens for incoming messages. */
void FrontendServer::handleClient(clientContext *client)
{
    char buffer[8192] = {0};
    while (true)
    {
        memset(buffer, 0, sizeof(buffer)); // at start of each loop

        std::string request;

        ssize_t bytesRead = read(client->conn_fd, buffer, sizeof(buffer));
        if (bytesRead < 0)
        {
            perror("Read failed");
            break;
        }
        else if (bytesRead == 0)
        {
            std::cout << "Client closed connection\n";
            break;
        }

        request = std::string(buffer, bytesRead);

        /* UP and DOWN only come from admin console to shut down or revive the frontend server. */
        if (request.rfind("DOWN", 0) == 0)
        {
            std::cout << "Received DOWN command from client.\n";
            isActive.store(false);
            break;
        }
        else if (request.rfind("UP", 0) == 0)
        {
            std::cout << "Received UP command from client.\n";
            isActive.store(true);
            // break;
        }


        /*
        Check if the server is active.
        If not, send a 503 Service Unavailable response and close the connection.
        This is to prevent clients from sending requests when the server is down.
        If yes, then continue to process the request.
        */
        if (!isActive.load())
        {
            std::cout << "Server is inactive. Closing client connection.\n";

            std::string httpResponse =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type:text/plain\r\n"
                "Content-Length:30\r\n"
                "\r\n"
                "Server is currently down.\n";
            write(client->conn_fd, httpResponse.c_str(), httpResponse.length());
            close(client->conn_fd);
            return;
        }

        // Parse the request line.
        std::istringstream requestStream(request);
        std::string requestLine;
        std::getline(requestStream, requestLine);
        std::string method, path, httpVersion;

        std::istringstream requestLineStream(requestLine);
        requestLineStream >> method >> path >> httpVersion;
        // Route the request to the appropriate handler.
        // Read Header
        std::string line;
        std::unordered_map<std::string, std::string> header;
        while (std::getline(requestStream, line) && line != "\r")
        {
            // std::cout << "Header: " << line << "\n";
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t colonPos = line.find(": ");
            if (colonPos != std::string::npos)
            {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 2);
                header[key] = value;
            }
        }

        // Read Body
        // deal with small file,file smaller than 10M could directly send to Router
        auto itCL = header.find("Content-Length");
        auto itTE = header.find("Transfer-Encoding");
        std::string requestBody;
        if (itCL != header.end())
        {
            try
            {
                int contentLength = std::stoi(itCL->second);
                size_t headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos)
                {
                    std::string bodyHead = request.substr(headerEnd + 4);
                    requestBody = bodyHead;
                    int totalRead = bodyHead.size();
                    char buffer[4096];
                    while (totalRead < contentLength)
                    {
                        int bytes = recv(client->conn_fd, buffer, std::min<int>(sizeof(buffer), contentLength - totalRead), 0);
                        if (bytes <= 0)
                            break;
                        requestBody.append(buffer, bytes);
                        totalRead += bytes;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Invalid Content-Length: " << itCL->second << " (" << e.what() << ")\n";
            }
        }
        else if (itTE != header.end() && itTE->second == "chunked")
        {
            // chunked transfer logic
            char buffer[4096];
            requestBody.clear();
            while (true)
            {
                std::string line;
                char ch;
                while (recv(client->conn_fd, &ch, 1, 0) > 0)
                {
                    if (ch == '\r')
                        continue;
                    if (ch == '\n')
                        break;
                    line += ch;
                }
                int chunkSize = std::stoi(line, nullptr, 16);
                if (chunkSize == 0)
                    break;

                int totalRead = 0;
                while (totalRead < chunkSize)
                {
                    int bytes = recv(client->conn_fd, buffer, std::min<int>(sizeof(buffer), chunkSize - totalRead), 0);
                    if (bytes <= 0)
                        break;
                    requestBody.append(buffer, bytes);
                    totalRead += bytes;
                }

                recv(client->conn_fd, buffer, 2, 0);
            }
        }

        routeRequests(method, path, requestBody, header, client);
        // return NULL;
    }

    close(client->conn_fd);
    // delete client;
}

/*
Route the request to the appropriate handler based on the path.
If session id is not found, redirect to login page.
*/
void FrontendServer::routeRequests(
    std::string method,
    std::string path,
    std::string body,
    std::unordered_map<std::string, std::string> header,
    clientContext *client)
{
    /* Step 1: Check if the request carries a cookie. Except for the login page, if no cookie, redirect to log in page.*/
    if (!COORDINATOR_SERVICE)
    {
        std::cerr << "Coordinator service is not initialized.\n";
        return;
    }
    if (DEBUG)
        std::cout << "[DEBUG]" << COORDINATOR_SERVICE->get_kv_servers_map().size() << std::endl;
    IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());

    for (const auto &pair : header)
    {
        std::cout << pair.first << ": " << pair.second << std::endl;
    }
    bool hasCookie = (header.find("Cookie") != header.end());



    std::string sessionid;
    std::string username;
    std::string password;

    // If cookie is found, and this is not the login page, get the sessionid and username from the cookie.
    if (hasCookie && path.rfind("/user/login") == std::string::npos)
    {
        std::cout << "Cookie found, extracting sessionid and username.\n";
        sessionid = getCookie(header);
        // client->username = username;
        // std::cout << "Username from cookie: " << username << std::endl;
        tablet_value temp;
        // This is getting username by sessionid.
        storage.get(user_to_row("SESSION"), sessionid, temp);
        username = IStorageService::to_string(temp);
        std::cout << "[DEBUG] Username from sessionid: " << username << std::endl;



        tablet_value pw;
        storage.get(user_to_row("ACCOUNTS"), username, pw);
        // client->pw = IStorageService::to_string(pw);
        std::cout << "[DEBUG] GET " << user_to_row("ACCOUNTS") << " " << username << " " << std::endl;
        password = IStorageService::to_string(pw);
    }

    if (path.rfind("/email", 0) == 0)
    {
        if (!hasCookie)
        {
            std::cout << "No cookie found, redirecting to login page.\n";
            std::string filepath = "static/accout/login.html";
            std::string htmlResp = FrontendServer::readFile(filepath);
            FrontendServer::sendRedirectResponse(client, "/user/login", htmlResp);
        }
        else
        {
            std::cout << "[STEP 1] Route to email handler.\n";
            handleEmailRequests(client, method, path, body, &storage, username, password);
        }
    }
    else if (path.rfind("/drive", 0) == 0)
    {
        DriveHandler handler(&storage);
        auto it = header.find("Cookie");
        if (it != header.end())
        {
            handler.handle(client, method, path, body, username, header);
        }
        else
        {
            std::string html = R"(
                <html>
                <head>
                <meta http-equiv="refresh" content="2;url=/account/login" />
                </head>
                <body>
                <h2>Please login before visiting this page.</h2>
                <p>Redirecting to login...</p>
                </body>
                </html>
                )";

            std::ostringstream resp;
            resp << "HTTP/1.1 401 Unauthorized\r\n"
                 << "Content-Type: text/html\r\n"
                 << "Content-Length: " << html.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << html;

            std::string fullResp = resp.str();
            send(client->conn_fd, fullResp.c_str(), fullResp.size(), 0);
            return;
        }
    }
    else if (path.rfind("/user", 0) == 0)
    {
        UserHandler handler(&storage);
        handler.handle(client, method, path, body, username);
    }
    else if (path.rfind("/home", 0) == 0)
    {
        std::string filepath = "static/HomePage.html";
        std::string htmlResp = FrontendServer::readFile(filepath);
        FrontendServer::sendResponse(htmlResp, client, "200 OK");
    }
    else if (path.rfind("/logout", 0) == 0)
    {
        std::cout << "[DEBUG] Logging out" << std::endl;
            // Revokes the session by setting the cookie to an empty value.
            std::string cookie = "Set-Cookie: sessionid=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/; HttpOnly\r\n";
            std::string response = "HTTP/1.1 302 Found\r\n"
                                   "Location: /user/login\r\n"
                                   + cookie +
                                   "Content-Length: 0\r\n"
                                   "Connection: keep-alive\r\n"
                                   "\r\n";
            send(client->conn_fd, response.c_str(), response.length(), 0);
    }
    else
    {
        sendResponse("404 Not Found", client, "404 Not Found");
    }
    return;
}

void FrontendServer::sendResponse(std::string response, clientContext *client, std::string statusCode)
{
    // Send response to client.
    // Need to know the client socket.
    // Might need different response message types for different status codes.
    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 " << statusCode << "\r\n"
                   << "Content-Type: text/html\r\n"
                   << "Content-Length: " << response.length() << "\r\n"
                   << "Connection: keep-alive\r\n"
                   << "\r\n"
                   << response;

    std::string resp = responseStream.str();
    send(client->conn_fd, resp.c_str(), resp.length(), 0);
}

void FrontendServer::sendHeadResponse(clientContext *client, ssize_t contentLength, std::string statusCode)
{
    // Send HEAD response to client.
    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 " << statusCode << "\r\n"
                   << "Content-Type: text/html\r\n"
                   << "Content-Length: " << contentLength << "\r\n"
                   << "Connection: keep-alive\r\n"
                   << "\r\n";

    std::string resp = responseStream.str();
    send(client->conn_fd, resp.c_str(), resp.length(), 0);
}

std::string FrontendServer::readFile(std::string &filepath)
{
    // Read file from disk.
    std::ifstream file(filepath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void FrontendServer::formatRequest(std::string method, std::string path, std::string body, std::string &request)
{
    // Format the request to send to the backend server.
    request = method + " " + path + " HTTP/1.1\r\n";
    request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    request += "\r\n" + body;
}

void FrontendServer::stop()
{
    std::cout << "[Shutdown] Cleaning up Frontend Server..." << std::endl;
    // 1. Set the stopRunning flag to true
    stopRunning.store(true);
    // 2. Wake up all sleeping threads
    pthread_mutex_lock(&taskQueueMutex);
    pthread_cond_broadcast(&taskQueueCond);  // Wake up all threads
    pthread_mutex_unlock(&taskQueueMutex);

    // 3. Join all threads
    for (pthread_t &thread : threadPool)
    {
        pthread_join(thread, nullptr);
    }
    // Clean up resources and close the server.
    for (auto &client : clientMap)
    {
        printf("Closing client connection: %d\n", client.second->conn_fd);
        close(client.second->conn_fd);
        delete client.second;
    }
    clientMap.clear();
    // 5. Close coordinator socket
    if (coordinatorSocket > 0) {
        close(coordinatorSocket);
    }

    // 6. Destroy mutex and cond
    pthread_mutex_destroy(&taskQueueMutex);
    pthread_cond_destroy(&taskQueueCond);

    std::cout << "[Shutdown] Frontend Server stopped cleanly." << std::endl;
}

void FrontendServer::sendRedirectResponse(clientContext *client, const std::string &location, const std::string &htmlResponse)
{
    // Send a redirect response to the client.
    std::ostringstream response;
    response << "HTTP/1.1 302 Found\r\n"
             << "Location: " << location << "\r\n"
             << "Content-Length: 0\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << htmlResponse;
    send(client->conn_fd, response.str().c_str(), response.str().size(), 0);
}

// Handle Ctrl+C signal to close the server gracefully.
void signalHandler(int signum)
{
    if (globalFrontendServer)
    {
        globalFrontendServer->stop();
    }
    std::cout << "Interrupt signal (" << signum << ") received. Exiting...\n";
    exit(signum);
}

int main(int argc, char* argv[])
{
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);

    if (argc != 4 || std::string(argv[1]) != "-p") {
        std::cerr << "Usage: " << argv[0] << " -p <index> <coordinator-service-port>\n";
        return 1;
    }

    int cs_port = std::stoi(argv[3]);
    COORDINATOR_SERVICE = new CoordinatorService(loadBalancerIp, cs_port, true);

    int index = std::stoi(argv[2]);

    std::ifstream configFile("fe_config.cfg");
    if (!configFile.is_open()) {
        std::cerr << "Failed to open fe_config.cfg\n";
        return 1;
    }

    std::vector<std::pair<std::string, int>> serverList;
    std::string line;

    while (getline(configFile, line)) {
        std::istringstream iss(line);
        std::string ip;
        int port;
        if (iss >> ip >> port) {
            serverList.emplace_back(ip, port);
        }
    }

    if (index < 0 || index >= serverList.size()) {
        std::cerr << "Invalid index: " << index << "\n";
        return 1;
    }

    std::string serverIp = serverList[index].first;
    int serverPort = serverList[index].second;

    FrontendServer server(serverIp, serverPort);
    globalFrontendServer = &server;
    server.run();
    return 0;
}