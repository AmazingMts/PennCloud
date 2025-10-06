#ifndef COOKIE_HANDLER_H
#define COOKIE_HANDLER_H

/*
Cookie handler main functionality:
1. Set cookie for when user logs in.
2. Check cookie for when user accesses a page.
*/

#include "../../Shared/IStorageService.h"
#include <string>
#include <unordered_map>


    // Set a cookie with the given name and value.
    std::string setCookie(const std::string &username);

    // Get the value of a cookie with the given name.
    std::string getCookie(std::unordered_map<std::string, std::string> cookieHeader);

    // Delete a cookie by setting its max-age to 0.
    std::string deleteCookie(const std::string &name);

    std::string generateSessionId();

    bool isCookieValid(const std::string &sessionId, IStorageService *storage);


#endif // COOKIE_HANDLER_H