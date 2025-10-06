#include "Globals.h"
#include <shared_mutex> // Required for shared_mutex

int PORT_NO = -1;
std::string HOST = "";
std::shared_mutex mtx;
std::unordered_map<pthread_t, int> THREAD2SOCKET;
std::atomic<int> SS_FD(-1);
bool DEBUG = false;
std::mutex coord_service_created_mtx;
std::condition_variable COORD_SERVICE_CREATED_CV;
CoordinatorService *COORDINATOR_SERVICE = nullptr;
std::shared_mutex coord_kv_servers_map_mtx;
std::map<int, std::vector<KVServer>> COORD_KV_SERVERS_MAP;
std::atomic<bool> IS_ALIVE(true);
UpdateForwarder *UPDATE_FORWARDER = nullptr;
std::shared_mutex pipe_map_mutex;
std::map<string, int> PIPE_MAP;
std::atomic<bool> RUN_KV_PRIMARY_THREAD(true);
std::atomic<bool> RUN_COORDINATOR_SERVICE(true);
int RECOVERY_PIPE_R = -1;
int RECOVERY_PIPE_W = -1;

int SERVERS_PER_RG = 3;
int RG_ID = -1;
int SERVER_ID = -1;
int SERVER_COUNT = -1;
