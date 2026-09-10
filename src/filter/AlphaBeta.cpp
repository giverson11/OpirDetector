#include "filter/AlphaBeta.hpp"
#include "core/Error.hpp"

#include <cmath>
#include <format>

namespace opir {
namespace {

///
/// The gains a least-squares fit to n points of a constant-velocity track
/// would use.
///
/// At n = 2 they come out at 1 and 1: the second detection is taken whole and
/// the velocity read straight off the pair, which is the best a track with two
/// positions can do. They fall away from there.
///
AlphaBetaParams expanding_gains(int n) {
    const double dn = static_cast<double>(n);
    const double denominator = dn * (dn + 1.0);
    return {.alpha = 2.0 * (2.0 * dn - 1.0) / denominator,
            .beta = 6.0 / denominator};
}

} // namespace

AlphaBetaFilter::AlphaBetaFilter(double row, double col, AlphaBetaParams p)
    : x_{row, col, 0.0, 0.0}, p_{p} {
    // Written as negated comparisons so a NaN gain fails rather than slipping
    // through every test it is asked.
    if (!(p.alpha > 0.0) || !(p.beta > 0.0) ||
        !(p.beta < 4.0 - 2.0 * p.alpha)) {
        throw Error(std::format("alpha {} and beta {} are outside the stable "
                                "region 0 < beta < 4 - 2 * alpha",
                                p.alpha, p.beta));
    }
}

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
    const AlphaBetaParams opening = expanding_gains(updates_ + 1);
    const bool still_opening = opening.alpha > p_.alpha;
    const AlphaBetaParams gains = still_opening ? opening : p_;
    if (still_opening) {
        ++updates_;
    }

    const double r_res = row - x_.row;
    const double c_res = col - x_.col;
    x_.row += gains.alpha * r_res;
    x_.col += gains.alpha * c_res;
    if (dt > 0.0) {
        x_.v_row += gains.beta * r_res / dt;
        x_.v_col += gains.beta * c_res / dt;
    }
}

} // namespace opir
