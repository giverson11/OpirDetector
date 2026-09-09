#include "filter/AlphaBeta.hpp"
#include "core/Error.hpp"

#include <cmath>
#include <format>

namespace opir {

AlphaBetaFilter::AlphaBetaFilter(double row, double col, AlphaBetaParams p)
    : x_{row, col, 0.0, 0.0}, p_{p} {
    if (p.alpha <= 0.0 || p.beta <= 0.0 || p.beta >= 4.0 - 2.0 * p.alpha) {
        throw Error(
            std::format("gains alpha {} and beta {} are outside the "
                        "stable region 0 < alpha, 0 < beta < 4 - 2 alpha",
                        p.alpha, p.beta));
    }
}

void AlphaBetaFilter::predict(double dt) {
    x_.row += x_.v_row * dt;
    x_.col += x_.v_col * dt;
}

double AlphaBetaFilter::distance(double row, double col) const {
    return std::hypot(row - x_.row, col - x_.col);
}

void AlphaBetaFilter::update(double dt, double row, double col) {
    // At the second measurement these are 1 and 1, which is exactly a two-point
    // initialisation: take the position, and the velocity from the difference.
    // They decay from there, so the steady-state pair takes over on its own.
    const double n = static_cast<double>(++updates_);
    double alpha = 2.0 * (2.0 * n - 1.0) / (n * (n + 1.0));
    double beta = 6.0 / (n * (n + 1.0));
    if (alpha <= p_.alpha) {
        alpha = p_.alpha;
        beta = p_.beta;
    }

    const double r_res = row - x_.row;
    const double c_res = col - x_.col;
    x_.row += alpha * r_res;
    x_.col += alpha * c_res;
    if (dt > 0.0) {
        x_.v_row += beta * r_res / dt;
        x_.v_col += beta * c_res / dt;
    }
}

} // namespace opir
