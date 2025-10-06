#include "ThreadSafeQueue.h"

using namespace std;

void ThreadSafeQueue::push(const std::string &message)
{
    lock_guard<std::mutex> lock(this->mtx_);
    this->queue_.push(message);
    this->cond_.notify_one(); // Notify waiting thread (NewClientHandler) that a message is available
}

std::string ThreadSafeQueue::pop()
{
    std::unique_lock<std::mutex> lock(this->mtx_);
    this->cond_.wait(lock, [this]()
                     { return !this->queue_.empty(); }); // Wait for a message to be available
    std::string message = this->queue_.front();
    this->queue_.pop();
    return message;
}

bool ThreadSafeQueue::empty()
{
    std::lock_guard<std::mutex> lock(this->mtx_);
    return this->queue_.empty();
}