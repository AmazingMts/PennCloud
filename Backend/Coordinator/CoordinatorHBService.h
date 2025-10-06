#include <map>
#include <vector>
#include "../../Shared/SharedStructures.h"
#include <../Globals.h>

class CoordinatorHBService
{
public:
    CoordinatorHBService(std::map<int, std::vector<KVServer>> &kv_servers_map);
    ~CoordinatorHBService();

    // Tries to connect to the KVServers, if it fails it will update
    // the is_alive flag to false and also update the is_primary flag
    void start();

private:
    std::map<int, std::vector<KVServer>> &kv_servers_map_;
};