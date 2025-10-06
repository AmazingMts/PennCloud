#include <shared_mutex>
#include "TabletArray.h"
#include <numeric>
#include <iterator>
#include <limits>
#include "Globals.h"
#include <unistd.h>
#include <filesystem>

namespace fs = std::filesystem;

TabletArray tablets;

size_t TabletArray::get_tablet_id(const std::string &row_key)
{
    size_t idx = std::upper_bound(tablet_sort_info.begin(), tablet_sort_info.end(), row_key,
        [this](const std::string &key, const TabletSortInfo &info)
        {return key < info.first_row_key;}) - tablet_sort_info.begin();

    if (idx > 0) --idx;

    if (DEBUG)
        fprintf(stdout, "Found tablet id %zu (%zu, %s) for row key %s\n", idx,
                tablet_sort_info[idx].index, tablet_sort_info[idx].first_row_key.c_str(), row_key.c_str());

    return idx;
}

void TabletArray::save_checkpoint(const std::list<TabletInfo>::iterator &tablet_it, const size_t tablet_id)
{
    // Remove old checkpoint(s)
    fs::remove(work_path / (tablet_file(tablet_id, loggers[tablet_id].get_last_checkpoint()) + ".bin.tblt"));
    fs::remove(work_path / (tablet_file(tablet_id, loggers[tablet_id].get_last_checkpoint()) + ".idx.tblt"));
    // Remove old log file
    fs::remove(work_path / (tablet_file(tablet_id, loggers[tablet_id].get_version()) + ".log.tblt"));

    // Save tablet to disk
    tablet_it->tablet.save_to_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
    // Save new checkpoint
    loggers[tablet_id].record_checkpoint();
}

void TabletArray::initiate_checkpoint(const std::list<TabletInfo>::iterator &tablet_it, const size_t tablet_id)
{
    if (loggers[tablet_id].get_last_checkpoint() + CHECKPOINT_FREQUENCY <= loggers[tablet_id].get_version())
        save_checkpoint(tablet_it, tablet_id);
}

size_t TabletArray::cache_inactive()
{
    // Find the tablet that hasn't been used for the longest
    const auto it_oldest_tablet{std::min_element(tablet_list.begin(), tablet_list.end(),
            [](const TabletInfo &a, const TabletInfo &b) {return a.in_memory < b.in_memory;})
    };
    const size_t idx_oldest_tablet{
        static_cast<size_t>(std::distance(tablet_list.begin(), it_oldest_tablet))
    };
    
    // Save tablet to disk
    save_checkpoint(it_oldest_tablet, idx_oldest_tablet);
    // Clear tablet from memory
    it_oldest_tablet->tablet.clear();
    // Mark tablet as cached
    it_oldest_tablet->in_memory = std::numeric_limits<size_t>::max();

    return idx_oldest_tablet;
}

// Check if adding the value to the tablet would exceed the maximum size
// Returns true if the size exceeds the maximum size, false otherwise
bool TabletArray::atomic_size_check(const size_t &value_size, std::atomic<size_t> &tablet_size)
{
    size_t current = tablet_size.load();
    while (true)
    {
        if (current + value_size > MAX_TABLET_SIZE)
            return true;

        if (tablet_size.compare_exchange_weak(current, current + value_size))
            return false;
    }
}

size_t TabletArray::get_first_row_hash() const
{
    size_t replication_group_count = SERVER_COUNT / SERVERS_PER_RG;
    return (std::numeric_limits<size_t>::max() / replication_group_count) * RG_ID;
}

