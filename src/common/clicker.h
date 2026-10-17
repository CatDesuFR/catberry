// The autoclicker's clicking thread.
#pragma once

#include "output.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

const double kMinCps = 0.1;
const double kMaxCps = 1000.0;

class Clicker {
public:
    explicit Clicker(Output &output);
    ~Clicker();

    bool running() const { return running_; }
    void setRunning(bool on);
    double cps() const { return cps_; }
    void setCps(double cps);
    int code() const { return code_; }
    void setCode(int code) { code_ = code; }

private:
    void loop();

    Output &output_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::atomic<double> cps_{10.0};
    std::atomic<int> code_{0};
    std::thread thread_;
};
