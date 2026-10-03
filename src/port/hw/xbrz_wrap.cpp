/* C interface to the vendored xBRZ scaler (third_party/xbrz, GPLv3). */
#include "xbrz.h"

#include <cstdint>

extern "C" void xbrz_upscale_argb(int factor, const uint32_t *src, uint32_t *dst, int width,
                                  int height) {
    xbrz::scale(static_cast<size_t>(factor), src, dst, width, height, xbrz::ColorFormat::ARGB);
}