void TabletArray::get_remote_checkpoint_files(std::map<int, CheckpointFiles> &checkpoint_versions)
{
    // Check if any primary is available in the replication group
    bool primary_available{false};
    std::map<int, std::vector<KVServer>> kv_servers_map = COORDINATOR_SERVICE->get_kv_servers_map();
    for (const auto &kv_server : kv_servers_map[RG_ID])
    {
        primary_available |= kv_server.is_primary;
    }

    // Return if no primary is available, local recovery
    if (!primary_available)
    {
        fprintf(stderr, "No primary available in replication group %zu, doing local recovery\n", RG_ID);
        return;
    }

    // Get latest version numbers from primary
    std::vector<size_t> remote_tablet_versions;
    get_remote_versions(remote_tablet_versions);

    // Delete unnecessary versions of split tablets that are not on the primary
    for (auto it = checkpoint_versions.upper_bound(remote_tablet_versions.size() - 1); it != checkpoint_versions.end();)
    {
        size_t tablet_id = it->first;
        const auto &checkpoint = it->second;

        if (checkpoint.log > 0)
            fs::remove(work_path / (tablet_file(tablet_id, checkpoint.log) + ".log.tblt"));
        
        if (checkpoint.tblt > 0)
        {
            fs::remove(work_path / (tablet_file(tablet_id, checkpoint.tblt) + ".bin.tblt"));
            fs::remove(work_path / (tablet_file(tablet_id, checkpoint.tblt) + ".idx.tblt"));
        }

        it = checkpoint_versions.erase(it);
    }

    // Download missing version files
    for (int i{0}; i < remote_tablet_versions.size(); ++i)
    {
        size_t local_version;
        if (checkpoint_versions.count(i) == 0)
        {
            local_version = 0;
            checkpoint_versions[i].tblt = 0;
            checkpoint_versions[i].log = 0;
        }
        else
        {
            local_version = std::max(checkpoint_versions[i].tblt, checkpoint_versions[i].log);
        }

        if (local_version < remote_tablet_versions[i])
        {
            // Send request to primary
            std::string message{std::to_string(i) + " " + std::to_string(local_version)};
            UPDATE_FORWARDER->forward_update("?SYNCF " + HOST + ":" + std::to_string(PORT_NO) + " " + message);

            // receive response
            parse_recover_reply(i, checkpoint_versions[i]);
        }
    }
}

void TabletArray::reader(int fd, std::string &message)
{
    message.clear();
    char buffer[1024];
    size_t newline_pos{0};

    while (true)
    {
        newline_pos = pipe_buffer.find("\r\n");
        if (newline_pos != std::string::npos)
        {
            fprintf(stderr, "Found CRLF in pipe buffer\n");
            message = pipe_buffer.substr(0, newline_pos);
            pipe_buffer.erase(0, newline_pos + 2);
            return;
        }

        ssize_t bytes_received = ::read(fd, buffer, sizeof(buffer));
        if (bytes_received < 0)
        {
            fprintf(stderr, "Error reading from recovery pipe: %s\n", strerror(errno));
            return;
        }
        pipe_buffer.append(buffer, bytes_received);
    }
}

void TabletArray::reader(int fd, std::string &message, const size_t bytes_requested)
{
    char buffer[1024];
    message.clear();
    message.reserve(bytes_requested);
    size_t total_bytes{0};
    size_t append_bytes{0};

    while (total_bytes < bytes_requested)
    {
        append_bytes = std::min(pipe_buffer.length(), bytes_requested - total_bytes);
        message.append(pipe_buffer, 0, append_bytes);
        pipe_buffer.erase(0, append_bytes);
        total_bytes += append_bytes;

        if (total_bytes == bytes_requested) return;

        int bytes_received = ::read(fd, buffer, sizeof(buffer));
        if (bytes_received < 0)
        {
            fprintf(stderr, "Error reading from recovery pipe: %s\n", strerror(errno));
            return;
        }

        pipe_buffer.append(buffer, bytes_received);
    }
}

void TabletArray::get_remote_versions(std::vector<size_t> &remote_tablet_versions)
{
    // send request to primary
    UPDATE_FORWARDER->forward_update("?SYNCV " + HOST + ":" + std::to_string(PORT_NO));

    // receive response
    std::string response;
    reader(RECOVERY_PIPE_R, response);
    std::string_view response_view{response};

    size_t sep{0}, prev_sep{0};
    while (sep != std::string::npos)
    {
        sep = response_view.find(",");
        remote_tablet_versions.push_back(std::stoul(std::string(response_view.substr(0, sep))));
        response_view.remove_prefix(sep + 1);
    }

    if (DEBUG) fprintf(stderr, "Received and parsed remote tablet versions\n");
}

std::string TabletArray::send_remote_versions(std::string host_port)
{
    std::string message{"#SYNCV " + host_port + " "};
    for (size_t i{0}; i < tablet_list.size() - 1; ++i)
    {
        message += std::to_string(loggers[i].get_version()) + ",";
    }
    message += std::to_string(loggers[tablet_list.size() - 1].get_version());

    return message;
}

