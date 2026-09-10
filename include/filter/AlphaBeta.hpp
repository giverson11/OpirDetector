#pragma once

namespace opir {

/// Constant-velocity estimate of one target, in pixels and pixels per second.
struct StateEstimate {
    double row, col;
    double v_row, v_col;
};

struct AlphaBetaParams {
    double alpha = 0.6; // fraction of each residual applied to position
    double beta = 0.2;  // fraction applied to velocity, per second
};

/// Fixed-gain constant-velocity filter: predict along the current velocity,
/// then pull toward each detection by a fraction of the residual.
///
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
