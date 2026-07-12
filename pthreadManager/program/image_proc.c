#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

typedef struct {
    int w, h, c;
    unsigned char *p; /* packed RGB */
} image_t;

/* === timing === */
static double wallclock(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static void step_timing(const char *label, double t0) {
    double dt = wallclock() - t0;
    printf("  %-50s %7.3f s\n", label, dt);
}

/* === image I/O via stb_image === */
static image_t *img_load(const char *path) {
    int w, h, n;
    unsigned char *data = stbi_load(path, &w, &h, &n, 3);
    if (!data) {
        fprintf(stderr, "Error: cannot load '%s': %s\n", path, stbi_failure_reason());
        return NULL;
    }
    image_t *im = malloc(sizeof(*im));
    im->w = w; im->h = h; im->c = 3;
    im->p = data;
    return im;
}

static int img_save(const char *path, const image_t *im, int quality) {
    const char *ext = strrchr(path, '.');
    if (!ext) ext = "";
    ext++; /* skip '.' */

    /* lower-case compare */
    char ext_low[16];
    int i;
    for (i = 0; i < 15 && ext[i]; i++)
        ext_low[i] = (char)tolower((unsigned char)ext[i]);
    ext_low[i] = '\0';

    if (strcmp(ext_low, "jpg") == 0 || strcmp(ext_low, "jpeg") == 0)
        return stbi_write_jpg(path, im->w, im->h, im->c, im->p, quality);
    else if (strcmp(ext_low, "bmp") == 0)
        return stbi_write_bmp(path, im->w, im->h, im->c, im->p);
    else if (strcmp(ext_low, "tga") == 0)
        return stbi_write_tga(path, im->w, im->h, im->c, im->p);
    else /* default: PNG */
        return stbi_write_png(path, im->w, im->h, im->c, im->p, im->w * im->c);
}

static void img_free(image_t *im) {
    if (im) { stbi_image_free(im->p); free(im); }
}

static image_t *img_alloc(int w, int h) {
    image_t *im = malloc(sizeof(*im));
    im->w = w; im->h = h; im->c = 3;
    im->p = calloc((size_t)w * h * 3, 1);
    return im;
}

/* === Filters === */

/* Separated 2-pass Gaussian blur */
static void gaussian_blur(image_t *dst, const image_t *src, int ksize) {
    double sigma = ksize / 6.0;
    int half = ksize / 2;

    double *K = malloc(sizeof(double) * ksize);
    double sum = 0.0;
    for (int i = -half; i <= half; i++) {
        double v = exp(-(i * i) / (2.0 * sigma * sigma));
        K[i + half] = v;
        sum += v;
    }
    for (int i = 0; i < ksize; i++) K[i] /= sum;

    size_t row_bytes = (size_t)src->w * 3;
    unsigned char *tmp = malloc(row_bytes * src->h);

    /* horizontal pass */
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            double r = 0, g = 0, b = 0;
            for (int kx = -half; kx <= half; kx++) {
                int px = x + kx;
                if (px < 0) px = 0;
                if (px >= src->w) px = src->w - 1;
                double w = K[kx + half];
                int idx = (y * src->w + px) * 3;
                r += src->p[idx] * w;
                g += src->p[idx + 1] * w;
                b += src->p[idx + 2] * w;
            }
            int idx = (y * src->w + x) * 3;
            tmp[idx]     = (unsigned char)fmin(255, fmax(0, r));
            tmp[idx + 1] = (unsigned char)fmin(255, fmax(0, g));
            tmp[idx + 2] = (unsigned char)fmin(255, fmax(0, b));
        }
    }

    /* vertical pass */
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            double r = 0, g = 0, b = 0;
            for (int ky = -half; ky <= half; ky++) {
                int py = y + ky;
                if (py < 0) py = 0;
                if (py >= src->h) py = src->h - 1;
                double w = K[ky + half];
                int idx = (py * src->w + x) * 3;
                r += tmp[idx] * w;
                g += tmp[idx + 1] * w;
                b += tmp[idx + 2] * w;
            }
            int idx = (y * src->w + x) * 3;
            dst->p[idx]     = (unsigned char)fmin(255, fmax(0, r));
            dst->p[idx + 1] = (unsigned char)fmin(255, fmax(0, g));
            dst->p[idx + 2] = (unsigned char)fmin(255, fmax(0, b));
        }
    }

    free(K);
    free(tmp);
}

/* Sobel edge detection */
static void sobel(image_t *dst, const image_t *src) {
    const int sobel_x[9] = {-1,0,1, -2,0,2, -1,0,1};
    const int sobel_y[9] = {-1,-2,-1, 0,0,0, 1,2,1};

    unsigned char *gray = malloc((size_t)src->w * src->h);
    for (int i = 0; i < src->w * src->h; i++) {
        int idx = i * 3;
        gray[i] = (unsigned char)(0.299 * src->p[idx] +
                                  0.587 * src->p[idx + 1] +
                                  0.114 * src->p[idx + 2]);
    }

    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            double gx = 0, gy = 0;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int px = x + kx, py = y + ky;
                    if (px < 0) px = 0;
                    if (px >= src->w) px = src->w - 1;
                    if (py < 0) py = 0;
                    if (py >= src->h) py = src->h - 1;
                    int ki = (ky + 1) * 3 + (kx + 1);
                    gx += gray[py * src->w + px] * sobel_x[ki];
                    gy += gray[py * src->w + px] * sobel_y[ki];
                }
            }
            double mag = fmin(255.0, sqrt(gx * gx + gy * gy));
            unsigned char v = (unsigned char)mag;
            dst->p[(y * src->w + x) * 3]     = v;
            dst->p[(y * src->w + x) * 3 + 1] = v;
            dst->p[(y * src->w + x) * 3 + 2] = v;
        }
    }
    free(gray);
}