void TabletArray::parse_recover_reply(size_t tablet_id, CheckpointFiles &local_versions)
{
    std::string numbers, bin, idx, log;
    fprintf(stderr, "Trying to read recovery files from pipe\n");
    reader(RECOVERY_PIPE_R, numbers);
    fprintf(stderr, "Received number string from primary %s\n", numbers.c_str());
    std::string_view numbers_view{numbers};

    // Extract the version numbers and file lengths
    size_t pos = numbers_view.find(" ");
    size_t checkpoint_version = std::stoul(string{numbers_view.substr(0, pos)});
    numbers_view.remove_prefix(pos + 1);

    pos = numbers_view.find(" ");
    size_t bin_file_length = std::stoul(string{numbers_view.substr(0, pos)});
    numbers_view.remove_prefix(pos + 1);

    pos = numbers_view.find(" ");
    size_t idx_file_length = std::stoul(string{numbers_view.substr(0, pos)});
    numbers_view.remove_prefix(pos + 1);

    pos = numbers_view.find(" ");
    size_t log_version = std::stoul(string{numbers_view.substr(0, pos)});
    numbers_view.remove_prefix(pos + 1);

    size_t log_file_length = std::stoul(string{numbers_view});

    if (checkpoint_version > 0)
    {
        // Remove old checkpoint and log files
        fs::remove(work_path / (tablet_file(tablet_id, local_versions.tblt) + ".bin.tblt"));
        fs::remove(work_path / (tablet_file(tablet_id, local_versions.tblt) + ".idx.tblt"));
        fs::remove(work_path / (tablet_file(tablet_id, local_versions.log) + ".log.tblt"));

        if (log_version > 0)
        {
            // Create new checkpoint and log files
            std::ofstream ifs_bin(work_path / (tablet_file(tablet_id, checkpoint_version) + ".bin.tblt"), std::ios::binary);
            std::ofstream ifs_idx(work_path / (tablet_file(tablet_id, checkpoint_version) + ".idx.tblt"));
            std::ofstream ifs_log(work_path / (tablet_file(tablet_id, log_version) + ".log.tblt"), std::ios::binary);

            // Write received data to the files
            reader(RECOVERY_PIPE_R, bin, bin_file_length);
            ifs_bin.write(bin.data(), bin_file_length);
            ifs_bin.close();

            reader(RECOVERY_PIPE_R, idx, idx_file_length);
            ifs_idx.write(idx.data(), idx_file_length);
            ifs_idx.close();

            reader(RECOVERY_PIPE_R, log, log_file_length);
            ifs_log.write(log.data(), log_file_length);
            ifs_log.close();
        }
        else
        {
            // Create new checkpoint and log files
            std::ofstream ifs_bin(work_path / (tablet_file(tablet_id, checkpoint_version) + ".bin.tblt"), std::ios::binary);
            std::ofstream ifs_idx(work_path / (tablet_file(tablet_id, checkpoint_version) + ".idx.tblt"));

            // Write received data to the files
            reader(RECOVERY_PIPE_R, bin, bin_file_length);
            ifs_bin.write(bin.data(), bin_file_length);
            ifs_bin.close();

            reader(RECOVERY_PIPE_R, idx, idx_file_length);
            ifs_idx.write(idx.data(), idx_file_length);
            ifs_idx.close();
        }
    }
    else
    {
        // Append to the log file
        std::ofstream ifs_log(work_path / (tablet_file(tablet_id, local_versions.log) + ".log.tblt"), std::ios::binary | std::ios::app);

        reader(RECOVERY_PIPE_R, log, log_file_length);
        ifs_log.write(log.data(), log_file_length);
        ifs_log.close();

        fs::rename(
            work_path / (tablet_file(tablet_id, local_versions.log) + ".log.tblt"),
            work_path / (tablet_file(tablet_id, log_version) + ".log.tblt")
        );
    }

    if (checkpoint_version) local_versions.tblt = checkpoint_version;
    if (log_version) local_versions.log = log_version;

    return;
}
/*
<checkpoint-version> <bin-length> <idx-length> <log-version> <log-length>\r\n
<bin-data>\r\n
<idx-data>\r\n
<log-data>\r\n
*/
std::string TabletArray::send_remote_files(std::string host_port, size_t tablet_id, size_t version)
{
    std::string reply{"#SYNCF " + host_port + " "};
    size_t checkpoint_version = loggers[tablet_id].get_last_checkpoint();
    size_t log_version = loggers[tablet_id].get_version();
    if (checkpoint_version > version)
    {
        // Checkpoint on primary needs to be sent because data before that is not available
        if (checkpoint_version == log_version)
        {
            // Don't need to send log file
            // Open and read the checkpoint files
            std::ifstream ifs_bin(work_path / (tablet_file(tablet_id, checkpoint_version) + ".bin.tblt"), std::ios::binary | std::ios::ate);
            std::ifstream ifs_idx(work_path / (tablet_file(tablet_id, checkpoint_version) + ".idx.tblt"), std::ios::ate);

            // Get the file sizes
            size_t bin_file_length = ifs_bin.tellg();
            size_t idx_file_length = ifs_idx.tellg();

            reply += std::to_string(checkpoint_version) + " " +
                     std::to_string(bin_file_length) + " " +
                     std::to_string(idx_file_length) + " 0 0\r\n";

            // Read the file contents into the string without issuing multiple resizes
            size_t reply_pos = reply.size();
            reply.resize(reply_pos + bin_file_length + idx_file_length + 4);

            ifs_bin.seekg(0);
            ifs_bin.read(&reply[reply_pos], bin_file_length);
            reply_pos += bin_file_length;
            reply[reply_pos++] = '\r';
            reply[reply_pos++] = '\n';
            ifs_bin.close();

            ifs_idx.seekg(0);
            ifs_idx.read(&reply[reply_pos], idx_file_length);
            reply_pos += idx_file_length;
            reply[reply_pos++] = '\r';
            reply[reply_pos++] = '\n';
            ifs_idx.close();
        }
        else
        {
            // Checkpoint and entire log file of the primary need to be sent
            std::ifstream ifs_bin(work_path / (tablet_file(tablet_id, checkpoint_version) + ".bin.tblt"), std::ios::binary | std::ios::ate);
            std::ifstream ifs_idx(work_path / (tablet_file(tablet_id, checkpoint_version) + ".idx.tblt"), std::ios::ate);
            std::ifstream ifs_log(work_path / (tablet_file(tablet_id, log_version) + ".log.tblt"), std::ios::binary | std::ios::ate);

            // Get the file sizes
            size_t bin_file_length = ifs_bin.tellg();
            size_t idx_file_length = ifs_idx.tellg();
            size_t log_file_length = ifs_log.tellg();

            reply += std::to_string(checkpoint_version) + " " +
                    std::to_string(bin_file_length) + " " +
                    std::to_string(idx_file_length) + " " +
                    std::to_string(log_version) + " " +
                    std::to_string(log_file_length) + "\r\n";

            // Read the file contents into the string without issuing multiple resizes
            size_t reply_pos = reply.size();
            reply.resize(reply_pos + bin_file_length + idx_file_length + log_file_length + 4);

            ifs_bin.seekg(0);
            ifs_bin.read(&reply[reply_pos], bin_file_length);
            reply_pos += bin_file_length;
            reply[reply_pos++] = '\r';
            reply[reply_pos++] = '\n';
            ifs_bin.close();

            ifs_idx.seekg(0);
            ifs_idx.read(&reply[reply_pos], idx_file_length);
            reply_pos += idx_file_length;
            reply[reply_pos++] = '\r';
            reply[reply_pos++] = '\n';
            ifs_idx.close();

            ifs_log.seekg(0);
            ifs_log.read(&reply[reply_pos], log_file_length);
            ifs_log.close();
        }
    }
    else
    {
        // Only the log file needs to be (potentially partially) sent
        std::string line;
        std::ifstream ifs_log(work_path / (tablet_file(tablet_id, log_version) + ".log.tblt"), std::ios::binary);

        reply += "0 0 0 ";
        // Parse the log file line by line until the version number is greater than the version of the recovering node
        while (std::getline(ifs_log, line))
        {
            size_t line_version = std::stoul(line.substr(4, line.find(",", 4)));
            if (line_version > version)
            {
                // Save current position
                std::streampos pos{ifs_log.tellg()};
                ifs_log.seekg(0, std::ios::end);
                // Get the remaining size of the log file
                std::streamsize log_size{ifs_log.tellg() - pos};
                ifs_log.seekg(pos);
                
                // Read the rest of the log file from the current position
                reply += std::to_string(log_version) + " " + std::to_string(log_size + line.size() + 1) + "\r\n\r\n\r\n" + line + "\n";
                
                size_t header_size = reply.size();

                reply.resize(header_size + log_size);
                ifs_log.read(&reply[header_size], log_size);
                break;
            }

            // If a put command is found we need to skip the value
            if (line.substr(0, 3) == "put")
            {
                size_t sep{0};
                for (int i{0}; i<4; ++i) sep = line.find(",", sep) + 1;

                size_t value_size = std::stoul(line.substr(sep));
                // Skip value and newline character
                ifs_log.seekg(value_size + 1, std::ios::cur);
            }
        }
    }

    return reply;
}

