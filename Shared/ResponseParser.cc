#include "ResponseParser.h"
#include <regex>

std::string ResponseParser::parse_get(const std::string &response)
{
    std::regex re(R"(((\+OK \d+) )(.*))");
    std::smatch match;
    if (std::regex_search(response, match, re))
    {
        std::string prefix = match[1].str();
        return remove_prefix_(response, prefix);
    }

    fprintf(stderr, "ResponseParser Error: Invalid GET response format\n");
    return "";
}

bool ResponseParser::parse_cput(const std::string &response)
{
    if (response == "+OK 1")
    {
        return true;
    }
    else if (response == "+OK 0")
    {
        return false;
    }

    fprintf(stderr, "ResponseParser Error: Invalid CPUT response format\n");
    return false;
}

std::set<std::string> ResponseParser::parse_list_rows(const std::string &response)
{
    // Empty response case (server has no rows)
    if (response == "+OK")
    {
        return std::set<std::string>();
    }
    // Example: "+OK\r\n02762169579135187400\r\n16594858356450794111\r\n"
    std::vector<std::string> rows = split_by_delimiter_(remove_prefix_(response, "+OK\r\n"), "\r\n");
    return std::set<std::string>(rows.begin(), rows.end());
}
std::set<std::string> ResponseParser::parse_list_columns(const std::string &response)
{
    // Empty response case (row has no columns)
    if (response == "+OK")
    {
        return std::set<std::string>();
    }

    // Example: "+OK\r\nc1\r\nc3\r\n"
    std::vector<std::string> rows = split_by_delimiter_(remove_prefix_(response, "+OK\r\n"), "\r\n");
    return std::set<std::string>(rows.begin(), rows.end());
}

std::size_t ResponseParser::parse_bytes(const std::string &response)
{
    return std::stoul(remove_prefix_(response, "+OK "));
}

std::string ResponseParser::remove_prefix_(const std::string &input, const std::string prefix)
{
    std::string::size_type pos = input.find(prefix);
    if (pos == std::string::npos)
    {
        fprintf(stderr, "ResponseParser Error: Prefix not found in response\n");
        return "";
    }
    return input.substr(pos + prefix.length());
}

std::vector<std::string> ResponseParser::split_by_delimiter_(const std::string &input, const std::string delimiter)
{
    std::regex crlf_re("(" + delimiter + ")");
    std::sregex_token_iterator it(input.begin(), input.end(), crlf_re, -1);
    std::sregex_token_iterator end;

    std::vector<std::string> result;
    while (it != end)
    {
        result.push_back(*it++);
    }
    return result;
}
