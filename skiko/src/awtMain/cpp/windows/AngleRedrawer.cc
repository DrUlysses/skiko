#ifdef SK_ANGLE

#include <Windows.h>
#include <jawt_md.h>
#include "jni_helpers.h"
#include "exceptions_handler.h"
#include "ganesh/GrBackendSurface.h"
#include "ganesh/GrDirectContext.h"
#include "ganesh/gl/GrGLBackendSurface.h"
#include "ganesh/gl/GrGLDirectContext.h"
#include "SkSurface.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "ganesh/gl/GrGLAssembleInterface.h"
#include "ganesh/gl/GrGLDefines.h"
#include <GL/gl.h>
#include "window_util.h"

class AngleDevice
{
public:
    HWND window;
    HDC device;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig surfaceConfig;
    sk_sp<const GrGLInterface> backendContext;
    ~AngleDevice()
    {
        backendContext.reset(nullptr);
        if (EGL_NO_CONTEXT != context)
        {
            eglDestroyContext(display, context);
        }
        if (EGL_NO_SURFACE != surface)
        {
            eglDestroySurface(display, surface);
        }
        if (EGL_NO_DISPLAY != display)
        {
            eglTerminate(display);
        }
    }
};

EGLDisplay getAngleEGLDisplay(HDC hdc)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
    eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    // We expect ANGLE to support this extension
    if (!eglGetPlatformDisplayEXT)
    {
        return EGL_NO_DISPLAY;
    }
    static constexpr EGLint attribs[] = {
        // We currently only support D3D11 ANGLE.
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_NONE, EGL_NONE
    };
    return eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, hdc, attribs);
}

bool initAngleSurface(JNIEnv *env, AngleDevice *angleDevice, EGLint width, EGLint height)
{
    const EGLint surfaceAttribs[] = {
        EGL_FIXED_SIZE_ANGLE, EGL_TRUE,
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE, EGL_NONE
    };
    if (EGL_NO_SURFACE != angleDevice->surface)
    {
        eglDestroySurface(angleDevice->display, angleDevice->surface);
    }
    angleDevice->surface = eglCreateWindowSurface(angleDevice->display, angleDevice->surfaceConfig, angleDevice->window, surfaceAttribs);
    if (EGL_NO_SURFACE == angleDevice->surface)
    {
        return false;
    }
    if (!eglMakeCurrent(angleDevice->display, angleDevice->surface, angleDevice->surface, angleDevice->context))
    {
        throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not make context current!");
        return false;
    }
    eglSwapInterval(angleDevice->display, 1);
    return true;
}