void TabletArray::reset()
{
    tablet_list.clear();
    tablet_sort_info.clear();
    loggers.clear();
    activity_counter = 0;
}

void TabletArray::init(const std::string &init_path, const std::string &work_path)
{
    this->init_path = init_path;
    this->work_path = work_path;
}

void TabletArray::load(bool recovery)
{
    if (DEBUG)
    {
        if (recovery)
            fprintf(stderr, "Starting recovery of TabletArray from %s\n", work_path.c_str());
        else
            fprintf(stderr, "Starting initializing of TabletArray from %s\n", work_path.c_str());
    }

    fs::path path = recovery ? work_path : init_path;

    if (path.empty())
    {
        tablet_list.emplace_back();
        loggers.emplace_back(0);
        tablet_sort_info.emplace_back(TabletSortInfo{0, num_to_row(get_first_row_hash())});
        if (DEBUG) fprintf(stderr, "Initialized empty TabletArray\n");
        return;
    }

    // Get all checkpoint indexing files in the directory
    std::map<int, CheckpointFiles> checkpoint_versions;
    parse_local_checkpoint_files(path, checkpoint_versions);

    // Copy initialization files to work path
    if (!recovery)
    {
        for (const auto& [tablet_id, checkpoint] : checkpoint_versions)
        {
            if (checkpoint.log > 0)
                fs::copy(init_path / (tablet_file(tablet_id, checkpoint.log) + ".log.tblt"),
                         work_path / (tablet_file(tablet_id, checkpoint.log) + ".log.tblt"),
                         fs::copy_options::overwrite_existing);
            
            if (checkpoint.tblt > 0)
            {
                fs::copy(init_path / (tablet_file(tablet_id, checkpoint.tblt) + ".bin.tblt"),
                         work_path / (tablet_file(tablet_id, checkpoint.tblt) + ".bin.tblt"),
                         fs::copy_options::overwrite_existing);
                fs::copy(init_path / (tablet_file(tablet_id, checkpoint.tblt) + ".idx.tblt"),
                         work_path / (tablet_file(tablet_id, checkpoint.tblt) + ".idx.tblt"),
                         fs::copy_options::overwrite_existing);
            }
        }
    }

    // Get remote checkpoint files from primary if required
    if (recovery) get_remote_checkpoint_files(checkpoint_versions);

    // Load tablets from checkpoint files
    load_from_checkpoint_files(checkpoint_versions);

    if (tablet_list.empty())
    {
        tablet_list.emplace_back();
        loggers.emplace_back(0);
        tablet_sort_info.emplace_back(TabletSortInfo{0, num_to_row(get_first_row_hash())});
        if (DEBUG) fprintf(stderr, "Initialized empty TabletArray\n");
        return;
    }

    fprintf(stdout, "TabletArray initialization successful\n");
}

