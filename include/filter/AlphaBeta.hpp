#pragma once

namespace opir {

/// Constant-velocity estimate of one target, in pixels and pixels per second.
struct StateEstimate {
    double row, col;
    double v_row, v_col;
};

/// The gains the filter settles at. They are not independent: stability needs
/// 0 < beta < 4 - 2 * alpha, and beta = alpha^2 / (2 - alpha) is the
/// Benedict-Bordner pair that balances lag against noise.
struct AlphaBetaParams {
    double alpha = 0.6; // fraction of each residual applied to position
    double beta = 0.2;  // fraction applied to velocity, per second
};

/// Fixed-gain constant-velocity filter: predict along the current velocity,
/// then pull toward each detection by a fraction of the residual.
///
/// The first few updates use expanding-memory gains instead, which start at a
/// two-point velocity initialisation and decay to the pair above. Without them
/// a new track needs dozens of frames to learn a velocity it can read off its
/// second detection.
class AlphaBetaFilter {
    StateEstimate x_;
    AlphaBetaParams p_;
    int updates_ = 1; // the constructor's position is the first measurement

  public:
    /// \throws Error if the gains fall outside the stable region
    AlphaBetaFilter(double row, double col, AlphaBetaParams p = {});

    void predict(double dt);

    /// Pixels between a detection and the current prediction.
    double error_magnitude(double row, double col) const;

    /// \param dt seconds since the predict() this corrects; must be positive
    ///           for the velocity term to be applied
    void update(double dt, double row, double col);

    const StateEstimate &state() const { return x_; }
};

} // namespace opir
