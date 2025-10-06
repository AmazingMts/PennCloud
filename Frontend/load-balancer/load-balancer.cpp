#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <arpa/inet.h>
#include <mutex>
#include <vector>
#include <algorithm>

using namespace std;

struct FEServer
{
    int port;
    string ip;
    string addr;
    FEServer(int p, string i) : port(p), ip(i)
    {
        addr = ip + ":" + to_string(port);
    }
    string status = "active";
};

vector<FEServer> frontendServers;

vector<string> frontendServerList;
int currentIndex = 0;
mutex indexLock;
std::string dumpNodes()
{
    lock_guard<mutex> lock(indexLock);
    std::string list;
    for (int i = 0; i < frontendServerList.size(); i++)
    {
        list = list + frontendServerList[i] + "\r";
    }
    return list;
}
void heartBeating()
{
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(5));
        vector<string> toRemove;
        vector<thread> threads;
        mutex removeLock;

        {
            lock_guard<mutex> lock(indexLock);
            // for (const auto &addr : frontendServerList)
            // {
            //     threads.emplace_back([addr, &toRemove, &removeLock]()
            //                          {
            //         size_t pos = addr.find_last_of(':');
            //         if (pos == string::npos) return;
            //         string ip = addr.substr(0, pos);
            //         int port = stoi(addr.substr(pos + 1));
            //         int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            //         sockaddr_in frontAddr{};
            //         frontAddr.sin_family = AF_INET;
            //         frontAddr.sin_port = htons(port);
            //         inet_pton(AF_INET, ip.c_str(), &frontAddr.sin_addr);
            //         if (connect(sockfd, (sockaddr*)&frontAddr, sizeof(frontAddr)) < 0) {
            //             cerr << "Failed to connect to: " << addr << endl;
            //             lock_guard<mutex> g(removeLock);
            //             toRemove.push_back(addr);
            //             close(sockfd);
            //             return;
            //         }
            //         close(sockfd); });
            // }

            for (const auto &it: frontendServers)
            {
                FEServer copy = it;
                threads.emplace_back([copy, &toRemove, &removeLock]() {
                    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in frontAddr{};
                    frontAddr.sin_family = AF_INET;
                    frontAddr.sin_port = htons(copy.port);
                    inet_pton(AF_INET, copy.ip.c_str(), &frontAddr.sin_addr);
                    if (connect(sockfd, (sockaddr*)&frontAddr, sizeof(frontAddr)) < 0) {
                        cerr << "Failed to connect to: " << copy.addr << endl;
                        lock_guard<mutex> g(removeLock);
                        toRemove.push_back(copy.addr);
                        close(sockfd);
                        return;
                    }
                    close(sockfd);
                });
            }
        }
        for (auto &t : threads)
        {
            if (t.joinable())
                t.join();
        }
        {
            lock_guard<mutex> lock(indexLock);
            for (const string &bad : toRemove)
            {
                frontendServers.erase(remove_if(frontendServers.begin(), frontendServers.end(), [&](const FEServer &server) {
                    return server.addr == bad;
                }), frontendServers.end());
                // frontendServerList.erase(remove(frontendServerList.begin(), frontendServerList.end(), bad), frontendServerList.end());
                cout << "Removed dead frontend: " << bad << endl;
            }
        }
    }
}

string getnextIndex()
{
    std::lock_guard<std::mutex> lock(indexLock);
    int n = frontendServers.size();
    if (n == 0) return "";

    int attempts = n;
    while (attempts-- > 0) {
        FEServer& server = frontendServers[currentIndex % n];
        currentIndex = (currentIndex + 1) % n;
        if (server.status == "active") {
            std::cout << "Selected server: " << server.addr << std::endl;
            return server.addr;
        }
    }

    return "";  // No active server found
    // std::lock_guard<std::mutex> lock(indexLock);
    // if (frontendServers.empty()) return "";

    // int attempts = 0;
    // int size = frontendServers.size();

    // while (attempts < size) {
    //     const FEServer& candidate = frontendServers[currentIndex % size];
    //     currentIndex = (currentIndex + 1) % size;
    //     if (candidate.status == "active") {
    //         return candidate.addr;
    //     }
    //     ++attempts;
    // }

    // // No active server found
    // return "";
}

void sendControlCmd(const std::string& ip, int port, const std::string& cmd)
{
    // Logic to send control command to the frontend server
    cout << "Sending control command: " << cmd << endl;
    // Implement the logic to send the control command here
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect to FE control port");
        return;
    }

    send(sockfd, cmd.c_str(), cmd.size(), 0);
    cout << "Sent control command: " << cmd << endl;
    close(sockfd);
}