void TabletArray::load_from_checkpoint_files(std::map<int, CheckpointFiles> &checkpoint_versions)
{
    for (int tablet_id{0}; tablet_id < checkpoint_versions.size(); ++tablet_id)
    {
        // No checkpoint or logging files for this tablet, end loading process
        if (checkpoint_versions.count(tablet_id) == 0) break;

        // Cache tablets if the maximum number of tablets in memory is reached
        if (tablet_id >= MAX_IN_MEMORY) cache_inactive();

        if (checkpoint_versions[tablet_id].tblt > 0)
        {
            // Load tablet from last checkpoint
            tablet_list.emplace_back();
            tablet_list.back().tablet.read_from_file(work_path / tablet_file(tablet_id, checkpoint_versions[tablet_id].tblt));
            tablet_list.back().in_memory = tablet_id;
            tablet_list.back().size = tablet_list.back().tablet.size();

            // Create logger for tablet
            loggers.emplace_back(tablet_id, checkpoint_versions[tablet_id].tblt, checkpoint_versions[tablet_id].tblt);

            if (tablet_id == 0)
            {
                const std::string first_row_key{num_to_row(get_first_row_hash())};

                tablet_list.back().tablet.first_row_key = first_row_key;
                tablet_sort_info.insert(tablet_sort_info.begin(),  {tablet_list.size() - 1, first_row_key});
            }
            else
            {
                const std::string first_row_key{tablet_list.back().tablet.get_first_row_key()};
                size_t insert_index = get_tablet_id(first_row_key) + 1;
                tablet_sort_info.insert(tablet_sort_info.begin() + insert_index, 
                    {tablet_list.size() - 1, first_row_key});
            }

            if (DEBUG) fprintf(stderr, "Successfully loaded checkpoint %zu for tablet %zu\n",
                checkpoint_versions[tablet_id].tblt, tablet_id);
        }
        else
        {
            if (tablet_id == 0)
            {
                tablet_list.emplace_back();
                loggers.emplace_back(0);
                tablet_sort_info.emplace_back(TabletSortInfo{0, num_to_row(get_first_row_hash())});
            }
            else
            {
                fprintf(stderr, "Cannot recover tablet %d without checkpoint file\n", tablet_id);
                break;
            }
        }

        if (checkpoint_versions[tablet_id].log > checkpoint_versions[tablet_id].tblt)
        {
            loggers[tablet_id].load(work_path, std::prev(tablet_list.end()), checkpoint_versions[tablet_id].log);
            tablet_list.back().size = tablet_list.back().tablet.size();
            if (DEBUG) fprintf(stderr, "Successfully loaded log entires up to %zu for tablet %zu\n",
                checkpoint_versions[tablet_id].log, tablet_id);
        }
    }
}

