
namespace opir {

struct CfarParams {
    int guard = 3;  // guard band half-width, ~2-3x psf_sigma
    int ref = 6;    // outer half-width; ref cells lie between guard and ref
    double k = 5.0; // threshold in sigmas
    int min_cluster = 2;
    int max_cluster = 25;
};
} // namespace opir