/* Joint/cross bilateral filter */
static void bilateral(image_t *dst,
                      const image_t *target,
                      const image_t *guide,
                      int ksize,
                      double sigma_s,
                      double sigma_r) {
    int half = ksize / 2;

    double *spatial = malloc(sizeof(double) * ksize * ksize);
    for (int y = -half; y <= half; y++)
        for (int x = -half; x <= half; x++)
            spatial[(y + half) * ksize + (x + half)] =
                exp(-(double)(x * x + y * y) / (2.0 * sigma_s * sigma_s));

    for (int y = 0; y < target->h; y++) {
        for (int x = 0; x < target->w; x++) {
            int ci = (y * target->w + x) * 3;
            unsigned char cr = guide->p[ci];
            unsigned char cg = guide->p[ci + 1];
            unsigned char cb = guide->p[ci + 2];

            double sum_r = 0, sum_g = 0, sum_b = 0, w_sum = 0;

            for (int ky = -half; ky <= half; ky++) {
                for (int kx = -half; kx <= half; kx++) {
                    int px = x + kx, py = y + ky;
                    if (px < 0) px = 0;
                    if (px >= target->w) px = target->w - 1;
                    if (py < 0) py = 0;
                    if (py >= target->h) py = target->h - 1;

                    int ni = (py * target->w + px) * 3;
                    int gi = (py * guide->w + px) * 3;

                    double dr = cr - guide->p[gi];
                    double dg = cg - guide->p[gi + 1];
                    double db = cb - guide->p[gi + 2];
                    double d = sqrt(dr * dr + dg * dg + db * db);

                    double w = spatial[(ky + half) * ksize + (kx + half)]
                             * exp(-(d * d) / (2.0 * sigma_r * sigma_r));

                    sum_r += target->p[ni]     * w;
                    sum_g += target->p[ni + 1] * w;
                    sum_b += target->p[ni + 2] * w;
                    w_sum += w;
                }
            }

            dst->p[ci]     = (unsigned char)(sum_r / w_sum);
            dst->p[ci + 1] = (unsigned char)(sum_g / w_sum);
            dst->p[ci + 2] = (unsigned char)(sum_b / w_sum);
        }
    }
    free(spatial);
}

/* === main === */
// 1. 仅需修改原项目主函数名称与参数列表命名
// 2. 将原项目编译成静态库
int main_task(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_path> <output_path>\n", argv[0]);
        fprintf(stderr, "  e.g.  %s photo.jpg result.png\n", argv[0]);
        fprintf(stderr, "  Supports: PNG, JPG, JPEG, BMP, TGA, PPM, GIF, etc.\n");
        return 1;
    }

    const char *inpath  = argv[1];
    const char *outpath = argv[2];

    double t_total = wallclock();

    printf("=== Image Processing Pipeline ===\n\n");
    printf("Input : %s\n", inpath);
    printf("Output: %s\n\n", outpath);

    /* ------ read ------ */
    printf("[1/4] Reading image ...\n");
    double t = wallclock();
    image_t *img = img_load(inpath);
    if (!img) return 1;
    step_timing("load (stb_image)", t);
    printf("  %d x %d px, %d channel(s), %.1f MB\n\n",
           img->w, img->h, img->c,
           (double)img->w * img->h * img->c / (1024.0 * 1024.0));

    image_t *buf1 = img_alloc(img->w, img->h);
    image_t *buf2 = img_alloc(img->w, img->h);
    if (!buf1 || !buf2) {
        fprintf(stderr, "Out of memory\n");
        img_free(img); img_free(buf1); img_free(buf2);
        return 1;
    }

    /* ------ Step 1: Gaussian blur (15x15) ------ */
    printf("[2/4] Gaussian blur (kernel 15x15, sigma=2.5) ...\n");
    t = wallclock();
    gaussian_blur(buf1, img, 15);
    step_timing("gaussian blur 15x15", t);

    /* ------ Step 2: Sobel edge detection ------ */
    printf("[3/4] Sobel edge detection ...\n");
    t = wallclock();
    sobel(buf2, buf1);
    step_timing("sobel edge", t);

    /* ------ Step 3: Bilateral filter (7x7, 2 passes) ------ */
    printf("[4/4] Bilateral filter (kernel 7x7, sigma_s=12, sigma_r=30, 2 passes) ...\n");
    t = wallclock();

    /* pass 1: smooth edge image guided by itself */
    bilateral(buf1, buf2, buf2, 7, 12.0, 30.0);

    /* swap: pass 2 */
    {
        image_t *tmp = buf1; buf1 = buf2; buf2 = tmp;
    }
    bilateral(buf1, buf2, buf2, 7, 12.0, 30.0);

    step_timing("bilateral 7x7 x2", t);
    /* result in buf1 */

    /* ------ write result ------ */
    printf("\nWriting %s ...\n", outpath);
    t = wallclock();
    if (!img_save(outpath, buf1, 95)) {
        fprintf(stderr, "Error: failed to write '%s'\n", outpath);
        img_free(img); img_free(buf1); img_free(buf2);
        return 1;
    }
    step_timing("save (stb_image_write)", t);

    printf("\n=== Done! Total time: %.3f s ===\n", wallclock() - t_total);

    img_free(img);
    img_free(buf1);
    img_free(buf2);
    return 0;
}
