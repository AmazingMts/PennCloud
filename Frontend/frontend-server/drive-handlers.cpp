#include "drive-handlers.h"
#include <sstream>
#include <set>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../Shared/SharedStructures.h"
#include "utils.h"

// std::string urlDecode(const std::string &s)
// {
//     std::string result;
//     char ch;
//     int i, ii;
//     for (i = 0; i < s.length(); i++)
//     {
//         if (int(s[i]) == 37)
//         {
//             sscanf(s.substr(i + 1, 2).c_str(), "%x", &ii);
//             ch = static_cast<char>(ii);
//             result += ch;
//             i = i + 2;
//         }
//         else
//         {
//             result += s[i];
//         }
//     }
//     return result;
// }

void DriveHandler::handle(clientContext *client, const std::string &method, const std::string &path, const std::string &body, std::string username, std::unordered_map<std::string, std::string> header)
{
    if (method == "GET")
    {
        if (path.rfind("/drive/download?", 0) == 0)
        {
            handleDownload(client, path, username);
        }
        else if (path.rfind("/drive/view", 0) == 0)
        {
            handleView(client, path, header, username);
        }
        else
        {
            FrontendServer::sendResponse("<html><body><h1>400 Bad Request</h1></body></html>", client, "400 Bad Request");
        }
    }
    else if (method == "POST")
    {
        if (path.rfind("/drive/upload?", 0) == 0)
        {
            handleUpload(client, path, body, username);
        }
        else if (path.find("/drive/delete") != std::string::npos)
        {
            handleDelete(client, path, username);
        }
        else if (path.find("/drive/rename") != std::string::npos)
        {
            handleRename(client, path, body, username);
        }
    }
}
void DriveHandler::handleRename(clientContext *client, const std::string &path, const std::string &body, const std::string &username)
{
    std::string row = user_to_row(username) + "-STORAGE";
    size_t a = body.find("oldPath");
    size_t b = body.find("newPath");
    if (a != std::string::npos && b != std::string::npos)
    {
        size_t start = a + 10;
        size_t t = body.length() - 1;
        std::string old = body.substr(start, b - a - 13);
        std::string newstring = body.substr(b + 10, t - b - 12);

        while (old.substr(0, 1) == "/")
        {
            old.replace(0, 1, "");
        }
        while (newstring.substr(0, 1) == "/")
        {
            newstring.replace(0, 1, "");
        }
        std::cout << "[Reanme] old is " << old << std::endl;
        std::cout << "[Rename] new is " << newstring << std::endl;

        if (old.size() >= 7 && old.substr(old.size() - 7) == "-folder")
        {
            storage->move(row, old, newstring);
            std::set<std::string> all_file;
            storage->list_columns(row, all_file);
            for (std::string file : all_file)
            {
                if (file.size() > old.size() &&
                    file.substr(0, old.size()) == old &&
                    file[old.size()] == '/')
                {
                    std::string suffix = file.substr(old.size());
                    std::string newFilePath = newstring + suffix;
                    storage->move(row, file, newFilePath);
                }
            }
            std::string html = R"(<html><head><script>alert("Rename successfully!");window.location.href = document.referrer || "/drive/view";</script></head><body></body></html>)";
            FrontendServer::sendResponse(html, client, "200 OK");
        }
        else
        {
            std::set<std::string> before;
            storage->list_columns(row, before);

            // rename main file and all chunked parts
            bool renamedAny = false;
            for (const std::string &col : before)
            {
                if (col == old)
                {
                    if (storage->move(row, old, newstring) == 0)
                        renamedAny = true;
                }
                else if (col.rfind(old + "_part", 0) == 0)
                {
                    std::string suffix = col.substr(old.length()); // e.g. _part0
                    std::string newCol = newstring + suffix;
                    if (storage->move(row, col, newCol) == 0)
                        renamedAny = true;
                }
            }

            if (renamedAny)
            {
                std::string html = R"(<html><head><script>alert("Rename successfully!");window.location.href = document.referrer || "/drive/view";</script></head><body></body></html>)";
                FrontendServer::sendResponse(html, client, "200 OK");
            }
            else
            {
                std::string html = R"(<html><head><script>alert("Rename failed!");window.location.href = document.referrer || "/drive/view";</script></head><body></body></html>)";
                FrontendServer::sendResponse(html, client, "200 OK");
            }
        }

        // std::set<std::string> userFiles;
        // storage->list_columns(row, userFiles);
        // std::cout << "[Rename successfully] Existing files for user " << username << ":\n";
        // for (const auto &file : userFiles)
        // {
        //     std::cout << "  - " << file << "\n";
        // }
        // std::string html = R"(<html><head><script>alert("Rename successfully!");window.location.href = document.referrer || "/user/login";</script></head><body></body></html>)";
        // FrontendServer::sendResponse(html, client, "200 OK");
    }
    else
    {
        std::cout << "lack of Arguement" << std::endl;
    }
}
void DriveHandler::handleDownload(clientContext *client, const std::string &path, const std::string &username)
{
    size_t pos = path.find("filename=");
    if (pos == std::string::npos)
    {
        FrontendServer::sendResponse("<html><body><h1>404 File Not Found</h1></body></html>", client, "404 Not Found");
        return;
    }

    std::string filename = urlDecode(path.substr(pos + 9));
    while (!filename.empty() && filename[0] == '/')
        filename = filename.substr(1);

    std::string row = user_to_row(username) + "-STORAGE";
    std::set<std::string> columns;
    storage->list_columns(row, columns);
    std::cout << "[Download] list_columns for rowkey " << row << ":\n";
    for (const std::string &col : columns)
        std::cout << col << std::endl;

    std::vector<std::string> chunkNames;
    for (const std::string &col : columns)
    {
        if (col.rfind(filename + "_part", 0) == 0) // starts with filename_part
            chunkNames.push_back(col);
    }

    std::string fullContent;

    if (!chunkNames.empty())
    {
        std::sort(chunkNames.begin(), chunkNames.end(), [](const std::string &a, const std::string &b)
                  {
            int numA = std::stoi(a.substr(a.rfind("part") + 4));
            int numB = std::stoi(b.substr(b.rfind("part") + 4));
            return numA < numB; });

        for (const std::string &chunkCol : chunkNames)
        {
            std::vector<std::byte> chunkVal;
            if (storage->get(row, chunkCol, chunkVal) == 0)
                fullContent += IStorageService::to_string(chunkVal);
        }
    }
    else
    {
        std::vector<std::byte> val;
        if (storage->get(row, filename, val) != 0)
        {
            FrontendServer::sendResponse("<html><body><h1>404 File Not Found</h1></body></html>", client, "404 Not Found");
            return;
        }
        fullContent = IStorageService::to_string(val);
    }

    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: application/octet-stream\r\n"
                   << "Content-Disposition: attachment; filename=\"" << filename << "\"\r\n"
                   << "Content-Length: " << fullContent.size() << "\r\n"
                   << "Connection: keep-alive\r\n\r\n";

    std::string header = responseStream.str();
    send(client->conn_fd, header.c_str(), header.size(), 0);
    send(client->conn_fd, fullContent.c_str(), fullContent.size(), 0);
}

