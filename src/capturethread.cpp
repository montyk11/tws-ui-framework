// capturethread.cpp
#include "capturethread.h"
#include <QDebug>
#include <QImage>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <time.h>
#include <QMetaObject>
#include <jpeglib.h>

static const int WIDTH = 640;
static const int HEIGHT = 480;
static const char *VIDEO_DEVICE = "/dev/video0";
static const int NUM_PALETTES = 6;

const char *palette_names[] = {
    "IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT"
};

CaptureThread::CaptureThread(std::atomic<int> &palette_atomic, QObject *parent)
    : QThread(parent),
      m_running(false),
      m_palette_atomic(palette_atomic),
      v4l2_fd(-1),
      video_buffer(MAP_FAILED),
      buf_length(0),
      gb(nullptr)
{
}

CaptureThread::~CaptureThread()
{
    stopAndWait();
}

const char* CaptureThread::paletteName(int idx)
{
    idx = idx % NUM_PALETTES;
    return palette_names[idx];
}

/* Palette conversion logic (same as before) */
static void gray_to_color_palette(uint16_t gray, uint8_t &r, uint8_t &g, uint8_t &b, int mode) {
    uint8_t v = gray >> 8;
    switch (mode) {
        case 0: r = (v < 128) ? v * 2 : 255; g = (v < 128) ? 0 : (v - 128) * 2; b = 0; break;
        case 1:
            if (v < 64) { r=0; g=0; b=4*v; }
            else if (v < 128) { r=0; g=4*(v-64); b=255-4*(v-64); }
            else if (v < 192) { r=4*(v-128); g=255-4*(v-128); b=0; }
            else { r=255; g=4*(v-192); b=4*(v-192); }
            break;
        case 2: r = v / 2; g = v; b = 255; break;
        case 3: r = v; g = v / 4; b = 0; break;
        case 4: r = g = b = 255 - v; break;
        case 5: r = g = b = v; break;
        default: r = g = b = v; break;
    }
}

/*
 * C callback used by gpio_buttons. The 'user' pointer is expected to be a
 * CaptureThread* (passed when registering). This function forwards the
 * event into the CaptureThread via QMetaObject::invokeMethod so the
 * event is delivered in Qt's event/queue system.
 */
static void gpio_button_cb(unsigned int offset, int value, void *user)
{
    if (!user) return;
    CaptureThread *ct = reinterpret_cast<CaptureThread*>(user);

    QMetaObject::invokeMethod(ct, "handleGpioEvent", Qt::QueuedConnection,
                              Q_ARG(unsigned int, offset),
                              Q_ARG(int, value));
}

void CaptureThread::handleGpioEvent(unsigned int offset, int value)
{
    emit gpioPressed(offset, value);
}

/* -------------------- MJPEG decompress helper (in-place) -------------------- */
/* decompress_mjpeg_to_rgb_inplace:
 *   in_buf/in_size -> input JPEG bytes
 *   out_buf/out_buf_sz -> preallocated buffer to write RGB24 into
 *   out_w/out_h/out_components -> returns image size and components (components usually 3)
 *
 * Writes RGB24 scanlines directly into out_buf as row-major RGBRGB...
 * Returns 0 on success, -1 on failure.
 */
