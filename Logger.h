#pragma once

#include "SPSCQueue.h"

#include <fstream>
#include <thread>
#include <atomic>
#include <string_view>
#include <cstring>

namespace hft {

class Logger {
    struct Msg {
        static constexpr std::size_t MAX_DATA = 254;
        uint16_t len{0};
        char     data[MAX_DATA]{};
    };

    static constexpr std::size_t QUEUE_CAP = 8192;

    SPSCQueue<Msg, QUEUE_CAP> queue_;
    std::ofstream             file_;
    std::thread               thread_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         thread_launched_{false};
    std::atomic<bool>         enabled_{true};

    void drain_loop() noexcept;
    void flush_remaining() noexcept;

public:
    explicit Logger(const char* path = "engine.log");
    ~Logger();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    void start();
    void stop() noexcept;
    void set_enabled(bool e) noexcept;

    void log(std::string_view msg) noexcept;
};

} // namespace hft
