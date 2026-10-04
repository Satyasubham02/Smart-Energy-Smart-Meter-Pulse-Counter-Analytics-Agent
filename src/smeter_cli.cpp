// smeter-cli - terminal dashboard client for smeterd (talks HTTP/JSON)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <vector>

static std::string http_get(const std::string& host, int port, const std::string& path) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &a.sin_addr);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) { if (fd >= 0) ::close(fd); return ""; }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    (void)!::send(fd, req.data(), req.size(), 0);
    std::string resp; char buf[4096]; ssize_t n;
    while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) resp.append(buf, n);
    ::close(fd);
    auto p = resp.find("\r\n\r\n");
    return p == std::string::npos ? "" : resp.substr(p + 4);
}
static double jnum(const std::string& j, const std::string& k) {
    auto p = j.find("\"" + k + "\":");
    return p == std::string::npos ? 0 : std::atof(j.c_str() + p + k.size() + 3);
}
static std::string jstr(const std::string& j, const std::string& k) {
    auto p = j.find("\"" + k + "\":\"");
    if (p == std::string::npos) return "";
    p += k.size() + 4;
    return j.substr(p, j.find('"', p) - p);
}
static std::vector<double> jarr(const std::string& j, const std::string& k) {
    std::vector<double> v;
    auto p = j.find("\"" + k + "\":[");
    if (p == std::string::npos) return v;
    p += k.size() + 4;
    auto e = j.find(']', p);
    const char* s = j.c_str() + p; const char* end = j.c_str() + e;
    while (s < end) { char* nx; double d = std::strtod(s, &nx); if (nx == s) break; v.push_back(d); s = nx; if (*s == ',') ++s; }
    return v;
}
static std::vector<std::string> jevents(const std::string& j) {
    std::vector<std::string> out;
    auto p = j.find("\"events\":[");
    if (p == std::string::npos) return out;
    p += 10;
    auto e = j.find(']', p);
    while (p < e) {
        auto q1 = j.find('"', p); if (q1 == std::string::npos || q1 > e) break;
        auto q2 = j.find('"', q1 + 1);
        out.push_back(j.substr(q1 + 1, q2 - q1 - 1));
        p = q2 + 1;
    }
    return out;
}
static std::string spark(const std::vector<double>& v, size_t width) {
    static const char* blocks[] = {"▁","▂","▃","▄","▅","▆","▇","█"};
    if (v.empty()) return "";
    size_t start = v.size() > width ? v.size() - width : 0;
    double mx = 1; for (size_t i = start; i < v.size(); ++i) mx = std::max(mx, v[i]);
    std::string s;
    for (size_t i = start; i < v.size(); ++i) s += blocks[std::min(7, static_cast<int>(v[i] / mx * 7.999))];
    return s;
}

static bool g_color = true;
static std::string paint(const char* code, const std::string& t) { return g_color ? std::string("\033[") + code + "m" + t + "\033[0m" : t; }
static std::string gauge(double v, double max, int w = 28) {
    double f = max > 0 ? v / max : 0;
    int n = std::max(0, std::min(w, static_cast<int>(f * w + 0.5)));
    std::string bar;
    for (int i = 0; i < w; ++i) bar += i < n ? "█" : "░";
    return paint(f > 0.9 ? "31" : f > 0.6 ? "33" : "32", bar);
}
static std::string jsec(const std::string& j, const std::string& k) {   // flat {...} section of /api/agent
    auto p = j.find("\"" + k + "\":{");
    if (p == std::string::npos) return "";
    return j.substr(p, j.find('}', p) - p + 1);
}
static std::vector<std::string> jtitles(const std::string& j, size_t max) {   // recommendation titles
    std::vector<std::string> out;
    auto p = j.find("\"recommendations\":[");
    if (p == std::string::npos) return out;
    auto end = j.find("\"analytics\"", p);
    while (out.size() < max) {
        p = j.find("\"title\":\"", p);
        if (p == std::string::npos || (end != std::string::npos && p > end)) break;
        p += 9;
        out.push_back(j.substr(p, j.find('"', p) - p));
    }
    return out;
}
static const char* zone_color(const std::string& z) { return z == "HIGH" ? "31" : z == "APPROACHING" ? "33" : "32"; }