static int decompress_mjpeg_to_rgb_inplace(const uint8_t *in_buf, size_t in_size,
                                           uint8_t *out_buf, size_t out_buf_sz,
                                           int *out_w, int *out_h, int *out_components)
{
    if (!in_buf || !out_buf) return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW rowptr[1];

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, in_buf, in_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_start_decompress(&cinfo);

    *out_w = cinfo.output_width;
    *out_h = cinfo.output_height;
    *out_components = cinfo.output_components; // expected 3 (RGB)

    size_t row_stride = (size_t)(*out_w) * (size_t)(*out_components);
    size_t needed = row_stride * (size_t)(*out_h);
    if (needed > out_buf_sz) {
        // provided buffer too small
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        size_t y = cinfo.output_scanline;
        rowptr[0] = out_buf + y * row_stride;
        int ret = jpeg_read_scanlines(&cinfo, rowptr, 1);
        if (ret != 1) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return -1;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return 0;
}
/* --------------------------------------------------------------------------- */

void CaptureThread::run()
{
    m_running = true;

    // Open V4L2 device
    v4l2_fd = open(VIDEO_DEVICE, O_RDWR);
    if (v4l2_fd < 0) {
        qWarning() << "Failed to open video device" << VIDEO_DEVICE << strerror(errno);
        return;
    }

    // Set V4L2 format to MJPEG (640x480)
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;   // MJPEG!
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed:" << strerror(errno);
        cleanup(); return;
    }

    // Request multiple buffers for smoother streaming
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4; // use 4 buffers (helps smoothness)
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        qWarning() << "VIDIOC_REQBUFS failed:" << strerror(errno);
        cleanup(); return;
    }

    // Query and mmap each buffer
    struct v4l2_buffer *bufs = (struct v4l2_buffer*)calloc(req.count, sizeof(struct v4l2_buffer));
    void **mapped_ptrs = (void**)calloc(req.count, sizeof(void*));
    if (!bufs || !mapped_ptrs) {
        qWarning() << "Out of memory for buffer structures";
        free(bufs);
        free(mapped_ptrs);
        cleanup(); return;
    }

    for (uint32_t i = 0; i < req.count; ++i) {
        memset(&bufs[i], 0, sizeof(struct v4l2_buffer));
        bufs[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bufs[i].memory = V4L2_MEMORY_MMAP;
        bufs[i].index = i;
        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &bufs[i]) < 0) {
            qWarning() << "VIDIOC_QUERYBUF failed for index" << i << ":" << strerror(errno);
            for (uint32_t j = 0; j < i; ++j) if (mapped_ptrs[j]) munmap(mapped_ptrs[j], bufs[j].length);
            free(bufs);
            free(mapped_ptrs);
            cleanup(); return;
        }
        mapped_ptrs[i] = mmap(NULL, bufs[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, bufs[i].m.offset);
        if (mapped_ptrs[i] == MAP_FAILED) {
            qWarning() << "mmap failed for index" << i << ":" << strerror(errno);
            for (uint32_t j = 0; j < i; ++j) if (mapped_ptrs[j]) munmap(mapped_ptrs[j], bufs[j].length);
            free(bufs);
            free(mapped_ptrs);
            cleanup(); return;
        }
        // queue buffer
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &bufs[i]) < 0) {
            qWarning() << "VIDIOC_QBUF failed for index" << i << ":" << strerror(errno);
            for (uint32_t j = 0; j <= i; ++j) if (mapped_ptrs[j]) munmap(mapped_ptrs[j], bufs[j].length);
            free(bufs);
            free(mapped_ptrs);
            cleanup(); return;
        }
    }

    // Save the first mapped pointer for backward compatibility with your code
    video_buffer = mapped_ptrs[0];
    buf_length = bufs[0].length;

    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed:" << strerror(errno);
        for (uint32_t i = 0; i < req.count; ++i) if (mapped_ptrs[i]) munmap(mapped_ptrs[i], bufs[i].length);
        free(bufs);
        free(mapped_ptrs);
        cleanup(); return;
    }

    // Initialize gpio_buttons if available
    const unsigned int offsets[] = {0,1,4,6};
    const char *names[] = {"PF0","PF1","PF4","PF6"};
    gb = gpio_buttons_create("/dev/gpiochip5", offsets, names, 4, 200, 20000);
    if (!gb) {
        qWarning() << "Warning: gpio_buttons_create failed — continuing without gpio";
    } else {
        for (int i = 0; i < 4; ++i) {
            gpio_buttons_register_callback(gb, i, gpio_button_cb, this);
        }
        gpio_buttons_start(gb);
    }

    // -------------------------
    // Precompute palette LUTs
    // -------------------------
    uint32_t palette_lut[NUM_PALETTES][256];
    for (int p = 0; p < NUM_PALETTES; ++p) {
        for (int v = 0; v < 256; ++v) {
            uint8_t r, g, b;
            uint16_t gray16 = (uint16_t)(v << 8);
            gray_to_color_palette(gray16, r, g, b, p);
            palette_lut[p][v] = qRgba(r, g, b, 0xFF);
        }
    }

    // Preallocate a single decoded buffer (RGB24): WIDTH*HEIGHT*3 bytes
    size_t max_decoded_sz = (size_t)WIDTH * (size_t)HEIGHT * 3;
    uint8_t *reusable_decoded = (uint8_t*)malloc(max_decoded_sz);
    if (!reusable_decoded) {
        qWarning() << "Failed to allocate reusable decoded buffer of size" << (long)max_decoded_sz;
        // we'll continue but decoding will fail
    }

    // Main capture loop
    while (m_running) {
        struct v4l2_buffer dq;
        memset(&dq, 0, sizeof(dq));
        dq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dq.memory = V4L2_MEMORY_MMAP;

        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &dq) < 0) {
            qWarning() << "VIDIOC_DQBUF failed:" << strerror(errno);
            break;
        }

        // pointer to the dequeued buffer's memory
        uint8_t *buf_ptr = reinterpret_cast<uint8_t*>(mapped_ptrs[dq.index]);
        size_t jpeg_size = dq.bytesused;
        int dec_w = 0, dec_h = 0, dec_components = 0;
        bool decode_ok = false;

        // Quick JPEG magic check
        if (jpeg_size >= 3 && buf_ptr[0] == 0xFF && buf_ptr[1] == 0xD8 && buf_ptr[2] == 0xFF) {
            if (reusable_decoded) {
                if (decompress_mjpeg_to_rgb_inplace(buf_ptr, jpeg_size,
                                                    reusable_decoded, max_decoded_sz,
                                                    &dec_w, &dec_h, &dec_components) == 0) {
                    decode_ok = true;
                } else {
                    qWarning() << "Inplace MJPEG decode failed (maybe image larger than expected)";
                    decode_ok = false;
                }
            } else {
                qWarning() << "No reusable decoded buffer available";
                decode_ok = false;
            }
        } else {
            qWarning() << "Frame did not look like MJPEG (magic missing) size=" << (int)jpeg_size;
        }

        if (decode_ok && dec_w > 0 && dec_h > 0) {
            // Create QImage and fill using palette mapping from the decoded RGB
            QImage img(WIDTH, HEIGHT, QImage::Format_ARGB32);
            int mode_idx = m_palette_atomic.load();
            if (mode_idx < 0) mode_idx = 0;
            if (mode_idx >= NUM_PALETTES) mode_idx %= NUM_PALETTES;
            uint32_t *lut = palette_lut[mode_idx];

            int row_stride = dec_w * dec_components; // should be dec_w * 3
            for (int y = 0; y < HEIGHT; ++y) {
                uint32_t *dest = reinterpret_cast<uint32_t*>(img.scanLine(y));
                if (y < dec_h) {
                    uint8_t *src_row = reusable_decoded + y * row_stride;
                    for (int x = 0; x < WIDTH; ++x) {
                        if (x < dec_w) {
                            uint8_t r = src_row[x * dec_components + 0];
                            uint8_t g = src_row[x * dec_components + 1];
                            uint8_t b = src_row[x * dec_components + 2];
                            // integer fast luminance approx: (19595*r + 38470*g + 7471*b) >> 16
                            uint8_t lum = (uint8_t)(((19595u * r + 38470u * g + 7471u * b) >> 16) & 0xFFu);
                            dest[x] = lut[lum];
                        } else {
                            dest[x] = qRgba(0,0,0,0xFF);
                        }
                    }
                } else {
                    for (int x = 0; x < WIDTH; ++x) dest[x] = qRgba(0,0,0,0xFF);
                }
            }

            emit frameReady(img);
        } else {
            // skip frame (avoid malloc); optionally track stats
        }

        // Re-queue the buffer
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &dq) < 0) {
            qWarning() << "VIDIOC_QBUF failed:" << strerror(errno);
            break;
        }

        // small sleep to throttle (optional)
        struct timespec ts = {0, 16666 * 1000};
        nanosleep(&ts, NULL);
    }

    // Stop streaming
    int type_off = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type_off);

    // cleanup mapped buffers
    for (uint32_t i = 0; i < req.count; ++i) {
        if (mapped_ptrs[i] && bufs[i].length) munmap(mapped_ptrs[i], bufs[i].length);
    }
    free(bufs);
    free(mapped_ptrs);

    if (reusable_decoded) free(reusable_decoded);

    cleanup();
}

void CaptureThread::cleanup()
{
    if (gb) {
        gpio_buttons_stop(gb);
        gpio_buttons_destroy(gb);
        gb = nullptr;
    }

    if (video_buffer != MAP_FAILED && buf_length > 0) {
        munmap(video_buffer, buf_length);
        video_buffer = MAP_FAILED;
    }
    if (v4l2_fd >= 0) {
        close(v4l2_fd);
        v4l2_fd = -1;
    }
}

void CaptureThread::stopAndWait()
{
    m_running = false;
    requestInterruption();
    quit();
    wait();
    cleanup();
}
