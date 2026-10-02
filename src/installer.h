#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "server.h"

struct InstallRequest {
    providers::Software sw = providers::Software::Paper;
    std::string mcVersion;
    std::string name;
    int port = 25565;
    int ramMB = 2048;
    std::string motd;
    bool acceptEula = false;
    fs::path dir;
};

// Downloads + sets up a brand new server on a worker thread.
class InstallJob {
public:
    enum class StepState { Pending, Active, Done, Failed, Skipped };
    struct Step {
        std::string label;
        StepState state = StepState::Pending;
        std::string detail;
    };

    explicit InstallJob(InstallRequest r);
    ~InstallJob();
    void cancel() { cancel_ = true; }

    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
    std::vector<Step> steps();
    float progress();                 // 0..1, <0 = indeterminate
    std::vector<std::string> logTail(size_t n = 200);
    std::string error();

    InstallRequest req;
    ServerConfig result;   // valid when finished && !failed

private:
    void run();
    bool fail(const std::string& msg);
    void setStep(size_t i, StepState s, const std::string& detail = "");
    void setProgress(float p);
    void addLog(const std::string& s);
    bool cancelled() const { return cancel_; }

    std::mutex mu_;
    std::vector<Step> steps_;
    float progress_ = -1.f;
    std::vector<std::string> log_;
    std::string error_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> failed_{false};
    std::thread thread_;
};