void killFrontendServer(string addr)
{
    // Logic to kill the frontend server
    cout << "Killing frontend server: " << addr << endl;

    // Implement the logic to kill the frontend server here

    /* If a fe server is killed, mark it as down. Stop directing to the server. */
    for (auto &it : frontendServers)
    {
        cout << it.addr << endl;
        if (it.addr == addr)
        {
            cout << "Found server to kill: " << addr << endl;
            it.status = "down";
            sendControlCmd(it.ip, it.port, "DOWN");
            cout << "Marked frontend server as down: " << addr << endl;
            break;
        }
    }
    // cout << "Server " << addr << " is not found." << endl;

    // Don't need to remove the server from the list.
    // Also need to tell the fe server to stop handling requests.
}

void reactivateFrontendServer(string addr)
{
    // Logic to reactivate the frontend server
    cout << "Reactivating frontend server: " << addr << endl;
    // Implement the logic to reactivate the frontend server here

    /* If a fe server is reactivated, mark it as active. Start directing to the server. */
    for (auto &it : frontendServers)
    {
        cout << it.addr << endl;
        if (it.addr == addr)
        {
            cout << "Found server to reactivate: " << addr << endl;
            it.status = "active";
            sendControlCmd(it.ip, it.port, "UP");
            cout << "Marked frontend server as active: " << addr << endl;
            break;
        }
    }
    //  cout << "Server " << addr << " is not found." << endl;
    // sendControlCmd(addr, 8880, "UP");
}



