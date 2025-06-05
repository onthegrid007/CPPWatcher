#ifndef WATCHER_HPP_
#define WATCHER_HPP_

#include "vendor/FileUtilities/FileUtilities.hpp"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>

class Watcher {
    public:
    typedef std::chrono::milliseconds DelayType;
    typedef std::function<void(const FileUtilities::ParsedPath&)> CallbackType;
    enum Depth : bool {
        SHALLOW = false,
        RECURSIVE = true
    };

    Watcher(const FileUtilities::ParsedPath& basePath, const CallbackType onCreate, const CallbackType onModify, const CallbackType onDelete, const Depth& depth, const bool alwaysCreateBaseDirectory = true, const bool& startImmediatly = false, const DelayType& delay = std::chrono::milliseconds(1000)) :
        m_basePath(basePath),
        m_onCreate(onCreate),
        m_onModify(onModify),
        m_onDelete(onDelete),
        m_depth(depth),
        m_delay(delay) {
        if(startImmediatly) start(basePath, alwaysCreateBaseDirectory);
    }

    void start(const FileUtilities::ParsedPath& path, const bool& alwaysCreateBaseDirectory = true) {
        m_running = true;
        if (m_depth) {
            for (const auto& content : FileUtilities::DirContentsRecursive(path))
                if (content.is_regular_file())
                    m_times[content.path().string()] = std::filesystem::last_write_time(content);
        }
        else {
            for (const auto& content : FileUtilities::DirContents(path))
                if (content.is_regular_file())
                    m_times[content.path().string()] = std::filesystem::last_write_time(content);
        }
        for (const auto& content : FileUtilities::DirContents(path)) {
            if (content.is_directory()) {
                start({content.path().string(), FileUtilities::ParsedPath::ABS{}}, false);
                continue;
            }
            std::lock_guard<std::mutex> lock(m_threadMtx);
            std::thread(
                [this, toWatch = path](){
                    while(m_running) {
                        std::this_thread::sleep_for(m_delay.load());

                        // Delete
                        auto it{m_times.begin()};
                        while (it != m_times.end()) {
                            if (!FileUtilities::FileExists(it->first)) {
                                m_onDelete(it->first);
                                it = m_times.erase(it);
                            }
                            else {
                                it++;
                            }
                        }

                        for (const auto& content : FileUtilities::DirContents(toWatch)) {
                            if (content.is_regular_file()) {
                                const auto lastModifyTime{FileUtilities::GetModifyTime(content)};
                                const FileUtilities::ParsedPath pp{content.path().string(), FileUtilities::ParsedPath::ABS{}};
                                const auto it{m_times.find(pp)};

                                // Create
                                if (it == m_times.end()) {
                                    m_times[pp] = lastModifyTime;
                                    m_onCreate(pp);
                                }
                                // Modify
                                else if (m_times[pp] != lastModifyTime) {
                                    m_times[pp] = lastModifyTime;
                                    m_onModify(pp);
                                }
                            }
                        }
                    }
                }
            ).detach();
        }
    }
    
    void stop() {
        const auto clearDelay{getDelay() * 2};
        setDelay(std::chrono::milliseconds(1));
        m_running = false;
        std::this_thread::sleep_for(clearDelay);
        {
            std::lock_guard<std::mutex> lock(m_timeMtx);
            m_times.clear();
        }
    }

    void setDelay(const DelayType& delay = std::chrono::milliseconds(1000)) {
        m_delay = delay;
    }

    const DelayType getDelay() const {
        return m_delay;
    }

    private:
    std::atomic<bool> m_running{false};
    std::mutex m_timeMtx;
    std::unordered_map<FileUtilities::ParsedPath, FileUtilities::TimeType> m_times;
    const FileUtilities::ParsedPath m_basePath;
    const CallbackType m_onCreate;
    const CallbackType m_onModify;
    const CallbackType m_onDelete;
    const Depth m_depth;
    std::atomic<DelayType> m_delay;
};

#endif