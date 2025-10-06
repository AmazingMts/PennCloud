#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H
#include <mutex>
#include <queue>
#include <condition_variable>

using namespace std;

class ThreadSafeQueue
{
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;
    // Push a message to the queue and notify waiting threads
    void push(const std::string &message);
    // Pop a message from the queue, blocking if the queue is empty
    std::string pop();
    // Check if the queue is empty
    bool empty();

private:
    queue<std::string> queue_; // Message queue
    mutex mtx_;                // Mutex to protect the queue
    condition_variable cond_;  // Condition variable to notify threads one new message is added
};

#endif