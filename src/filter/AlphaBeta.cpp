#include "filter/AlphaBeta.hpp"
#include "core/Error.hpp"

#include <cmath>
#include <format>

namespace opir {

AlphaBetaFilter::AlphaBetaFilter(double row, double col, AlphaBetaParams p)
    : x_{row, col, 0.0, 0.0}, p_{p} {}

void AlphaBetaFilter::predict(double dt) {
    x_.row += x_.v_row * dt;
    x_.col += x_.v_col * dt;
}

double AlphaBetaFilter::error_magnitude(double row, double col) const {
    return std::hypot(row - x_.row, col - x_.col);
}

void AlphaBetaFilter::update(double dt, double row, double col) {
    // A new track knows where it is and nothing about how fast. The expanding
    // gains open by reading the velocity off the second detection and decay
    // from there; once they have fallen to the caller's pair the schedule has
    // nothing left to offer and the steady gains take over for good.

    const double r_res = row - x_.row;
    const double c_res = col - x_.col;
    x_.row += p_.alpha * r_res;
    x_.col += p_.alpha * c_res;
    if (dt > 0.0) {
        x_.v_row += p_.beta * r_res / dt;
        x_.v_col += p_.beta * c_res / dt;
    }
}

} // namespace opir
