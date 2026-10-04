// pulse-injector - drives a gpio-sim line to emulate a meter LED, for hardware-in-the-loop tests.
// Linux "gpio-sim" kernel module exposes /sys/devices/platform/gpio-sim.N/gpiochipM/sim_gpioK/pull.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    std::string pull;
    double watts = 1000, imp = 1000;
    long count = 20;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pull" && i + 1 < argc) pull = argv[++i];
        else if (a == "--watts" && i + 1 < argc) watts = std::atof(argv[++i]);
        else if (a == "--imp" && i + 1 < argc) imp = std::atof(argv[++i]);
        else if (a == "--count" && i + 1 < argc) count = std::atol(argv[++i]);
    }
    if (pull.empty()) { std::cerr << "usage: pulse-injector --pull <sysfs .../sim_gpio0/pull> [--watts W] [--imp N] [--count C]\n"; return 2; }
    const double period_s = 3.6e6 / imp / watts;  // seconds between pulses at this load
    std::cout << "injecting " << count << " pulses, period " << period_s << " s (" << watts << " W)\n";
    auto next = std::chrono::steady_clock::now();
    for (long i = 0; i < count; ++i) {
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(period_s));
        std::this_thread::sleep_until(next);
        { std::ofstream f(pull); f << "pull-up"; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        { std::ofstream f(pull); f << "pull-down"; }
    }
    return 0;
}