bool handleRegister(string request)
{
    size_t position = request.find("/register?");
    if (position == std::string::npos)
        return false;

    size_t ipPos = request.find("addr=", position);
    if (ipPos == std::string::npos)
        return false;

    ipPos += 5;  // Skip past "addr="
    size_t end = request.find_first_of(" &\r\n", ipPos);
    if (end == std::string::npos)
        end = request.length();

    std::string fullAddr = request.substr(ipPos, end - ipPos);

    // Strip "http://" prefix if present
    if (fullAddr.rfind("http://", 0) == 0)
        fullAddr = fullAddr.substr(7);

    size_t colon = fullAddr.find(':');
    if (colon == std::string::npos)
        return false;

    std::string ip = fullAddr.substr(0, colon);
    int port;
    try {
        port = std::stoi(fullAddr.substr(colon + 1));
    } catch (...) {
        std::cerr << "Invalid port in address: " << fullAddr << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(indexLock);

    for (const auto& it : frontendServers) {
        if (it.ip == ip && it.port == port) {
            std::cout << "Already registered: " << fullAddr << std::endl;
            return false;
        }
    }

    FEServer server(port, ip);
    frontendServers.push_back(server);
    std::cout << "Registered frontend server: " << server.addr << std::endl;

    return true;
    // size_t position = request.rfind("/register?");
    // if (position != string::npos)
    // {
    //     size_t ipPosit = request.rfind("addr=");
    //     if (ipPosit == string::npos)
    //         return false;
    //     ipPosit += 5;

    //     size_t end = request.find_first_of(" &\r\n", ipPosit);
    //     if (end == string::npos)
    //         end = request.length();

    //     string addr = request.substr(ipPosit, end - ipPosit);

    //     lock_guard<mutex> lock(indexLock);

    //     FEServer server(8880, addr);
    //     for (const auto &it : frontendServers)
    //     {
    //         if (it.addr == addr)
    //         {
    //             cout << "Already registered: " << addr << endl;
    //             return false;
    //         }
    //     }
    //     frontendServers.push_back(server);
    //     // frontendServerList.push_back(addr);
    //     return true;
    // }
    // return false;
}
void readClient(int clientSocket)
{
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    read(clientSocket, buffer, sizeof(buffer) - 1);
    cout << "Received Request:\n"
         << buffer << endl;
    string request(buffer);
    if (request.rfind("GET /nodes", 0) == 0)
    {
        std::string body = dumpNodes();
        std::cout<<"[LB] body is "<<body<<std::endl;
        write(clientSocket, body.c_str(), body.size());
        close(clientSocket);
        return;
    } else if (request.rfind("UP", 0) == 0)
    {
        size_t pos = request.find(" ");
        if (pos != string::npos)
        {
            string addr = request.substr(pos + 1);
            addr.erase(std::remove(addr.begin(), addr.end(), '\n'), addr.end());
            addr.erase(std::remove(addr.begin(), addr.end(), '\r'), addr.end());
            reactivateFrontendServer(addr);
        }
    } else if (request.rfind("DOWN", 0) == 0)
    {
        size_t pos = request.find(" ");
        if (pos != string::npos)
        {
            string addr = request.substr(pos + 1);
            addr.erase(std::remove(addr.begin(), addr.end(), '\n'), addr.end());
            addr.erase(std::remove(addr.begin(), addr.end(), '\r'), addr.end());
            killFrontendServer(addr);
        }
    } else {
        bool registered = handleRegister(request);
    string httpResponse;
    if (registered)
    {
        // Just close the socket silently, without sending any response
        close(clientSocket);
        return;
    }

    if (frontendServers.size() == 0)
    {
        httpResponse =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Content-Type:text/plain\r\n"
            "Content-Length:30\r\n"
            "\r\n"
            "No frontend server available.\n";
        write(clientSocket, httpResponse.c_str(), httpResponse.length());
        close(clientSocket);
        return;
    }
    string frontServer = getnextIndex();
    cout << "redirected url  " + frontServer + "/user/login" << endl;
    // httpResponse =
    //     "HTTP/1.1 302 Found\r\n"
    //     "Location: " +
    //     frontServer + "/user/login" + "\r\n"
    //                                   "Content-Length:0 \r\n"
    //                                   "\r\n";
    httpResponse =
    "HTTP/1.1 302 Found\r\n"
    "Location: http://" + frontServer + "/user/login\r\n"
    "Content-Length: 0\r\n"
    "\r\n";
    write(clientSocket, httpResponse.c_str(), httpResponse.length());
    cout << "Respond" << httpResponse << endl;
    close(clientSocket);
    }
}



void connectToAdminServer()
{
    // Connect to the admin server
    int adminSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (adminSocket < 0)
    {
        perror("socket failed");
        exit(1);
    }
    sockaddr_in adminAddr;
    memset(&adminAddr, 0, sizeof(adminAddr));
    adminAddr.sin_family = AF_INET;
    adminAddr.sin_port = htons(8080);
    adminAddr.sin_addr.s_addr = INADDR_ANY;

    if (::connect(adminSocket, (struct sockaddr *)&adminAddr, sizeof(adminAddr)) < 0)
    {
        perror("connect failed");
        exit(1);
    }

    // Waiting for the admin server to send which fe server to kill/reactivate.
    ssize_t bytesRead;
    char buffer[1024];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        bytesRead = read(adminSocket, buffer, sizeof(buffer) - 1);
        if (bytesRead < 0)
        {
            perror("read failed");
            break;
        }
        else if (bytesRead == 0)
        {
            cout << "Admin server closed connection" << endl;
            break;
        }
        buffer[bytesRead] = '\0';
        string body(buffer);
        cout << "Received command from admin server: " << body << endl;

        size_t actionPos = body.find("\"action\"");
        size_t addrPos = body.find("\"addr\"");

        if (actionPos == std::string::npos || addrPos == std::string::npos) return;

        size_t actionStart = body.find(":", actionPos) + 2;
        size_t actionEnd = body.find("\"", actionStart);
        std::string action = body.substr(actionStart, actionEnd - actionStart);

        size_t addrStart = body.find(":", addrPos) + 2;
        size_t addrEnd = body.find("\"", addrStart);
        std::string addr = body.substr(addrStart, addrEnd - addrStart);



        // Parse command to check command and address.
        if (action == "DOWN")
        {
            // size_t pos = command.find(" ");
            // if (pos != string::npos)
            // {
            //     string addr = command.substr(pos + 1);
            //     killFrontendServer(addr);
            // }
            killFrontendServer(addr);
            cout << "Killed frontend server: " << addr << endl;
        }
        else if (action == "UP")
        {
            // size_t pos = command.find(" ");
            // if (pos != string::npos)
            // {
            //     string addr = command.substr(pos + 1);
            //     reactivateFrontendServer(addr);
            // }
            reactivateFrontendServer(addr);
            cout << "Reactivated frontend server: " << addr << endl;
        }
    }
    close(adminSocket);
    cout << "Admin server connection closed" << endl;
}



int main()
{
    // first receive browser's http request and response echo
    // build connection between browser
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("socket failed");
        exit(1);
    }
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8880);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    if (::listen(serverSocket, 20) < 0)
    {
        perror("listen failed");
        exit(1);
    }
    cout << "running!" << endl;
    thread(heartBeating).detach();
    while (true)
    {
        sockaddr_in clientAddr;
        socklen_t clientSize = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientSize);
        thread(readClient, clientSocket).detach();
    }

    close(serverSocket);
    return 0;
}
