// utils.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
std::string urlDecode(const std::string &s);
std::unordered_map<std::string, std::string> parseFormEncoded(const std::string &body);