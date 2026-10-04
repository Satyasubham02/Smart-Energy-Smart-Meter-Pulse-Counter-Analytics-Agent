#pragma once
// Persistence: crash-safe counter state (write-temp + fsync + rename) and a CSV minute log.
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include "smeter/analytics.hpp"
#include "smeter/calendar.hpp"

namespace sm {

struct PersistedState { uint64_t pulses = 0; double energy_wh = 0, cost = 0; };

class Storage {
public:
    explicit Storage(std::string dir) : dir_(std::move(dir)) {}

    bool save_state(const PersistedState& s) const {
        const std::string tmp = dir_ + "/smeter.state.tmp", fin = dir_ + "/smeter.state";
        std::ostringstream o;
        o.precision(10);
        o << "pulses=" << s.pulses << "\nenergy_wh=" << s.energy_wh << "\ncost=" << s.cost << "\n";
        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return false;
        const std::string d = o.str();
        bool ok = ::write(fd, d.data(), d.size()) == static_cast<ssize_t>(d.size()) && ::fsync(fd) == 0;
        ::close(fd);
        return ok && std::rename(tmp.c_str(), fin.c_str()) == 0;  // atomic replace
    }

    bool load_state(PersistedState& s) const {
        std::ifstream f(dir_ + "/smeter.state");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto k = line.substr(0, eq);
            auto v = line.substr(eq + 1);
            if (k == "pulses") s.pulses = std::stoull(v);
            else if (k == "energy_wh") s.energy_wh = std::stod(v);
            else if (k == "cost") s.cost = std::stod(v);
        }
        return true;
    }

    /// Hourly usage history (day, 24 x kWh) - lets the agent keep its month/week data across restarts.
    bool save_usage(const std::map<long, Hours>& m) const {
        const std::string tmp = dir_ + "/usage_hourly.csv.tmp", fin = dir_ + "/usage_hourly.csv";
        { std::ofstream f(tmp);
          if (!f) return false;
          f.precision(6);
          for (auto& [day, h] : m) { f << day; for (double v : h) f << ',' << v; f << '\n'; } }
        return std::rename(tmp.c_str(), fin.c_str()) == 0;
    }
    bool load_usage(std::map<long, Hours>& m) const {
        std::ifstream f(dir_ + "/usage_hourly.csv");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream is(line); std::string tok; Hours h{}; long day = 0; int i = -1;
            while (std::getline(is, tok, ',')) { if (i < 0) day = std::stol(tok); else if (i < 24) h[i] = std::stod(tok); ++i; }
            if (i == 24) m[day] = h;
        }
        return !m.empty();
    }

    void append_bucket(const Bucket& b) const {
        const std::string path = dir_ + "/energy_minutes.csv";
        bool fresh = ::access(path.c_str(), F_OK) != 0;
        std::ofstream f(path, std::ios::app);
        if (fresh) f << "minute_epoch,pulses,energy_wh,avg_w,peak_w,cost\n";
        f << b.minute * 60 << ',' << b.pulses << ',' << b.wh << ',' << b.avg_w() << ',' << b.peak_w << ',' << b.cost << '\n';
    }

private:
    std::string dir_;
};

}  // namespace sm
