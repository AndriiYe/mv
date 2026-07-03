# LocalTracker Design

`LocalTracker` owns the local optical-flow state machine for one grayscale frame
buffer. Capture, BGR-to-gray conversion, drawing, and `imshow` stay outside the
class.

```mermaid
flowchart TD
    A["Caller owns frame source<br/>camera, video, stream"] --> B["Caller converts BGR frame<br/>to grayscale cv::Mat"]
    B --> C["LocalTracker constructed with<br/>const cv::Mat& frame_buffer"]
    D["LocalTrackerConfig<br/>max_points<br/>tracking_region_size<br/>max_delta<br/>ROI_center_x / ROI_center_y<br/>qualityLevel / minDistance<br/>winSize / maxLevel / criteria<br/>flags / minEigThreshold"] --> C

    C --> E{"update() called"}
    E --> F{"Initialized?"}
    F -- "No" --> G["Initialize ROI center<br/>config ROI if provided<br/>else frame center"]
    G --> H["Create tracking_region<br/>around ROI center"]
    H --> I["goodFeaturesToTrack<br/>inside ROI"]
    I --> J["Store previous_gray<br/>Store previous_points<br/>recap_counter++"]
    J --> Z["Return dx=0, dy=0"]

    F -- "Yes" --> K{"Have previous_points?"}
    K -- "No" --> L["Re-detect points<br/>in current ROI"]
    L --> M["Copy current frame<br/>to previous_gray"]
    M --> Z

    K -- "Yes" --> N["calcOpticalFlowPyrLK<br/>previous_gray -> current frame"]
    N --> O["Keep LK points<br/>with status=true"]
    O --> P["median_delta<br/>median dx, median dy"]
    P --> Q{"abs(dx) or abs(dy)<br/>greater than max_delta?"}
    Q -- "Yes" --> R["Fast-jump protection<br/>Do not move ROI<br/>Re-detect points<br/>recap_counter++"]
    R --> Y["Copy current frame<br/>to previous_gray"]

    Q -- "No" --> S["Move ROI center<br/>center += median dx/dy"]
    S --> T["Build moved tracking_region"]
    T --> U{"ROI touches<br/>frame border?"}
    U -- "Yes" --> V["Reset ROI center<br/>to origin center<br/>Re-detect points<br/>recap_counter++"]
    V --> Y

    U -- "No" --> W["Filter tracked points<br/>must remain inside moved ROI"]
    W --> X["valid_next_points becomes<br/>next previous_points"]
    X --> Y
    Y --> AA["Return accepted dx, dy"]

    AA --> AB["Caller reads outputs"]
    AB --> AC["tracking_region()"]
    AB --> AD["recap_counter()"]
    AB --> AE["valid_next_points()"]
```

## Input Data

- `frame_buffer`: grayscale `cv::Mat` reference updated by the caller before
  every `update()` call.
- `LocalTrackerConfig`: feature detection, LK optical-flow, ROI, and guard
  parameters.

## Processing Stages

1. Initialize ROI origin from config center, or frame center by default.
2. Detect feature points in the local ROI.
3. Track points with pyramidal Lucas-Kanade optical flow.
4. Estimate motion using median `dx/dy` from valid point pairs.
5. Reject sudden jumps larger than `max_delta`.
6. Reset ROI to its origin center if the ROI touches a frame border.
7. Keep only points that remain inside the active ROI.

## Output Data

- `update()`: accepted median `dx/dy` for the frame.
- `tracking_region()`: current ROI rectangle.
- `recap_counter()`: number of feature re-detection events.
- `valid_next_points()`: current accepted points in frame coordinates.
