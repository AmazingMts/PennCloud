#include "user-handlers.h"
#include "cookie-handler.h"
#include "../../Shared/SharedStructures.h"
#include "utils.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include "common.h"
using namespace std;

void UserHandler::handle(clientContext *client, const std::string &method, const std::string &path, const std::string &body, const std::string &username)
{
    if (method == "GET")
    {
        std::string filepath;
        std::string htmlResp;
        if (path.rfind("/user/login", 0) == 0)
        {
            // std::cout << "Getting login page\n";
            filepath = "static/account/login.html";
            htmlResp = FrontendServer::readFile(filepath);

            // std::string cookie = setCookie(uername);

            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html\r\n"
                     << "Connection: keep-alive\r\n"
                     //  << "Set-Cookie: sessionid=deleted; Max-Age=0; Path=/\r\n" // Don't need to set cookie here. Using incognito mode.
                     << "Content-Length: " << htmlResp.size() << "\r\n\r\n"
                     << htmlResp;

            std::string fullResponse = response.str();
            // std::cout << "Found login html file, and sending response to browser.\n"
            //           << std::endl;
            send(client->conn_fd, fullResponse.c_str(), fullResponse.size(), 0);
            // std::cout << "[DEBUG] Sent response to browser\n"
            //           << std::endl;
        }
        else if (path.rfind("/user/signup", 0) == 0)
        {
            std::cout << "Getting signup page\n";
            filepath = "static/account/signup.html";
            htmlResp = FrontendServer::readFile(filepath);
            std::cout << "Found signup html file\n"
                      << std::endl;
            FrontendServer::sendResponse(htmlResp, client);
        } else if (path.rfind("/user/password", 0) == 0)
        {
            std::cout << "Getting password page\n";
            filepath = "static/account/pw.html";
            htmlResp = FrontendServer::readFile(filepath);
            std::cout << "Found password html file\n"
                      << std::endl;
            FrontendServer::sendResponse(htmlResp, client);
        }
    }

    else if (method == "POST")
    {
        if (path.rfind("/user/login", 0) == 0)
        {
            size_t pos1 = body.find("username");
            size_t pos2 = body.find("&password");
            bool authenticated = false;
            if (pos1 != std::string::npos && pos2 != std::string::npos)
            {
                std::string username = body.substr(pos1 + 9, pos2 - (pos1 + 9));
                std::string password = body.substr(pos2 + 10);

                std::cout << "Parsed username: " << username << std::endl;
                std::cout << "Parsed password: " << password << std::endl;

                // std::cout << "Parsed username: " << username << std::endl;
                // std::cout << "Parsed password: " << password << std::endl;
                tablet_value store_pw;
                std::cout << "[DEBUG] Getting password from storage" << IStorageService::to_string(store_pw) << std::endl;
                // std::cout << user_to_row("ACCOUNTS") << std::endl;
                if (storage->get(user_to_row("ACCOUNTS"), username, store_pw) == 0)
                {
                    // std::cout << "[DEBUG] password stored is " << IStorageService::to_string(store_pw)  << std::endl;
                    std::string ps = IStorageService::to_string(store_pw);
                    // ps = ps.substr(0, ps.size() - 2);
                    std::cout << "[DEBUBG] cleaned password: " << ps << endl;
                    if (ps == password)
                    {
                        authenticated = true;
                    }
                    else
                    {
                        authenticated = false;
                    }
                }

                if (authenticated && username != "admin")
                {
                    std::string path = "static/HomePage.html";
                    std::string htmlResp = FrontendServer::readFile(path);

                    std::string cookie = setCookie(username);

                    std::ostringstream response;
                    std::cout << "[DEBUG] redirecting to home page " << std::endl;
                    response << "HTTP/1.1 302 Found\r\n"
                             << "Location: /home\r\n"
                             << cookie
                             << "Content-Length: 0\r\n"
                             << "Connection: keep-alive\r\n"
                             << "\r\n";
                    std::string fullResponse = response.str();
                    send(client->conn_fd, fullResponse.c_str(), fullResponse.size(), 0);
                }
                else if (authenticated && username == "admin")
                {
                    std::string path = "static/account/admin.html";
                    std::string htmlResp = FrontendServer::readFile(path);
                    std::string cookie = setCookie(username);

                    std::ostringstream response;
                    std::cout << "[DEBUG] redirecting to home page " << std::endl;
                    response << "HTTP/1.1 302 Found\r\n"
                             << "Location: /admin\r\n"
                             << cookie
                             << "Content-Length: 0\r\n"
                             << "Connection: keep-alive\r\n"
                             << "\r\n";
                    std::string fullResponse = response.str();
                    send(client->conn_fd, fullResponse.c_str(), fullResponse.size(), 0);
                }

                else
                {
                    std::string html =
                        "<html><head><script>"
                        "alert('Invalid username or password');"
                        "window.location.href = '/user/login';"
                        "</script></head><body></body></html>";

                    FrontendServer::sendResponse(html, client, "200 OK");
                }
            }
        }
        else if (path.rfind("/user/signup", 0) == 0)
        {
            size_t pos1 = body.find("username");
            size_t pos2 = body.find("&password");
            if (pos1 != std::string::npos && pos2 != std::string::npos)
            {
                std::string username = body.substr(pos1 + 9, pos2 - (pos1 + 9));
                std::string password = body.substr(pos2 + 10);
                std::cout << "Parsed username: " << username << std::endl;
                std::cout << "Parsed password: " << password << std::endl;
                tablet_value store_pw;
                if (storage->get(user_to_row("ACCOUNTS"), username, store_pw) == 0)
                {
                    std::string html =
                        "<html><head><script>"
                        "alert('Username already exists');"
                        "window.location.href = '/user/signup';"
                        "</script></head><body></body></html>";
                    FrontendServer::sendResponse(html, client, "200 OK");
                }
                else
                {
                    storage->put(user_to_row("ACCOUNTS"), username, IStorageService::from_string(password));
                    // Create mailbox and drive for new user.
                    storage->create_row(user_to_row(username) + "-MAILBOX");
                    storage->create_row(user_to_row(username) + "-STORAGE");
                    std::string html =
                        "<html><head><script>"
                        "alert('Account created successfully');"
                        "window.location.href = '/user/login';"
                        "</script></head><body></body></html>";
                    FrontendServer::sendResponse(html, client, "200 OK");
                }
                std::string path = "static/HomePage.html";
                std::string htmlResp = FrontendServer::readFile(path);
                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: text/html\r\n"
                         << "Set-Cookie: sessionid=" << username << "; Path=/\r\n"
                         << "Content-Length: " << htmlResp.size() << "\r\n"
                         << "Connection: keep-alive\r\n"
                         << "\r\n"
                         << htmlResp;
                std::string fullResponse = response.str();
                send(client->conn_fd, fullResponse.c_str(), fullResponse.size(), 0);
            }
            else
            {
                std::string html =
                    "<html><head><script>"
                    "alert('Invalid username or password');"
                    "window.location.href = '/user';"
                    "</script></head><body></body></html>";

                FrontendServer::sendResponse(html, client, "200 OK");
            }
        } else if (path.rfind("/user/password", 0) == 0)
    {
        std::cout << "[DEBUG] Change password page\n";
        std::unordered_map<std::string, std::string> parsedDate = parseFormEncoded(body);
        std::string oldPw = parsedDate["oldPassword"];
        std::string newPw = parsedDate["newPassword"];
        std::string confirmPw = parsedDate["confirmPassword"];


        if (newPw != confirmPw)
        {
            std::string html =
                "<html><head><script>"
                "alert('New password and confirmation do not match');"
                "window.location.href = '/user/password';"
                "</script></head><body></body></html>";
            FrontendServer::sendResponse(html, client, "200 OK");
        } else if (oldPw == newPw)
        {
            std::string html =
                "<html><head><script>"
                "alert('New password cannot be the same as old password');"
                "window.location.href = '/user/password';"
                "</script></head><body></body></html>";
            FrontendServer::sendResponse(html, client, "200 OK");
        }
        else
        {
            // This is a CPUT to KVstorage.
            bool result;
            int res = storage->cput(user_to_row("ACCOUNTS"), username, IStorageService::from_string(oldPw), IStorageService::from_string(newPw), result);
            if (res == 0)
            {
                std::string html =
                    "<html><head><script>"
                    "alert('Password changed successfully');"
                    "window.location.href = '/user/login';"
                    "</script></head><body></body></html>";
                FrontendServer::sendResponse(html, client, "200 OK");
            }
            else
            {
                if (res == 2) {
                    std::string html =
                    "<html><head><script>"
                    "alert('Invalid old password');"
                    "window.location.href = '/user/password';"
                    "</script></head><body></body></html>";
                    FrontendServer::sendResponse(html, client, "400 Bad Request");
                } else {
                    std::string html =
                    "<html><head><script>"
                    "alert('Error changing password. Please try again.');"
                    "window.location.href = '/user/password';"
                    "</script></head><body></body></html>";
                    FrontendServer::sendResponse(html, client, "500 Internal Server Error");
                }
            }

        }
    }
    }
}
