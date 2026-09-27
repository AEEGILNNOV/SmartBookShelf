#ifndef PROJECT_CAMERA_CONFIG_H
#define PROJECT_CAMERA_CONFIG_H

#include "board_pins.h"

/* frame size used for the still capture that gets uploaded / archived */
#ifndef CAM_CAPTURE_FRAME_SIZE
#define CAM_CAPTURE_FRAME_SIZE FRAMESIZE_SVGA
#endif
#ifndef CAM_CAPTURE_QUALITY
#define CAM_CAPTURE_QUALITY 12
#endif

/* low resolution used while the camera just watches for a big change */
#ifndef CAM_WATCH_FRAME_SIZE
#define CAM_WATCH_FRAME_SIZE FRAMESIZE_QVGA
#endif
#ifndef CAM_WATCH_QUALITY
#define CAM_WATCH_QUALITY 24
#endif

#ifndef CAM_XCLK_FREQUENCY
#define CAM_XCLK_FREQUENCY 20000000
#endif
#ifndef CAM_FB_COUNT
#define CAM_FB_COUNT 2
#endif

/* change detection */
#ifndef CAM_DIFF_SAMPLE_W
#define CAM_DIFF_SAMPLE_W 32
#endif
#ifndef CAM_DIFF_SAMPLE_H
#define CAM_DIFF_SAMPLE_H 24
#endif
#ifndef CAM_DIFF_PIXEL_THRESHOLD
#define CAM_DIFF_PIXEL_THRESHOLD 25
#endif
#ifndef CAM_DIFF_CHANGED_RATIO_PCT
#define CAM_DIFF_CHANGED_RATIO_PCT 12
#endif
#ifndef CAM_CAPTURE_BURST_COUNT
#define CAM_CAPTURE_BURST_COUNT 3
#endif
#ifndef CAM_CAPTURE_BURST_INTERVAL_MS
#define CAM_CAPTURE_BURST_INTERVAL_MS 1000
#endif
#ifndef CAM_ACTIVE_WINDOW_MS
#define CAM_ACTIVE_WINDOW_MS 5000
#endif

#endif