void TabletArray::parse_local_checkpoint_files(const fs::path &path, std::map<int, CheckpointFiles> &checkpoint_versions)
{
    std::string prefix{std::to_string(SERVER_ID) + "_"};
    size_t tablet_id, version;
    for (const auto& file : std::filesystem::directory_iterator(path))
    {
        std::filesystem::path filename = file.path().filename();
        if (file.is_regular_file() && filename.string().substr(0, prefix.size()) == prefix)
        {
            size_t sep = filename.string().find("_", prefix.size());
            tablet_id = std::stoul(filename.string().substr(prefix.size(), sep - prefix.size()));

            if (filename.extension() == ".tblt")
            {
                if (filename.stem().extension() == ".log")
                {
                    checkpoint_versions[tablet_id].log = std::max(
                        std::stoul(filename.stem().string().substr(sep + 1)),
                        checkpoint_versions[tablet_id].log
                    );
                }
                else if (filename.stem().extension() == ".idx")
                {
                    checkpoint_versions[tablet_id].tblt = std::max(
                        std::stoul(filename.stem().stem().string().substr(sep + 1)),
                        checkpoint_versions[tablet_id].tblt
                    );
                }
            }
        }
    }
}


int TabletArray::handle_tablet_split(
    size_t &tablet_id,
    std::list<TabletInfo>::iterator &tablet_it,
    const size_t &value_size,
    const std::string &row_key,
    const bool tablet_on_disk)
{
    // Check if splitting of the tablet is needed
    if (!atomic_size_check(value_size, tablet_it->size))
        return 0;

    // Upgrade mutex to exclusive lock if tablet in memory
    if (!tablet_on_disk && tablets_mutex.try_upgrade())
    {
        tablets_mutex.unlock_shared();
        tablet_it->size -= value_size;
        return -1;
    }

    // Create new tablet
    tablet_list.emplace_back();
    // Split current tablet into new tablet
    tablet_it->tablet.split(tablet_list.back().tablet);
    // Copy activity counter to new tablet
    tablet_list.back().in_memory = tablet_it->in_memory;

    loggers.emplace_back(tablet_list.size() - 1, 1);

    if (DEBUG)
    {
        std::set<std::string> row_keys;
        fprintf(stdout, "Splitting tablet %zu\n", tablet_id);

        fprintf(stdout, "Tablet %zu: ", tablet_id);
        row_keys.clear();
        tablet_it->tablet.list_rows(row_keys);
        for (const auto &row : row_keys)
            fprintf(stdout, "%s ", row.c_str());
        
        fprintf(stdout, "\nTablet %zu: ", tablet_list.size()-1);
        row_keys.clear();
        tablet_list.back().tablet.list_rows(row_keys);
        for (const auto &row : row_keys)
            fprintf(stdout, "%s ", row.c_str());
        fprintf(stdout, "\n");
    }

    // Update tablet sizes
    tablet_list.back().size = tablet_list.back().tablet.size();
    tablet_it->size = tablet_it->tablet.size();

    // Get the insertion index for the new tablet
    const std::string first_row_key{tablet_list.back().tablet.get_first_row_key()};
    const size_t insert_index = get_tablet_id(first_row_key) + 1;
    // Insert sorted index in index map
    tablet_sort_info.insert(tablet_sort_info.begin() + insert_index, 
        {tablet_list.size() - 1, first_row_key});

    // Get new tablet id
    size_t new_tablet_id = tablet_sort_info[get_tablet_id(row_key)].index;
    std::list<TabletInfo>::iterator new_tablet_it = tablet_list.begin();
    std::advance(new_tablet_it, new_tablet_id);

    new_tablet_it->size += value_size;

    // Update activity counter for tablet
    new_tablet_it->in_memory = activity_counter++;

    // Increase version number of split tablet
    loggers[tablet_id].log_noop();

    // Check if any other tablet needs to be saved to disk
    if (active_tablet_count() > MAX_IN_MEMORY)
    {
        if (cache_inactive() == tablet_id)
            save_checkpoint(std::prev(tablet_list.end()), tablet_list.size() - 1);
        else
            save_checkpoint(tablet_it, tablet_id);
    }
    else
    {
        save_checkpoint(tablet_it, tablet_id);
        save_checkpoint(std::prev(tablet_list.end()), tablet_list.size() - 1);
    }

    tablet_it = new_tablet_it;
    tablet_id = new_tablet_id;
    
    return 1;
}

