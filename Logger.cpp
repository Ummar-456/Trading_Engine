#include "Logger.h"

#include <cstring>
#include <algorithm>

namespace hft {

Logger::Logger(const char* path) {
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_.is_open())
        enabled_.store(false, std::memory_order_relaxed);
}

Logger::~Logger() {
    stop();
    if (file_.is_open()) file_.close();
}

void Logger::start() {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] {
        thread_launched_.store(true, std::memory_order_release);
        drain_loop();
    });
}

void Logger::stop() noexcept {
    running_.store(false, std::memory_order_release);
    if (thread_launched_.load(std::memory_order_acquire) && thread_.joinable())
        thread_.join();
    flush_remaining();
}

void Logger::set_enabled(bool e) noexcept {
    enabled_.store(e, std::memory_order_relaxed);
}

void Logger::log(std::string_view msg) noexcept {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    Msg m;
    m.len = static_cast<uint16_t>(
        std::min(msg.size(), static_cast<std::size_t>(Msg::MAX_DATA)));
    std::memcpy(m.data, msg.data(), m.len);
    (void)queue_.try_push(std::move(m));
}

void Logger::drain_loop() noexcept {
    Msg m;
    while (true) {
        if (queue_.try_pop(m)) {
            file_.write(m.data, m.len);
            file_.put('\n');
        } else {
            if (!running_.load(std::memory_order_acquire)) break;
            std::this_thread::yield();
        }
    }
    file_.flush();
}

void Logger::flush_remaining() noexcept {
    Msg m;
    while (queue_.try_pop(m)) {
        if (file_.is_open()) {
            file_.write(m.data, m.len);
            file_.put('\n');
        }
    }
    if (file_.is_open()) file_.flush();
}

} // namespace hft
