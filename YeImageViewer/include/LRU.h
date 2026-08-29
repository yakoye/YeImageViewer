#pragma once
#include <unordered_map>
#include <list>
#include <deque>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <cstdint>
#include <shared_mutex>
#include <vector>

template<typename keyType, typename valueType>
class LRU {
private:
    // 使用shared_ptr包装数据，确保数据不会被意外释放
    using ValuePtr = std::shared_ptr<valueType>;
    using ListIterator = typename std::list<std::pair<keyType, ValuePtr>>::iterator;

    // 原有的缓存结构，但使用shared_ptr
    std::unordered_map<keyType, ListIterator> cache_map;
    std::list<std::pair<keyType, ValuePtr>> cache_list;
    size_t CAPACITY = 5;

    // 预读取相关的成员
    struct PreloadTask {
        keyType key;
        std::uint64_t generation;
    };

    std::thread preload_thread;
    // 用 deque 而非 queue：当前要显示的图需要插到队首，不能排在已有预读任务后面
    std::deque<PreloadTask> preload_queue;
    std::unordered_map<keyType, std::uint64_t> preload_pending;
    mutable std::shared_mutex cache_mutex;  // 使用读写锁提高性能
    std::mutex preload_mutex;
    std::condition_variable preload_cv;
    // 缓存写入后唤醒等待方，取代固定间隔轮询
    std::condition_variable_any cache_cv;
    std::atomic<bool> stop_preload{ false };
    std::uint64_t preload_generation = 0;

    void erasePendingLocked(const PreloadTask& task) {
        auto it = preload_pending.find(task.key);
        if (it != preload_pending.end() && it->second == task.generation) {
            preload_pending.erase(it);
        }
    }

    // 预读取工作线程函数
    void preloadWorker() {
        while (true) {
            std::unique_lock<std::mutex> lock(preload_mutex);
            preload_cv.wait(lock, [this] { return !preload_queue.empty() || stop_preload.load(); });

            if (stop_preload) break;

            PreloadTask task = std::move(preload_queue.front());
            preload_queue.pop_front();

            {
                std::shared_lock<std::shared_mutex> cache_lock(cache_mutex);
                if (cache_map.contains(task.key)) {
                    erasePendingLocked(task);
                    continue;
                }
            }
            lock.unlock();


            valueType value = loader(task.key);
            auto value_ptr = std::make_shared<valueType>(std::move(value));

            lock.lock();
            if (!stop_preload && task.generation == preload_generation) {
                std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);
                putInternal(task.key, value_ptr);
            }
            erasePendingLocked(task);
            lock.unlock();
        }
    }

    // 内部put函数，不加锁版本
    void putInternal(const keyType& key, ValuePtr value_ptr) {
        auto it = cache_map.find(key);
        if (it != cache_map.end()) {
            it->second->second = value_ptr;
            cache_list.splice(cache_list.begin(), cache_list, it->second);
        }
        else {
            if (cache_map.size() >= CAPACITY) {
                cache_map.erase(cache_list.back().first);
                cache_list.pop_back();
            }
            cache_list.emplace_front(key, value_ptr);
            cache_map[key] = cache_list.begin();
        }

        // 唤醒 getDataPtr 中等待该 key 的调用方
        cache_cv.notify_all();
    }