TabletStatus TabletArray::write(const std::string &row_key, const std::string &column_key, tablet_value &value)
{
    if (value.size() > MAX_VALUE_SIZE)
        return TabletStatus::VALUE_SIZE_ERR;
    
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    int split_result = handle_tablet_split(tablet_id, tablet_it,
                                           value.size(), row_key, tablet_on_disk);

    if (split_result == -1)
        return TabletStatus::SPLITTING_ERR;
    const bool split_needed = split_result == 1;

    std::lock_guard row_lock{tablet_it->tablet.row_mutexes[row_key]};

    if (tablet_it->tablet.data[row_key].size + value.size() > MAX_ROW_SIZE)
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_SIZE_ERR;
    }

    // Write value to tablet
    loggers[tablet_id].log_write(work_path, row_key, column_key, value);
    initiate_checkpoint(tablet_it, tablet_id);
    TabletStatus status = tablet_it->tablet.write(row_key, column_key, value);

    if (tablet_on_disk || split_needed)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

TabletStatus TabletArray::conditional_write(const std::string &row_key, const std::string &column_key,
                                            const tablet_value &cvalue, tablet_value &value, bool &result)
{
    if (value.size() > MAX_VALUE_SIZE || cvalue.size() > MAX_VALUE_SIZE)
        return TabletStatus::VALUE_SIZE_ERR;

    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    int split_result = handle_tablet_split(tablet_id, tablet_it,
                                           value.size(), row_key, tablet_on_disk);

    if (split_result == -1)
        return TabletStatus::SPLITTING_ERR;
    const bool split_needed = split_result == 1;

    std::lock_guard row_lock{tablet_it->tablet.row_mutexes[row_key]};

    if (tablet_it->tablet.data[row_key].size + value.size() - cvalue.size() > MAX_ROW_SIZE)
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_SIZE_ERR;
    }

    // Write value to tablet
    TabletStatus status = tablet_it->tablet.compare(row_key, column_key, cvalue, result);
    if (result)
    {
        loggers[tablet_id].log_write(work_path, row_key, column_key, value);
        initiate_checkpoint(tablet_it, tablet_id);
        tablet_it->tablet.write(row_key, column_key, value);
    }

    if (tablet_on_disk || split_needed)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

