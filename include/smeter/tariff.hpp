#pragma once
// Slab (telescopic) electricity tariff: calculateBill(), getCurrentSlab(), getUnitsToNextSlab().
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace sm {

constexpr double kInf = 1e18;  // "no upper limit" for the last slab

struct Slab { double upto; double rate; };  // upto = cumulative upper bound in units (kWh)

/// Demo tariff (editable): 1-100 @2.50, 101-200 @3.50, 201-300 @5.00, 300+ @7.00, plus a fixed charge.
struct SlabTariff {
    std::vector<Slab> slabs{{100, 2.50}, {200, 3.50}, {300, 5.00}, {kInf, 7.00}};
    double fixed_charge = 30.0;
    std::string currency = "\xE2\x82\xB9";  // "₹"
};

struct BillLine { int slab; double from, to, units, rate, amount; };
struct Bill {
    std::vector<BillLine> lines;
    double units = 0, energy_charge = 0, fixed_charge = 0, total = 0;
};

inline std::string num(double v, int dec = 0) {
    std::ostringstream o; o << std::fixed << std::setprecision(dec) << v; return o.str();
}
/// "8" for whole numbers, "2.5" otherwise.
inline std::string numAuto(double v) { return num(v, std::fabs(v - std::round(v)) < 0.05 ? 0 : 1); }

/// Telescopic billing: each block of units is charged at its own slab rate.
inline Bill calculateBill(const SlabTariff& t, double units) {
    Bill b;
    b.units = std::max(0.0, units);
    double lo = 0;
    for (size_t i = 0; i < t.slabs.size() && lo < b.units; ++i) {
        double hi = std::min(b.units, t.slabs[i].upto);
        if (hi > lo) {
            double u = hi - lo;
            b.lines.push_back({static_cast<int>(i), lo, hi, u, t.slabs[i].rate, u * t.slabs[i].rate});
            b.energy_charge += u * t.slabs[i].rate;
        }
        lo = t.slabs[i].upto;
    }
    b.fixed_charge = b.units > 0 ? t.fixed_charge : 0.0;
    b.total = b.energy_charge + b.fixed_charge;
    return b;
}

/// Index of the slab that `units` falls in (slab i covers (upto[i-1], upto[i]]).
inline int getCurrentSlab(const SlabTariff& t, double units) {
    for (size_t i = 0; i < t.slabs.size(); ++i) if (units <= t.slabs[i].upto) return static_cast<int>(i);
    return static_cast<int>(t.slabs.size()) - 1;
}

/// Units left before the next slab starts; -1 when already in the last slab.
inline double getUnitsToNextSlab(const SlabTariff& t, double units) {
    int i = getCurrentSlab(t, units);
    if (i >= static_cast<int>(t.slabs.size()) - 1) return -1.0;
    return t.slabs[i].upto - units;
}

inline std::string slabLabel(const SlabTariff& t, int i) {
    double lo = i == 0 ? 0 : t.slabs[i - 1].upto;
    if (t.slabs[i].upto >= kInf / 2) return num(lo, 0) + "+";
    return num(lo + 1, 0) + "-" + num(t.slabs[i].upto, 0);
}

}  // namespace sm