int main(int argc, char** argv) {
    std::string host = "127.0.0.1"; int port = 8099; double interval = 1; bool once = false, compact = false, show_agent = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--interval" && i + 1 < argc) interval = std::atof(argv[++i]);
        else if (a == "--once") once = true;
        else if (a == "--compact") compact = true;
        else if (a == "--no-color") g_color = false;
        else if (a == "--no-agent") show_agent = false;
        else { std::cout << "usage: smeter-cli [--host H] [--port P] [--interval S] [--once] [--compact] [--no-color] [--no-agent]\n"; return a == "--help" ? 0 : 2; }
    }
    if (!isatty(STDOUT_FILENO) || std::getenv("NO_COLOR")) g_color = false;
    for (;;) {
        std::string st = http_get(host, port, "/api/status");
        if (st.empty()) { std::cerr << "cannot reach smeterd at " << host << ":" << port << "\n"; return 1; }
        std::string hi = http_get(host, port, "/api/history?n=60");
        if (!once) std::printf("\033[2J\033[H");
        time_t w = static_cast<time_t>(jnum(st, "wall_s")); char tb[32]; std::strftime(tb, sizeof tb, "%H:%M:%S", localtime(&w));
        const std::string state = jstr(st, "state");
        const char* sc = state == "OVERLOAD" ? "41;97" : state == "MEASURING" ? "42;30" : state == "FAULT" ? "43;30" : "100;97";
        const double pw = jnum(st, "power_w"), lim = jnum(st, "overload_w");
        std::string ag = show_agent ? http_get(host, port, "/api/agent") : "";
        const std::string stt = jsec(ag, "status"), prd = jsec(ag, "prediction");
        std::string cur = jstr(ag, "cur"); if (cur.empty() || cur[0] == '\\') cur = "Rs ";
        if (compact) {
            std::printf("%s %.0fW  %.3f kWh  %s%.2f", state.c_str(), pw, jnum(st, "energy_kwh"), cur.c_str(), jnum(st, "cost"));
            if (!stt.empty()) std::printf("  | month %.0f units, bill %s%.0f -> %s%.0f  [%s]", jnum(stt, "units"), cur.c_str(), jnum(stt, "bill"), cur.c_str(), jnum(prd, "bill"), jstr(stt, "zone").c_str());
            std::printf("\n");
        } else {
            std::printf("%s  %s  source: %s  meter-time: %s\n", paint("1;36", "⚡ SMART METER AGENT").c_str(), paint(sc, " " + state + " ").c_str(), jstr(st, "source").c_str(), tb);
            std::printf("%s\n", paint("90", "──────────────────────────────────────────────────────────────").c_str());
            std::printf("Power   %s %6.0f W  %s\n", gauge(pw, lim).c_str(), pw, paint("90", "limit " + std::to_string(static_cast<int>(lim)) + " W, peak " + std::to_string(static_cast<int>(jnum(st, "peak_w"))) + " W").c_str());
            std::printf("Energy  %.3f kWh   Cost %s%.2f @ %.2f/kWh   %s\n", jnum(st, "energy_kwh"), cur.c_str(), jnum(st, "cost"), jnum(st, "rate"),
                        paint("90", "pulses=" + std::to_string(static_cast<long>(jnum(st, "pulses"))) + " bounce=" + std::to_string(static_cast<long>(jnum(st, "debounced"))) + " spikes=" + std::to_string(static_cast<long>(jnum(st, "spikes")))).c_str());
            std::printf("Trend   %s\n", paint("36", spark(jarr(hi, "avg_w"), 60)).c_str());
            if (!stt.empty()) {
                const std::string z = jstr(stt, "zone");
                std::printf("%s\n", paint("90", "──────────────────────────────────────────────────────────────").c_str());
                std::printf("Month   %.1f units   bill %s%.0f   projected %.0f units / %s%.0f\n", jnum(stt, "units"), cur.c_str(), jnum(stt, "bill"), jnum(prd, "units"), cur.c_str(), jnum(prd, "bill"));
                std::printf("Slab    %s   %s   %.0f units to next slab\n", jstr(stt, "slab_label").c_str(), paint(zone_color(z), "● " + z).c_str(), jnum(stt, "to_next"));
                int i = 1;
                for (const auto& t : jtitles(ag, 3)) std::printf("Advice%d %s\n", i++, t.c_str());
            }
            std::printf("%s\n", paint("90", "Events").c_str());
            auto ev = jevents(st);
            for (size_t i = ev.size() > 5 ? ev.size() - 5 : 0; i < ev.size(); ++i) std::printf("  %s\n", ev[i].c_str());
        }
        std::fflush(stdout);
        if (once) return 0;
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
}