TabletStatus TabletArray::read(const std::string &row_key, const std::string &column_key, tablet_value &value)
{
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    std::shared_lock row_lock{tablet_it->tablet.row_mutexes[row_key]};

    // Read value from tablet
    TabletStatus status = tablet_it->tablet.read(row_key, column_key, value);

    if (tablet_on_disk)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

TabletStatus TabletArray::move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    std::lock_guard row_lock{tablet_it->tablet.row_mutexes[row_key]};

    if (!tablet_it->tablet.has_column(row_key, column_key))
    {
        tablets_mutex.unlock();
        return TabletStatus::COLUMN_KEY_ERR;
    }

    // Move columns
    loggers[tablet_id].log_move(work_path, row_key, column_key, new_column_key);
    initiate_checkpoint(tablet_it, tablet_id);
    TabletStatus status = tablet_it->tablet.move(row_key, column_key, new_column_key);

    if (tablet_on_disk)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

TabletStatus TabletArray::remove(const std::string &row_key, const std::string &column_key)
{
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    std::lock_guard row_lock{tablet_it->tablet.row_mutexes[row_key]};

    // Remove value from tablet
    size_t value_size{0};
    loggers[tablet_id].log_remove(work_path, row_key, column_key);
    initiate_checkpoint(tablet_it, tablet_id);
    TabletStatus status = tablet_it->tablet.remove(row_key, column_key, value_size);
    tablet_it->size -= value_size;

    if (tablet_on_disk)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

TabletStatus TabletArray::list_columns(const std::string &row_key, std::set<std::string> &column_keys)
{
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::shared_lock tablet_lock{tablet_it->tablet.tablet_mutex};

    if (!tablet_it->tablet.has_row(row_key))
    {
        if (tablet_on_disk)
            tablets_mutex.unlock();
        else
            tablets_mutex.unlock_shared();

        return TabletStatus::ROW_KEY_ERR;
    }

    std::shared_lock row_lock{tablet_it->tablet.row_mutexes[row_key]};

    column_keys.clear();
    TabletStatus status = tablet_it->tablet.list_columns(row_key, column_keys);

    if (tablet_on_disk)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}

void TabletArray::list_rows(std::set<std::string> &row_keys)
{
    // Exclusive lock on tablet array
    tablets_mutex.lock_shared();
    while (tablets_mutex.try_upgrade());

    row_keys.clear();
    int idx{0};
    for (auto it{tablet_list.begin()}; it != tablet_list.end(); ++it)
    {
        if (it->in_memory == std::numeric_limits<size_t>::max())
        {
            cache_inactive();
            it->tablet.read_from_file(work_path / tablet_file(idx, loggers[idx].get_last_checkpoint()));
        }

        it->tablet.list_rows(row_keys);
        it->in_memory = activity_counter++;
        idx++;
    }

    tablets_mutex.unlock();
}

void TabletArray::list_tablets(std::set<std::string> &tablet_infos)
{
    // Exclusive lock on tablet array
    tablets_mutex.lock_shared();
    while (tablets_mutex.try_upgrade())
        ;

    tablet_infos.clear();
    int idx{0};
    for (const auto &tablet_info : tablet_sort_info)
    {
        auto it{tablet_list.begin()};
        std::advance(it, tablet_info.index);
        tablet_infos.insert(tablet_info.first_row_key +
                            " " + std::to_string(it->size.load()) +
                            " " + std::to_string(it->in_memory != std::numeric_limits<size_t>::max()));
    }

    tablets_mutex.unlock();
}

TabletStatus TabletArray::create_row(const std::string &row_key)
{
    // Shared lock on tablet array for safe reading
    tablets_mutex.lock_shared();

    // Get tablet id
    size_t tablet_id{tablet_sort_info[get_tablet_id(row_key)].index};
    auto tablet_it{tablet_list.begin()};
    std::advance(tablet_it, tablet_id);

    // Check if tablet is on disk
    const bool tablet_on_disk{tablet_it->in_memory == std::numeric_limits<size_t>::max()};
    if (tablet_on_disk)
    {
        if (tablets_mutex.try_upgrade())
        {
            tablets_mutex.unlock_shared();
            return TabletStatus::CACHING_ERR;
        }

        // Check if any other tablet needs to be saved to disk
        if (active_tablet_count() >= MAX_IN_MEMORY) cache_inactive();
 
        tablet_it->tablet.read_from_file(work_path / tablet_file(tablet_id, loggers[tablet_id].get_version()));
        tablet_it->in_memory = activity_counter++;
    }

    std::lock_guard tablet_lock{tablet_it->tablet.tablet_mutex};

    // Create row in tablet
    loggers[tablet_id].log_create_row(work_path, row_key);
    initiate_checkpoint(tablet_it, tablet_id);
    TabletStatus status = tablet_it->tablet.create_row(row_key);

    if (tablet_on_disk)
        tablets_mutex.unlock();
    else
        tablets_mutex.unlock_shared();

    return status;
}