#pragma once
// Minimal single-threaded HTTP/1.1 server on raw POSIX sockets (no external deps).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <functional>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include "smeter/analytics.hpp"
#include <map>
#include "smeter/agent_json.hpp"
#include "smeter/dashboard.hpp"

namespace sm {

inline std::string json_status(const Snapshot& s) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(3);
    o << "{\"state\":\"" << to_string(s.state) << "\",\"source\":\"" << s.source << "\""
      << ",\"power_w\":" << s.power_w << ",\"smooth_w\":" << s.smooth_w << ",\"peak_w\":" << s.peak_w
      << ",\"energy_kwh\":" << s.energy_kwh << ",\"cost\":" << s.cost << ",\"rate\":" << s.rate
      << ",\"pulses\":" << s.pulses << ",\"debounced\":" << s.debounced << ",\"out_of_order\":" << s.out_of_order
      << ",\"spikes\":" << s.spikes << ",\"overload_w\":" << s.overload_w << ",\"wall_s\":" << std::setprecision(0) << s.wall_s << ",\"events\":[";
    for (size_t i = 0; i < s.events.size(); ++i) o << (i ? "," : "") << "\"" << s.events[i] << "\"";
    o << "]}";
    return o.str();
}

inline std::string json_history(const std::vector<Bucket>& h) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2) << "{\"t\":[";
    for (size_t i = 0; i < h.size(); ++i) o << (i ? "," : "") << h[i].minute * 60;
    o << "],\"avg_w\":[";
    for (size_t i = 0; i < h.size(); ++i) o << (i ? "," : "") << h[i].avg_w();
    o << "],\"energy_wh\":[";
    for (size_t i = 0; i < h.size(); ++i) o << (i ? "," : "") << h[i].wh;
    o << "],\"peak_w\":[";
    for (size_t i = 0; i < h.size(); ++i) o << (i ? "," : "") << h[i].peak_w;
    o << "]}";
    return o.str();
}

inline std::string prometheus(const Snapshot& s) {
    std::ostringstream o;
    o << "# TYPE smeter_power_watts gauge\nsmeter_power_watts " << s.power_w
      << "\n# TYPE smeter_energy_kwh_total counter\nsmeter_energy_kwh_total " << s.energy_kwh
      << "\n# TYPE smeter_pulses_total counter\nsmeter_pulses_total " << s.pulses
      << "\n# TYPE smeter_spikes_total counter\nsmeter_spikes_total " << s.spikes
      << "\n# TYPE smeter_cost_total counter\nsmeter_cost_total " << s.cost
      << "\n# TYPE smeter_state gauge\nsmeter_state " << static_cast<int>(s.state) << "\n";
    return o.str();
}

inline std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else o += s[i];
    }
    return o;
}
inline std::map<std::string, std::string> parseQuery(const std::string& q) {
    std::map<std::string, std::string> m;
    std::istringstream is(q); std::string kv;
    while (std::getline(is, kv, '&')) { auto e = kv.find('='); if (e != std::string::npos) m[urlDecode(kv.substr(0, e))] = urlDecode(kv.substr(e + 1)); }
    return m;
}

class HttpServer {
public:
    HttpServer(Analytics& a, uint16_t port, std::string bind = "127.0.0.1") : a_(a), port_(port), bind_(std::move(bind)) {}
    ~HttpServer() { stop(); }
    void set_restart_handler(std::function<void()> f) { on_restart_ = std::move(f); }
    void set_agent(const EnergyAgent* ag) { agent_ = ag; }
    void set_reset_handler(std::function<void()> f) { on_reset_ = std::move(f); }

