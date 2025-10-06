#pragma once

#include <filesystem>
#include "TabletArray.h"
#include "Tablet.h"

namespace fs = std::filesystem;

enum class TabletLoggerCmdType {
    PUT,
    MOV,
    DEL,
    ROW
};

class TabletInfo;

class TabletLogger {
private:
    // Version number, incremented for each write operation
    size_t version{0};
    // Saves the version of the last checkpoint
    size_t last_checkpoint{0};
    // Convenience variable to get the file prefix for a tablet
    const std::string file_prefix;

public:
    // Constructor setting the tablet id, current version and last checkpoint
    TabletLogger(size_t tablet_id, size_t version = 0, size_t last_checkpoint = 0);
    // Apply a log file to the given tablet
    void load(const fs::path &path, const std::list<TabletInfo>::iterator &tablet_it, size_t version);

    inline size_t get_version() const
    {
        return version;
    }
    inline size_t get_last_checkpoint() const
    {
        return last_checkpoint;
    }
    inline void record_checkpoint()
    {
        last_checkpoint = version;
    }
    inline void log_noop()
    {
        ++version;
    }

    // Log a write operation (conditional write is not needed, because it will result in a noop or write operation, that can be logged)
    void log_write(const fs::path &path, const std::string &row_key, const std::string &column_key, const tablet_value &value);
    // Log a move operation
    void log_move(const fs::path &path, const std::string &row_key, const std::string &column_key, const std::string &new_column_key);
    // Log a delete operation
    void log_remove(const fs::path &path, const std::string &row_key, const std::string &column_key);
    // Log a create row operation
    void log_create_row(const fs::path &path, const std::string &row_key);
};