void DriveHandler::handleView(clientContext *client, const std::string &path, const std::unordered_map<std::string, std::string> &header, const std::string &username)
{
    std::string accept = header.count("Accept") ? header.at("Accept") : "";
    std::string rowkey = user_to_row(username) + "-STORAGE";
    if (accept.find("application/json") != std::string::npos)
    {
        std::string prefix = "";
        if (path.length() > std::string("/drive/view/").length())
        {
            prefix = path.substr(std::string("/drive/view/").length());
            size_t pos;
            while ((pos = prefix.find("%2F")) != std::string::npos)
                prefix.replace(pos, 3, "/");
            while (!prefix.empty() && prefix[0] == '/')
                prefix = prefix.substr(1);
        }

        std::set<std::string> allFiles;
        std::vector<std::string> currentDirFiles;
        if (storage->list_columns(rowkey, allFiles) == 0)
        {
            size_t prefixLen = prefix.length();
            for (const auto &name : allFiles)
            {
                if (name.substr(0, prefixLen) == prefix)
                {
                    std::string rest = name.substr(prefixLen);
                    if (!rest.empty() && rest[0] == '/')
                        rest = rest.substr(1);
                    if (!rest.empty() && rest.find("/") == std::string::npos)
                        if (rest.size() > 6 && rest.substr(rest.size() - 6) == "_part0")
                        {
                            std::string baseName = rest.substr(0, rest.size() - 6);
                            currentDirFiles.push_back(baseName);
                        }
                        else if (rest.find("_part") == std::string::npos)
                        {
                            currentDirFiles.push_back(rest);
                        }
                }
            }
        }

        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < currentDirFiles.size(); ++i)
        {
            if (i > 0)
                json << ",";
            json << "{ \"name\": \"" << currentDirFiles[i] << "\" }";
        }
        json << "]";

        std::string body = json.str();
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: keep-alive\r\n\r\n"
             << body;
        std::cout << "[Drive View] Folder prefix: \"" << prefix << "\"\n";
        for (const auto &name : currentDirFiles)
        {
            std::cout << "  - " << name << "\n";
        }
        send(client->conn_fd, resp.str().c_str(), resp.str().size(), 0);
    }
    else
    {
        std::string path = "static/drive/drive.html";
        std::string html = FrontendServer::readFile(path);
        FrontendServer::sendResponse(html, client, "200 OK");
    }
}

