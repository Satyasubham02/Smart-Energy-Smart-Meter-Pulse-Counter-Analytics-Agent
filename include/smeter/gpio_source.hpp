#pragma once
// Userspace driver for a metering-pulse input using the Linux GPIO character-device
// ABI (linux/gpio.h, v2). Concepts used: character device node, ioctl line request,
// kernel-side edge detection + timestamping + debounce, poll()/eventfd wakeup.
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>
#include "smeter/pulse_source.hpp"

namespace sm {

class GpioCdevSource : public PulseSource {
public:
    GpioCdevSource(std::string chip, unsigned line, unsigned debounce_us = 5000)
        : chip_(std::move(chip)), line_(line), debounce_us_(debounce_us) {}
    ~GpioCdevSource() override { stop(); }

    bool start(PulseQueue& q) override {
        chip_fd_ = ::open(chip_.c_str(), O_RDONLY | O_CLOEXEC);
        if (chip_fd_ < 0) { err("open " + chip_); return false; }

        gpio_v2_line_request req{};
        req.offsets[0] = line_;
        req.num_lines = 1;
        std::strncpy(req.consumer, "smeterd", sizeof(req.consumer) - 1);
        req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
                           GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        req.config.num_attrs = 1;                                   // kernel-side debounce
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
        req.config.attrs[0].attr.debounce_period_us = debounce_us_;
        req.config.attrs[0].mask = 1;
        if (ioctl(chip_fd_, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { err("GPIO_V2_GET_LINE_IOCTL"); return false; }
        line_fd_ = req.fd;

        stop_fd_ = eventfd(0, EFD_CLOEXEC);
        wall0_ = static_cast<double>(std::time(nullptr));
        th_ = std::thread([this, &q] { loop(q); });
        return true;
    }

    void stop() override {
        if (stop_fd_ >= 0) { uint64_t one = 1; (void)!::write(stop_fd_, &one, sizeof one); }
        if (th_.joinable()) th_.join();
        for (int* fd : {&line_fd_, &chip_fd_, &stop_fd_}) if (*fd >= 0) { ::close(*fd); *fd = -1; }
    }
    std::string name() const override { return "gpio-cdev:" + chip_ + ":" + std::to_string(line_); }
    double wall_start_s() const override { return wall0_; }

private:
    void err(const std::string& what) {
        std::cerr << "[gpio] " << what << ": " << std::strerror(errno) << "\n";
        failed = true;
    }
    void loop(PulseQueue& q) {
        pollfd fds[2] = {{line_fd_, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
        gpio_v2_line_event ev[16];
        for (;;) {
            if (poll(fds, 2, -1) < 0) { if (errno == EINTR) continue; err("poll"); return; }
            if (fds[1].revents) return;
            if (fds[0].revents & POLLIN) {
                ssize_t n = ::read(line_fd_, ev, sizeof ev);
                if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; err("read"); return; }
                for (size_t i = 0; i < static_cast<size_t>(n) / sizeof(ev[0]); ++i)
                    if (ev[i].id == GPIO_V2_LINE_EVENT_RISING_EDGE) q.push(Pulse{ev[i].timestamp_ns});
            }
            if (fds[0].revents & (POLLERR | POLLNVAL)) { err("line fd error"); return; }
        }
    }
    std::string chip_;
    unsigned line_, debounce_us_;
    int chip_fd_ = -1, line_fd_ = -1, stop_fd_ = -1;
    double wall0_ = 0;
    std::thread th_;
};

}  // namespace sm
