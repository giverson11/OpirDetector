:# OPIR Detection

A C++23 practice pipeline: simulate an overhead persistent infrared scene,
detect point targets in it, track them across frames, and score the result
against the truth the simulator recorded.

## Pipeline

Two executables. `scene_gen` writes the data, `scitec_opir_detection` consumes
it.

| Stage | In | Out |
| --- | --- | --- |
| `SceneSimulator` | targets, noise params | 16-bit frame |
| `FrameWriter` / `FrameReader` | frame | `Scene.bin` |
| `TruthWriter` / `TruthTable` | truth records | `Truth.csv` |
| `cfar_threshold` | frame | `mask`, `bg`, `sg` planes |
| `label_clusters` | `mask` | `labels`, count |
| `centroid_clusters` | frame, `labels`, `bg`, `sg` | `Detection` list |
| `Tracker::step` | detections | `TrackRecord` list |
| `Scorer::add` | truth, tracks | per-frame and run statistics |

Libraries mirror those stages: `core` (types, IO helpers), `sim`, `frame`,
`detect`, `filter`, `eval`.

## The calculations

### Simulator

IRFrames are simulated by defining a the background constant (dc_level), a row gradient, fixed pattern noise (gaussian) that is constant across all frames and read_noise (gaussian) that is individually generated per frame. Then targets are defined with starting row/col and constant speed, each target is display as a "gaussian blob" with a set amplitube that determines the magnitude of the brightness.
```
brightness = dc_level
           + row_gradient * r      ramp down the rows
           + fixed_pattern[r, c]   drawn once, identical every frame
           + read_noise(rng)       redrawn per pixel per frame
           
per target:
    (dr, dc) = (row, col) - (target.row, target.col)

    brightness += amplitude * exp(-(dr^2 + dc^2) / (2*sigma^2))

out[r, c] = clamp(round(brightness), 0, 65535)
```

The two noise sources differ only in what they hold constant. Fixed-pattern
noise is one sample per pixel drawn at construction; read noise is redrawn
every frame.


The target term is a [2D isotropic Gaussian](https://en.wikipedia.org/wiki/Gaussian_function): two 1D normal
distributions on independent axes sharing one sigma, multiplied together.

```
f(x,y) = f(x) * f(y) = 1/(2*pi*sigma^2) * exp(-(dr^2 + dc^2) / (2*sigma^2))
```

`amplitude` replaces the normalising
`1/(2*pi*sigma^2)`. Peak-normalising instead puts exactly `amplitude` on
the centre pixel, and the blob carries `amplitude * 2*pi*sigma^2` in total.

### CFAR threshold

[Constant False Alarm Rate](https://www.mathworks.com/help/phased/ug/constant-false-alarm-rate-cfar-detection.html) is used to determine if a pixel can be part of a detection. Each pixel is compared against noise estimated from a square annulus around
it. The guard band is excluded so a target's own skirt cannot inflate the
background it is supposed to stand out from.

```
window     = (2*ref + 1)^2
guard_area = (2*guard + 1)^2
cells      = window - guard_area

mean  = sum(v) / cells
var   = sum(v^2) / cells - mean^2       floored at 1e-9
sigma = sqrt(var)

flagged if px > mean + k * sigma
```

At the defaults (`guard = 3`, `ref = 6`) the ring is `169 - 49 = 120` cells.
Cost is `O(N * ref^2)`, which dominates the run. This will be optimized at a later point after implementing summ

Border pixels within `ref` of an edge are skipped, since their ring would fall
outside the frame.  Flagged pixels are stored in a row * col sized mask.

### Clustering and centroid

Flagged pixels are labeled into clusters using a BFS.
Each cluster has its rows and cols averaged into a centroid with peak brightness a signal to noise ratio recorded.
```
w      = max(px - bg, 0)
row    = sum(w * r) / sum(w)
col    = sum(w * c) / sum(w)

amplitude = max(w)                       the peak, background subtracted
snr       = amplitude / sg[peak pixel]   0 if that sigma is 0
```

Clusters outside `[min_cluster, max_cluster]` pixels are discarded: smaller is
a hot pixel, larger is not a point target (like the sun).

### Alpha-beta filter

Tracking is done by having a Tracker follow each cluster using a [Alpha-beta filter](https://en.wikipedia.org/wiki/Alpha_beta_filter#cite_note-Kalata-4) predict and updatethe trajectory of each eligible cluster.
```
predict(dt):  row += v_row * dt

update(dt):   residual = measured_row - row
              row     += alpha * residual
              v_row   += beta  * residual / dt
```

`alpha` and `beta` are independent constants the caller sets. Future iterations will include the addition of Kalman Filters, but this early implementation is good to make sure I have general pipeline setup.



### Association

Eligible clusters are assigned to Trackers using Greedy nearest neighbour on each trackers versus each eligible cluster (hypo of the clusters row/col versus the trackers prediction). 
Trackers are only determined to be associated if they correctly track a cluster with a thresholded magnitude for
 `confirm_hits` detections and dropped after
`max_misses` consecutive frames without one. A confirmed track that goes unseen
is still reported, on its prediction.

### Scoring

Truth (the true statistics of each tracker not just the detected location) is paired against tracks with the same greedy claim, so each truth record
and each track is accounted for exactly once. Matching every truth record to
its own nearest track independently would let two of them claim one track and
report no miss.

Error is signed as track minus truth, so a consistent lag reads as a consistent
sign. Radial error is a magnitude, so its mean sits above zero even for an
unbiased tracker; the signed row and column means are where lag shows.

Statistics include mean and variance and are
keyed by target rather than by track, since the target is what persists.

## Decisions

- **Planes are `std::mdspan`**, via `template <class T> using Plane`. Callers
  own the storage and reuse it across frames, so no plane is reallocated per
  frame. The detection and track lists are still returned by value.

- **`std::expected<T, ParseError>` for stream outcomes, `Error` thrown for
  caller mistakes.** Reaching the end of a file is control flow; a path that
  will not open is a bug. `read_exact` distinguishes a clean end from a
  truncation, which `istream` alone cannot.
- **The frame format.** A 32-byte header holds magic,
  version, dimensions, id and a microsecond timestamp, so a reader needs only a
  path. Fixed-width fields, no implicit padding, both asserted at compile time.

- **Truth is read whole and indexed by frame.** A CSV of records is far smaller
  than the imagery, and one slot per frame gives O(1) lookup during scoring.

## Build and run

Requires GCC 16+ or Clang 20+ for the C++23 library features.

```sh
cmake -S . -B build
cmake --build build -j
./build/scene_gen                 # writes Scene.bin and Truth.csv
./build/scitec_opir_detection     # detects, tracks and scores
ctest --test-dir build
```