    bool start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0) return false;
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        inet_pton(AF_INET, bind_.c_str(), &addr.sin_addr);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd_, 16) < 0) {
            std::cerr << "[http] bind/listen: " << std::strerror(errno) << "\n";
            ::close(fd_); fd_ = -1;
            return false;
        }
        run_ = true;
        th_ = std::thread([this] { loop(); });
        return true;
    }
    void stop() {
        run_ = false;
        if (th_.joinable()) th_.join();
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    /// Route a request path to (status, content-type, body). Public for testing.
    std::string route(const std::string& path, int& status, std::string& ctype) const {
        status = 200; ctype = "application/json";
        // ---- Smart Energy Agent API (needs set_agent) ----
        if (agent_ && path.rfind("/api/", 0) == 0) {
            const auto qp = path.find('?');
            const std::string base = path.substr(0, qp);
            const auto q = parseQuery(qp == std::string::npos ? "" : path.substr(qp + 1));
            auto num_or = [&](const char* k, double d) { auto it = q.find(k); return it == q.end() || it->second.empty() ? d : std::atof(it->second.c_str()); };
            if (base == "/api/agent") { auto s = a_.snapshot(); return agentJson(agent_->report(s.wall_s, s.power_w), agent_->tariff()); }
            if (base == "/api/appliances") { return rankingJson(rankAppliancesByConsumption(agent_->appliances(), agent_->tariff())); }
            if (base == "/api/tariff") return tariffJson(agent_->tariff());
            if (base == "/api/whatif") {
                auto it = q.find("appliance");
                auto s = a_.snapshot();
                const Appliance* ap = it == q.end() ? nullptr : findAppliance(agent_->appliances(), it->second);
                if (!ap) { status = 400; return "{\"error\":\"unknown appliance\"}"; }
                return whatIfJson(agent_->whatIf(ap->name, num_or("hours", ap->hours_per_day), s.wall_s), agent_->tariff().currency);
            }
            if (base == "/api/bill") {
                BillCalcInput in;
                in.units = num_or("units", 0); in.days = num_or("days", 1); in.prev_units = num_or("prev", -1);
                in.avg_daily = num_or("avg", -1); in.month_days = num_or("month_days", 30);
                auto r = smartBillCalculator(agent_->tariff(), in);
                if (!r.valid) status = 400;
                return billCalcJson(r, agent_->tariff());
            }
        }
        if (path == "/" || path == "/index.html") { ctype = "text/html; charset=utf-8"; return dashboard_html(); }
        if (path == "/api/status") return json_status(a_.snapshot());
        if (path.rfind("/api/history", 0) == 0) {
            std::size_t n = 60;
            auto q = path.find("n=");
            if (q != std::string::npos) n = std::min<std::size_t>(1440, std::strtoul(path.c_str() + q + 2, nullptr, 10));
            return json_history(a_.history(n));
        }
        if (path == "/metrics") { ctype = "text/plain; version=0.0.4"; return prometheus(a_.snapshot()); }
        if (path == "/healthz") { ctype = "text/plain"; return "ok\n"; }
        status = 404;
        return "{\"error\":\"not found\"}";
    }

private:
    void loop() {
        while (run_) {
            pollfd p{fd_, POLLIN, 0};
            if (poll(&p, 1, 200) <= 0) continue;
            int c = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) continue;
            timeval tv{1, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            char buf[2048];
            ssize_t n = ::recv(c, buf, sizeof buf - 1, 0);
            if (n > 0) {
                buf[n] = 0;
                std::string req(buf);
                std::string method, path;
                std::istringstream is(req);
                is >> method >> path;
                int st; std::string ct;
                std::string body;
                if (method == "GET") body = route(path, st, ct);
                else if (method == "POST" && (path == "/api/restart" || path == "/api/reset")) {
                    // custom header forces a CORS preflight, so other websites cannot trigger a restart
                    if (req.find("X-Smeter:") == std::string::npos) { st = 403; ct = "application/json"; body = "{\"error\":\"missing X-Smeter header\"}"; }
                    else { auto& f = path == "/api/restart" ? on_restart_ : on_reset_; if (f) f(); st = 200; ct = "application/json"; body = "{\"ok\":true}"; }
                } else { st = 405; ct = "text/plain"; body = "method not allowed\n"; }
                std::ostringstream r;
                r << "HTTP/1.1 " << st << (st == 200 ? " OK" : " Error") << "\r\nContent-Type: " << ct
                  << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
                const std::string out = r.str();
                (void)!::send(c, out.data(), out.size(), MSG_NOSIGNAL);
            }
            ::close(c);
        }
    }
    Analytics& a_;
    const EnergyAgent* agent_ = nullptr;
    std::function<void()> on_restart_, on_reset_;
    uint16_t port_;
    std::string bind_;
    int fd_ = -1;
    std::atomic<bool> run_{false};
    std::thread th_;
};

}  // namespace sm
