#include <string>
#include <fstream>
#include <string_view>
#include <filesystem>
#include "TabletLogger.h"
#include "TabletArray.h"
#include "Tablet.h"
#include "Globals.h"

TabletLogger::TabletLogger(size_t tablet_id, size_t version, size_t last_checkpoint)
    : version(version), last_checkpoint(last_checkpoint), file_prefix(std::to_string(SERVER_ID) + "_" + std::to_string(tablet_id) + "_")
{}

void TabletLogger::load(const fs::path &path, const std::list<TabletInfo>::iterator &tablet_it, size_t max_version)
{
    static const size_t cmd_len{3};
    std::ifstream ifs_log{path / (file_prefix + std::to_string(max_version) + ".log.tblt"), std::ios::binary};

    std::string line;
    while (std::getline(ifs_log, line))
    {
        // Extract command
        std::string_view cmd_str = std::string_view(line).substr(0, cmd_len);

        TabletLoggerCmdType cmd;
        if (cmd_str == "put") cmd = TabletLoggerCmdType::PUT;
        else if (cmd_str == "mov") cmd = TabletLoggerCmdType::MOV;
        else if (cmd_str == "del") cmd = TabletLoggerCmdType::DEL;
        else if (cmd_str == "row") cmd = TabletLoggerCmdType::ROW;
        else
        {
            fprintf(stderr, "Unknown command: %s\n", std::string(cmd_str).c_str());
            return;
        }

        size_t sep_version = line.find(",", cmd_len + 1) + 1;
        size_t log_version = std::stoul(line.substr(cmd_len + 1, sep_version - cmd_len));

        if (log_version > this->version + 1)
        {
            fprintf(stderr, "Missing entries in log: local version %zu << log version %zu\n", this->version, log_version);
            return;
        }

        size_t sep_row_key = line.find(",", sep_version) + 1;
        std::string row_key = line.substr(sep_version, sep_row_key - sep_version - 1);

        size_t sep_column_key;
        std::string column_key;
        if (cmd != TabletLoggerCmdType::ROW)
        {
            sep_column_key = line.find(",", sep_row_key) + 1;
            column_key = line.substr(sep_row_key, sep_column_key - sep_row_key - 1);
        }

        switch (cmd)
        {
            case TabletLoggerCmdType::PUT:
            {
                // Extract size of value
                size_t value_size{std::stoul(line.substr(sep_column_key))};

                if (log_version <= this->version)
                {
                    ifs_log.ignore(value_size + 1);
                    continue;
                }

                ++this->version;
                // Allocate memory for value
                tablet_value value;
                value.resize(value_size);

                // Read value from log
                ifs_log.read(reinterpret_cast<char *>(value.data()), value_size);
                // Skip newline character
                ifs_log.ignore(1);

                // Write value to tablet
                tablet_it->tablet.write(row_key, column_key, value);
                tablet_it->size += value_size;
                break;
            }
            case TabletLoggerCmdType::MOV:
            {
                if (log_version <= this->version) continue;

                ++this->version;
                std::string new_column_key = line.substr(sep_column_key);
                tablet_it->tablet.move(row_key, column_key, new_column_key);
                break;
            }
            case TabletLoggerCmdType::DEL:
            {
                if (log_version <= this->version) continue;

                ++this->version;
                size_t value_size{0};
                tablet_it->tablet.remove(row_key, column_key, value_size);
                tablet_it->size -= value_size;
                break;
            }
            case TabletLoggerCmdType::ROW:
            {
                if (log_version <= this->version) continue;

                ++this->version;
                tablet_it->tablet.create_row(row_key);
                break;
            }
        }
    }

    if (this->version != max_version)
    {
        fprintf(stderr, "Missing entries in log: local version %zu << log version %zu\n", this->version, max_version);
    }
}

void TabletLogger::log_write(const fs::path &path, const std::string &row_key, const std::string &column_key, const tablet_value &value)
{
    const fs::path old_file{path / (file_prefix + std::to_string(version) + ".log.tblt")};
    const fs::path new_file{path / (file_prefix + std::to_string(version + 1) + ".log.tblt")};
    std::ofstream ofs_log{old_file, std::ios::app | std::ios::binary};

    if (DEBUG) printf("Writing to log file: %s\n", (old_file.string() + " -> " + new_file.string()).c_str());

    // Write command to log
    ++version;
    ofs_log << "put," << version << "," << row_key << "," << column_key << "," << value.size() << "\n";
    // Write value to log
    ofs_log.write(reinterpret_cast<const char *>(value.data()), value.size());
    // Write newline character
    ofs_log << "\n";
    ofs_log.close();

    fs::rename(old_file, new_file);
}

void TabletLogger::log_move(const fs::path &path, const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    const fs::path old_file{path / (file_prefix + std::to_string(version) + ".log.tblt")};
    const fs::path new_file{path / (file_prefix + std::to_string(version + 1) + ".log.tblt")};
    std::ofstream ofs_log{old_file, std::ios::app | std::ios::binary};

    if (DEBUG) printf("Writing to log file: %s\n", (old_file.string() + " -> " + new_file.string()).c_str());

    // Write command to log
    ++version;
    ofs_log << "mov," << version << "," << row_key << "," << column_key << "," << new_column_key << "\n";
    ofs_log.close();
    
    fs::rename(old_file, new_file);
}

void TabletLogger::log_remove(const fs::path &path, const std::string &row_key, const std::string &column_key)
{
    const fs::path old_file{path / (file_prefix + std::to_string(version) + ".log.tblt")};
    const fs::path new_file{path / (file_prefix + std::to_string(version + 1) + ".log.tblt")};
    std::ofstream ofs_log{old_file, std::ios::app | std::ios::binary};

    if (DEBUG) printf("Writing to log file: %s\n", (old_file.string() + " -> " + new_file.string()).c_str());

    // Write command to log
    ++version;
    ofs_log << "del," << version << "," << row_key << "," << column_key << ",\n";
    ofs_log.close();
    
    fs::rename(old_file, new_file);
}

void TabletLogger::log_create_row(const fs::path &path, const std::string &row_key)
{
    const fs::path old_file{path / (file_prefix + std::to_string(version) + ".log.tblt")};
    const fs::path new_file{path / (file_prefix + std::to_string(version + 1) + ".log.tblt")};
    std::ofstream ofs_log{old_file, std::ios::app | std::ios::binary};

    if (DEBUG) printf("Writing to log file: %s\n", (old_file.string() + " -> " + new_file.string()).c_str());

    // Write command to log
    ++version;
    ofs_log << "row," << version << "," << row_key << ",\n";
    ofs_log.close();
    
    fs::rename(old_file, new_file);
}