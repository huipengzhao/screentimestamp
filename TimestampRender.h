#ifndef TIMESTAMPRENDER_H
#define TIMESTAMPRENDER_H

#ifndef PLATFORM_SDK_VERSION
  /*
    Default version is Android-KK.
    Add the following line in Android.mk to customize the version.
      LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
  */
  #warning PLATFORM_SDK_VERSION is not defined, use 19 as default.
  #define PLATFORM_SDK_VERSION 19
#endif

#if PLATFORM_SDK_VERSION <= 19
  #define FOR_ANDROID_KK
#endif

#include <stdint.h>
#include <sys/types.h>
#include <EGL/egl.h>
#include <GLES/egl.h>
#include <GLES/gl.h>

class SkPaint;
class SkBitmap;
class SkCanvas;

namespace android {

// ---------------------------------------------------------------------------

class TimestampRender {
public:
    TimestampRender();
    virtual ~TimestampRender();

    void init(int w=0, int h=0); // size of the inner bitmap
    void setColor(uint32_t fgColor, uint32_t bgColor);
    void drawText(const char *fmt, ...);
    void renderToGL(int x, int y); // coordinate to the surface

private:
    SkPaint  *mPaint; // paint for text
    SkBitmap *mBitmap;
    SkCanvas *mCanvas;
    GLuint    mTexture;
    uint32_t  mFgColor;
    uint32_t  mBgColor;
};

// ---------------------------------------------------------------------------
}; // namespace android

#endif //TIMESTAMPRENDER_H
