#pragma once

#include <filesystem>
#include <atomic>
#include <list>
#include "Tablet.h"
#include "TabletLogger.h"
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

class TabletArray;
extern TabletArray tablets;

// Set maximum size for tablet (200 MB)
constexpr size_t MAX_TABLET_SIZE{200ul * 1000ul * 1000ul};
// Maximum number of tablets available in memory
constexpr int MAX_IN_MEMORY{5};
// Maximum size of value (30 MB)
constexpr size_t MAX_VALUE_SIZE{30ul * 1000ul * 1000ul};
// Maximum size of a row (150 MB)
constexpr size_t MAX_ROW_SIZE{150ul * 1000ul * 1000ul};
// Frequency of checkpointing (in version updates)
constexpr size_t CHECKPOINT_FREQUENCY{10};

// Class that provides a mutex that can safely be upgraded from shared to exclusive
class upgradable_mutex
{
public:
    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
    void lock_shared() { mutex.lock_shared(); }
    void unlock_shared() { mutex.unlock_shared(); }
    int try_upgrade()
    {
        bool expected{false};
        bool desired{true};
        if (!upgrading.compare_exchange_strong(expected, desired))
            return 1;
        mutex.unlock_shared();
        mutex.lock();
        upgrading = false;
        return 0;
    }

private:
    std::shared_mutex mutex;
    std::atomic<bool> upgrading{false};
};

// Struct for keeping track of log and checkpoint versions
struct CheckpointFiles
{
    size_t log{0};
    size_t tblt{0};
};

struct TabletInfo
{
    // Actual tablet
    Tablet tablet;
    // Counter representing last activity on tablet
    // Set to MAX_SIZE_T if tablet is not in memory
    size_t in_memory{0};
    // Size of the tablet
    std::atomic<size_t> size{0};
};

struct TabletSortInfo
{
    size_t index;
    std::string first_row_key;
};

class TabletLogger;

// Class that implements the array of tablets
class TabletArray
{
private:
    // Activity counter for tablets to know which ones have been recently used
    std::atomic<size_t> activity_counter{0};
    // Lock for the entire tablet array
    upgradable_mutex tablets_mutex;

    // List of tablets
    std::list<TabletInfo> tablet_list;
    // Vector of information about tablets that can be sorted by first row key
    std::vector<TabletSortInfo> tablet_sort_info;
    // Vector of loggers for each tablet
    std::vector<TabletLogger> loggers;

    // Working path for the tablet array
    std::filesystem::path work_path;
    // Initialization path for the tablet array
    std::filesystem::path init_path;

    // Internal buffer for reading from pipes
    std::string pipe_buffer;

    // Get tablet id from row key to know which tablet to access
    size_t get_tablet_id(const std::string &row_key);
    // Convenience function to get the file prefix for a tablet
    inline std::string tablet_file(size_t tablet_id, size_t version) const
    {
        return std::to_string(SERVER_ID) + "_" + std::to_string(tablet_id) + "_" + std::to_string(version);
    }
    // Convenience function to get the number of tablets in memory
    inline size_t active_tablet_count() const
    {
        return std::count_if(tablet_list.begin(), tablet_list.end(),
                             [](const TabletInfo &tablet)
                             { return tablet.in_memory != std::numeric_limits<size_t>::max(); });
    }
    // Function for saving checkpoint files every 10 write updates
    void initiate_checkpoint(const std::list<TabletInfo>::iterator &tablet_it, const size_t tablet_id);
    // Save checkpoint files to disk and clean up old log and checkpoint files
    void save_checkpoint(const std::list<TabletInfo>::iterator &tablet_it, const size_t tablet_id);
    // Save tablet that hasn't been used for the longest time to disk
    size_t cache_inactive();
    // Atomically check if adding a value to the tablet size would exceed the maximum size
    bool atomic_size_check(const size_t &value_size, std::atomic<size_t> &tablet_size);
    // Split a tablet into two tablets if the size exceeds the maximum size
    int handle_tablet_split(size_t &tablet_id, std::list<TabletInfo>::iterator &tablet_it,
                            const size_t &value_size, const std::string &row_key, const bool tablet_on_disk);
    // Convenience function to get the first hash of replication group
    size_t get_first_row_hash() const;

    // Helper function to read a string until <CLRF> from a file descriptor (here: a pipe)
    void reader(int fd, std::string &message);
    // Helper function to read a number of bytes from a file descriptor (here: a pipe)
    void reader(int fd, std::string &message, const size_t bytes);

    // Parse existing local checkpoint files
    void parse_local_checkpoint_files(const fs::path &path, std::map<int, CheckpointFiles> &checkpoint_versions);

    // Obtain remote checkpoint files from the primary
    void get_remote_checkpoint_files(std::map<int, CheckpointFiles> &checkpoint_versions);
    // Obtain remote versions from the primary
    void get_remote_versions(std::vector<size_t> &remote_tablet_versions);

    // Parse the received reply from the primary
    void parse_recover_reply(size_t tablet_id, CheckpointFiles &local_versions);

    // Load local checkpoint files into the tablet array
    void load_from_checkpoint_files(std::map<int, CheckpointFiles> &checkpoint_versions);

public:
    // Send checkpoint versions from the primary
    std::string send_remote_versions(std::string host_port);
    // Send checkpoint files from the primary
    std::string send_remote_files(std::string host_port, size_t tablet_id, size_t version);

    // Reset the tablet array
    void reset();
    // Initialize paths
    void init(const std::string &init_path, const std::string &work_path);
    // Initialize the tablet array (local/remote recovery or simple initialization from files)
    void load(bool recovery);
    // Write a value to a given row and column key
    TabletStatus write(const std::string &row_key, const std::string &column_key, tablet_value &value);
    // Conditionally write a value to a given row and column key
    TabletStatus conditional_write(const std::string &row_key, const std::string &column_key,
                                   const tablet_value &cvalue, tablet_value &value, bool &result);
    // Read a value from a given row and column key
    TabletStatus read(const std::string &row_key, const std::string &column_key, tablet_value &value);
    // Move a value from one column to another in a given row
    TabletStatus move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key);
    // Remove a value from a given row and column key
    TabletStatus remove(const std::string &row_key, const std::string &column_key);
    // List all columns in a given row
    TabletStatus list_columns(const std::string &row_key, std::set<std::string> &column_keys);
    // List all rows in the tablet array
    void list_rows(std::set<std::string> &row_keys);
    // List all tablets in the tablet array
    void list_tablets(std::set<std::string> &tablet_infos);
    // Create a new row in the tablet array
    TabletStatus create_row(const std::string &row_key);
};