extern "C"
{
    JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_createAngleDevice(
        JNIEnv *env, jobject redrawer, jlong platformInfoPtr, jboolean transparency)
    {
        __try
        {
            JAWT_Win32DrawingSurfaceInfo *dsi_win = fromJavaPointer<JAWT_Win32DrawingSurfaceInfo *>(platformInfoPtr);
            HWND hwnd = dsi_win->hwnd;
            HDC hdc = GetDC(hwnd);

            if (transparency)
            {
                enableTransparentWindow(hwnd);
            }

            AngleDevice *angleDevice = new AngleDevice();
            angleDevice->window = hwnd;
            angleDevice->device = hdc;
            angleDevice->display = getAngleEGLDisplay(angleDevice->device);
            if (EGL_NO_DISPLAY == angleDevice->display)
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not get display!");
                return (jlong) 0;
            }

            EGLint majorVersion;
            EGLint minorVersion;
            if (!eglInitialize(angleDevice->display, &majorVersion, &minorVersion))
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not initialize display!");
                return (jlong) 0;
            }

            static constexpr int fSampleCount = 1;
            static constexpr int sampleBuffers = fSampleCount > 1 ? 1 : 0;
            static constexpr int eglSampleCnt = fSampleCount > 1 ? fSampleCount : 0;
            static constexpr EGLint configAttribs[] = {
                // We currently only support ES3.
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_SAMPLE_BUFFERS, sampleBuffers,
                EGL_SAMPLES, eglSampleCnt,
                EGL_NONE, EGL_NONE
            };

            EGLint numConfigs;
            if (!eglChooseConfig(angleDevice->display, configAttribs, &angleDevice->surfaceConfig, 1, &numConfigs))
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not create choose config!");
                return (jlong) 0;
            }

            // We currently only support ES3.
            static constexpr EGLint contextAttribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_CONTEXT_MINOR_VERSION, 0,
                EGL_NONE, EGL_NONE
            };
            angleDevice->context = eglCreateContext(angleDevice->display, angleDevice->surfaceConfig, nullptr, contextAttribs);
            if (EGL_NO_CONTEXT == angleDevice->context)
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not create context!");
                return (jlong) 0;
            }

            // initial surface
            if (!initAngleSurface(env, angleDevice, 0, 0))
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not create surface!");
                return (jlong) 0;
            }

            sk_sp<const GrGLInterface> glInterface(GrGLMakeAssembledInterface(
                nullptr,
                [](void *ctx, const char name[]) -> GrGLFuncPtr { return eglGetProcAddress(name); }));

            angleDevice->backendContext = glInterface;

            return toJavaPointer(angleDevice);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            auto code = GetExceptionCode();
            throwJavaRenderExceptionByExceptionCode(env, __FUNCTION__, code);
        }
        return (jlong) 0;
    }

    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_makeCurrent(
        JNIEnv *env, jobject redrawer, jlong devicePtr)
    {
        AngleDevice *angleDevice = fromJavaPointer<AngleDevice *>(devicePtr);
        if (!eglMakeCurrent(angleDevice->display, angleDevice->surface, angleDevice->surface, angleDevice->context))
        {
            throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not make context current!");
        }
    }

    JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_makeAngleContext(
        JNIEnv *env, jobject redrawer, jlong devicePtr)
    {
        AngleDevice *angleDevice = fromJavaPointer<AngleDevice *>(devicePtr);
        sk_sp<const GrGLInterface> backendContext = angleDevice->backendContext;
        return toJavaPointer(GrDirectContexts::MakeGL(backendContext).release());
    }

    JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_makeAngleRenderTarget(
        JNIEnv *env, jobject redrawer, jlong devicePtr, jint width, jint height)
    {
        __try
        {
            AngleDevice *angleDevice = fromJavaPointer<AngleDevice *>(devicePtr);

            if (!initAngleSurface(env, angleDevice, width, height))
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not create surface!");
                return (jlong) 0;
            }

            angleDevice->backendContext->fFunctions.fViewport(0, 0, width, height);

            GrGLint buffer;
            angleDevice->backendContext->fFunctions.fGetIntegerv(GR_GL_FRAMEBUFFER_BINDING, &buffer);

            GrGLFramebufferInfo glInfo = { static_cast<unsigned int>(buffer), GR_GL_RGBA8 };
            GrBackendRenderTarget renderTarget = GrBackendRenderTargets::MakeGL(width,
                                                                                height,
                                                                                0,
                                                                                8,
                                                                                glInfo);
            GrBackendRenderTarget *pRenderTarget = new GrBackendRenderTarget(renderTarget);

            return toJavaPointer(pRenderTarget);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            auto code = GetExceptionCode();
            throwJavaRenderExceptionByExceptionCode(env, __FUNCTION__, code);
        }
        return (jlong) 0;
    }

    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_swapBuffers(
        JNIEnv *env, jobject redrawer, jlong devicePtr, jboolean waitForVsync)
    {
        __try
        {
            AngleDevice *angleDevice = fromJavaPointer<AngleDevice *>(devicePtr);
            eglSwapInterval(angleDevice->display, waitForVsync ? 1 : 0);
            if (!eglSwapBuffers(angleDevice->display, angleDevice->surface))
            {
                throwJavaRenderExceptionWithMessage(env, __FUNCTION__, "Could not complete eglSwapBuffers.");
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            auto code = GetExceptionCode();
            throwJavaRenderExceptionByExceptionCode(env, __FUNCTION__, code);
        }
    }

    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_redrawer_AngleRedrawerKt_disposeDevice(
        JNIEnv *env, jobject redrawer, jlong devicePtr)
    {
        AngleDevice *angleDevice = fromJavaPointer<AngleDevice *>(devicePtr);
        eglMakeCurrent(angleDevice->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        delete angleDevice;
    }
} // end extern C

#endif
