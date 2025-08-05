#pragma once
/**
 * @author achene
 * @date 2025-08-05
 * 
 * 线程安全队列实现，支持多线程环境下的生产者-消费者模型。
 * 提供了带超时机制的元素入队和出队操作，支持有界/无界队列模式，
 * 并包含队列终止功能以安全结束线程协作。
 */
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <atomic>
#include <optional>
#include <iostream>
// 线程安全队列模板类，支持多线程环境下的生产者-消费者模型
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @brief 构造函数，指定队列容量
     * @param max_size 队列最大容量（0表示无界队列）
     */
    explicit ThreadSafeQueue(size_t max_size = 0) 
        : max_size_(max_size), 
          terminated_(false) {}
    
    /**
     * @brief 析构函数，唤醒所有等待线程并终止队列
     */
    ~ThreadSafeQueue() {
        terminate();
    }
    
    /**
     * @brief 向队列中推入元素（拷贝语义）
     * @param item 要推入的元素（const引用）
     * @param timeout_ms 超时时间(毫秒，-1表示无限等待)
     * @return 成功推入返回true，超时或队列已终止返回false
     */
    bool push(const T& item, int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 如果是有界队列，需要等待队列有空闲空间
        if (max_size_ > 0) {
            if (timeout_ms >= 0) {
                // 带超时的等待：等待队列有空间或队列已终止
                if (!not_full_.wait_for(lock, 
                                       std::chrono::milliseconds(timeout_ms), 
                                       [this] { 
                                           return queue_.size() < max_size_ || terminated_; 
                                       })) {
                    return false; // 超时
                }
            } else {
                // 无限等待：直到队列有空间或队列已终止
                not_full_.wait(lock, [this] { 
                    return queue_.size() < max_size_ || terminated_; 
                });
            }
        }
        
        // 检查是否已终止
        if (terminated_) return false;
        
        // 添加元素
        queue_.push(item);
        
        // 通知消费者
        not_empty_.notify_one();
        return true;
    }
    
    /**
     * @brief 向队列中推入元素（移动语义，减少拷贝开销）
     * @param item 要推入的元素（右值引用）
     * @param timeout_ms 超时时间(毫秒，-1表示无限等待)
     * @return 成功推入返回true，超时或队列已终止返回false
     */
    bool push(T&& item, int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待队列有空间或超时
        if (max_size_ > 0) {
            if (timeout_ms >= 0) {
                if (!not_full_.wait_for(lock, 
                                       std::chrono::milliseconds(timeout_ms), 
                                       [this] { 
                                           return queue_.size() < max_size_ || terminated_; 
                                       })) {
                    return false; // 超时
                }
            } else {
                not_full_.wait(lock, [this] { 
                    return queue_.size() < max_size_ || terminated_; 
                });
            }
        }
        
        // 检查是否已终止
        if (terminated_) return false;
        
        // 添加元素
        queue_.push(std::move(item));
        
        // 通知消费者
        not_empty_.notify_one();
        return true;
    }
    
    /**
     * @brief 从队列中取出元素
     * @param item 用于接收元素的引用
     * @param timeout_ms 超时时间(毫秒，-1表示无限等待)
     * @return 成功取出返回true，超时或队列终止且为空返回false
     */
    bool pop(T& item, int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待队列有元素或超时
        if (timeout_ms >= 0) {
            if (!not_empty_.wait_for(lock, 
                                    std::chrono::milliseconds(timeout_ms), 
                                    [this] { 
                                        return !queue_.empty() || terminated_; 
                                    })) {
                return false; // 超时
            }
        } else {
            not_empty_.wait(lock, [this] { 
                return !queue_.empty() || terminated_; 
            });
        }
        
        // 检查是否已终止
        if (terminated_ && queue_.empty()) return false;
        
        // 取出元素
        item = std::move(queue_.front());
        queue_.pop();
        
        // 通知生产者
        if (max_size_ > 0) {
            not_full_.notify_one();
        }
        return true;
    }
    
    // 尝试取出元素（非阻塞）
    // std::optional<T> try_pop() {
    //     std::unique_lock<std::mutex> lock(mutex_);
    //     if (queue_.empty()) {
    //         return std::nullopt;
    //     }
        
    //     T item = std::move(queue_.front());
    //     queue_.pop();
        
    //     if (max_size_ > 0) {
    //         not_full_.notify_one();
    //     }
    //     return item;
    // }
    
    /**
     * @brief 获取当前队列中的元素数量
     * @return 队列大小（线程安全）
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    /**
     * @brief 检查队列是否为空
     * @return 空返回true，否则返回false（线程安全）
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    /**
     * @brief 终止队列运行，唤醒所有等待的线程
     * @note 调用后队列不再接受新元素，已有的元素可以被取走
     */
    void terminate() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            terminated_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }
    
    /**
     * @brief 检查队列是否已终止
     * @return 已终止返回true，否则返回false
     */
    bool is_terminated() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return terminated_;
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;
    const size_t max_size_;
    bool terminated_;
};
