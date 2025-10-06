// lbregister.h
#pragma once
#include <vector>
#include <string>

bool registerWithLoadBalancer(const std::string &loadBalancerIp, int loadBalancerPort, int localPort);
std::vector<std::string> getListfromLb(const std::string &loadBalancerIp, int loadBalancerPort);