void DriveHandler::handleUpload(clientContext *client, const std::string &path, const std::string &body, const std::string &username)
{
    size_t pos = path.find("filename=");
    std::string rowkey = user_to_row(username) + "-STORAGE";
    std::cout << "[Upload] rowkey " + rowkey << std::endl;
    std::string filename = (pos != std::string::npos) ? urlDecode(path.substr(pos + 9)) : "";
    while (!filename.empty() && filename[0] == '/')
        filename = filename.substr(1);
    std::cout << "[Upload] column is " << filename << std::endl;
    if (!path.ends_with("-folder"))
    {
        size_t start = body.find("\r\n\r\n") + 4;
        size_t end = body.rfind("------");
        std::string content = body.substr(start, end - start);
        const size_t CHUNK_SIZE = 25 * 1024 * 1024;
        size_t total_size = content.size();
        size_t num_chunks = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

        std::cout << "[Upload] Total size: " << total_size << ", Chunks: " << num_chunks << std::endl;

        for (size_t i = 0; i < num_chunks; ++i)
        {
            size_t chunk_start = i * CHUNK_SIZE;
            size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, total_size);
            std::string chunk = content.substr(chunk_start, chunk_end - chunk_start);
            std::string chunk_col_name = filename + "_part" + std::to_string(i);
            tablet_value value = IStorageService::from_string(chunk);
            int status = storage->put(rowkey, chunk_col_name, value);
            std::cout << "[Upload] Put " << chunk_col_name << ", status: " << status << std::endl;
        }
    }
    else
    {
        tablet_value value;
        value.push_back(static_cast<std::byte>('a'));
        int status = storage->put(rowkey, filename, value);
        std::cout << "[Upload] status is " << status << std::endl;
    }
    std::set<std::string> columns;
    storage->list_columns(rowkey, columns);

    std::cout << "[Upload] ListColumn for user " << rowkey << ":\n";
    for (const std::string &col : columns)
    {
        std::cout << " - " << col << std::endl;
    }

    std::string html = R"(<html><head><script>alert("Upload successfully!");window.location.href = document.referrer || "/user/login";</script></head><body></body></html>)";
    FrontendServer::sendResponse(html, client, "200 OK");
}
void DriveHandler::handleDelete(clientContext *client, const std::string &path, const std::string &username)
{
    size_t pos = path.find("filename=");
    std::string filename = (pos != std::string::npos) ? urlDecode(path.substr(pos + 9)) : "";
    while (!filename.empty() && filename[0] == '/')
        filename = filename.substr(1);

    std::cout << "[Delete] filename is " << filename << std::endl;
    std::string rowKey = user_to_row(username) + "-STORAGE";

    std::set<std::string> columns;
    storage->list_columns(rowKey, columns);

    bool deletedAny = false;

    bool isFolder = filename.size() >= 7 && filename.substr(filename.size() - 7) == "-folder";
    std::string folderPrefix = filename + "/"; // e.g., myfolder/...

    for (const std::string &col : columns)
    {
        bool match = false;
        if (isFolder)
        {
            // Case 1: it is the folder itself
            if (col == filename)
                match = true;

            // Case 2: all files inside the folder or its subfolders
            else if (col.rfind(folderPrefix, 0) == 0)
                match = true;
        }
        else
        {
            // File case: exact file or chunked parts
            if (col == filename || col.rfind(filename + "_part", 0) == 0)
                match = true;
        }
        std::cout<<"[DELETE] Before remove"<<std::endl;
        if (match && storage->remove(rowKey, col) == 0)
        {
            std::cout << "[Delete] Removed column: " << col << std::endl;
            deletedAny = true;
        }
    }

    if (deletedAny)
    {
        std::string html = R"(<html><head><script>alert("Deleted successfully!");window.location.href = document.referrer || "/drive/view";</script></head><body></body></html>)";
        FrontendServer::sendResponse(html, client, "200 OK");
    }
    else
    {
        std::string html = "<html><body><h2>File or Folder Not Found!</h2><a href=\"/drive/view\">Back to Drive</a></body></html>";
        FrontendServer::sendResponse(html, client, "404 Not Found");
    }
}