public:
    LRU() {
        preload_thread = std::thread(&LRU::preloadWorker, this);
    }

    virtual ~LRU() {
        stop_preload = true;
        preload_cv.notify_all();
        if (preload_thread.joinable()) {
            preload_thread.join();
        }
    }

    // 禁用拷贝构造和赋值
    LRU(const LRU&) = delete;
    LRU& operator=(const LRU&) = delete;

    virtual valueType loader(const keyType&) = 0;

    std::shared_ptr<valueType> getDataPtr(const keyType& key) {
        // 原先按 10ms 轮询，受 Windows 计时器精度限制每轮实际约 15.6ms，
        // 即使解码早已完成也要等到下一轮才返回。改为由写入方唤醒，
        // 超时上限仍保持 60 秒。
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        while (true) {
            auto it = cache_map.find(key);
            if (it != cache_map.end()) {
                cache_list.splice(cache_list.begin(), cache_list, it->second);
                return it->second->second;
            }

            if (cache_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }

        // 超时判定与最后一次写入可能相邻，这里再查一次避免错过
        auto it = cache_map.find(key);
        if (it != cache_map.end()) {
            cache_list.splice(cache_list.begin(), cache_list, it->second);
            return it->second->second;
        }
        return nullptr;
    }

    std::shared_ptr<valueType> getSafePtr(const keyType& key) {
        requestPreload(key, true);
        return getDataPtr(key);
    }

    std::shared_ptr<valueType> getSafePtr(const keyType& key, const keyType& nextKey) {
        // 当前要显示的图插到队首：快速翻页时队列里可能还堆着此前的预读任务，
        // 按先进先出会让正在等的这张排到最后，等成超时。
        requestPreload(key, true);
        if (key != nextKey)
            requestPreload(nextKey);

        return getDataPtr(key);
    }

    // 请求预读取指定的key。urgent 表示这是当前要显示的图，插到队首优先解码
    void requestPreload(const keyType& key, bool urgent = false) {
        std::lock_guard<std::mutex> lock(preload_mutex);

        if (preload_pending.contains(key))
            return;

        // 使用读锁检查缓存
        {
            std::shared_lock<std::shared_mutex> cache_lock(cache_mutex);
            if (cache_map.contains(key)) {
                return;
            }
        }

        if (urgent)
            preload_queue.push_front(PreloadTask{ key, preload_generation });
        else
            preload_queue.push_back(PreloadTask{ key, preload_generation });
        preload_pending[key] = preload_generation;
        preload_cv.notify_one();
    }

    // 批量预读取
    void requestPreloadBatch(const std::vector<keyType>& keys) {
        std::lock_guard<std::mutex> lock(preload_mutex);
        bool hasNewTask = false;

        for (const auto& key : keys) {
            if (preload_pending.find(key) != preload_pending.end()) {
                continue;
            }

            {
                std::shared_lock<std::shared_mutex> cache_lock(cache_mutex);
                if (cache_map.find(key) != cache_map.end()) {
                    continue;
                }
            }

            preload_queue.push_back(PreloadTask{ key, preload_generation });
            preload_pending[key] = preload_generation;
            hasNewTask = true;
        }

        if (hasNewTask) {
            preload_cv.notify_all();
        }
    }

    void put(const keyType& key, valueType&& value) {
        auto value_ptr = std::make_shared<valueType>(std::move(value));
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        putInternal(key, value_ptr);
    }

    void clear() {
        std::lock_guard<std::mutex> preload_lock(preload_mutex);
        std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);

        ++preload_generation;

        cache_map.clear();
        cache_list.clear();

        std::deque<PreloadTask> empty_queue;
        preload_queue.swap(empty_queue);
        preload_pending.clear();
    }

    // 使指定条目失效。文件被删除或重命名后必须调用，否则之后出现的同名文件
    // 会命中已经过期的缓存内容。
    void erase(const keyType& key) {
        std::lock_guard<std::mutex> preload_lock(preload_mutex);
        std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);

        // 预读线程可能正在解码该 key，递增代号让在途结果不再写回缓存
        ++preload_generation;

        auto it = cache_map.find(key);
        if (it != cache_map.end()) {
            cache_list.erase(it->second);
            cache_map.erase(it);
        }

        // 队列中任务的代号已过期，执行也不会写回，直接丢弃
        std::deque<PreloadTask> empty_queue;
        preload_queue.swap(empty_queue);
        preload_pending.clear();
    }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(cache_mutex);
        return cache_map.size();
    }

    void setCapacity(size_t capacity) {
        if (capacity < 3 || capacity > 4096)
            capacity = 3;

        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        CAPACITY = capacity;

        while (cache_map.size() > CAPACITY) {
            cache_map.erase(cache_list.back().first);
            cache_list.pop_back();
        }
    }
};
