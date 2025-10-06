#include "cookie-handler.h"
#include "../../Shared/IStorageService.h"
#include "../../Backend/Globals.h"
#include "../../Shared/SharedStructures.h"
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <iomanip>

std::string getCookie(std::unordered_map<std::string, std::string> cookieHeaders)
{
    auto it = cookieHeaders.find("Cookie");
    if (it == cookieHeaders.end())
        return {"", ""};

    const std::string &cookieHeader = it->second;
    std::istringstream stream(cookieHeader);
    std::string token;

    std::string sessionid;
    std::string username;

    while (std::getline(stream, token, ';'))
    {
        size_t start = token.find_first_not_of(" ");
        if (start == std::string::npos)
            continue;

        size_t eq = token.find('=', start);
        if (eq == std::string::npos)
            continue;

        std::string key = token.substr(start, eq - start);
        std::string value = token.substr(eq + 1);

        if (key == "sessionid")
            sessionid = value;
        else if (key == "username")
            username = value;
    }

    // Might be better to return a bool value; if false redirect to login page.
    // std::cout << "Session ID: " << sessionid << std::endl;

    return sessionid;
}

std::string setCookie(const std::string &username)
{
    // Need a randomly generated session ID for security. and a username kv storage in cookie jar. also remember to set the expiration time.

    // Need to store the cookie to the backend.
    std::string sessionId = generateSessionId();
    std::string duration = "3600"; // 1 hour
    std::ostringstream cookie;
    cookie << "Set-Cookie: sessionid=" << sessionId << "; Path=/; HttpOnly; Max-Age=" << duration << "\r\n";

    std::cout << "Cookie: " << cookie.str() << std::endl;
    // The cookie needs to be stored in the backend.
    IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());

    time_t now = time(nullptr);
    time_t expirationTime = now + std::stoi(duration);

    std::cout << "[SESSION  HASH] " << user_to_row("SESSION") << std::endl;

    int putResult = storage.put(user_to_row("SESSION"), sessionId, IStorageService::from_string(username));
    std::cout << "[COOKIE DEBUG] PUT result: " << putResult << std::endl;
    if (putResult != 0)
    {
        std::cerr << "[ERROR] Failed to store session ID in storage." << std::endl;
        return "";
    }
    // std::cout << "[SESSION] Storing session ID: " << user_to_row("SESSION") << std::endl;
    std::cout << "[SESSION] Storing session ID: " << user_to_row("SESSION") << std::endl;
    // storage.put(user_to_row("SESSION"), sessionId + "-exp", IStorageService::from_string(std::to_string(expirationTime)));

    std::cout << "Cookie set for user: " << username << ", " << cookie.str() << std::endl;
    return cookie.str();
}

std::string generateSessionId()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);  // for hex digits
    std::uniform_int_distribution<> dis2(8, 11); // for variant bits

    std::stringstream ss;
    ss << std::hex;

    for (int i = 0; i < 8; ++i)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; ++i)
        ss << dis(gen);
    ss << "-4"; // UUID version 4
    for (int i = 0; i < 3; ++i)
        ss << dis(gen);
    ss << "-";
    ss << dis2(gen); // UUID variant bits
    for (int i = 0; i < 3; ++i)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; ++i)
        ss << dis(gen);

    return ss.str();
}
