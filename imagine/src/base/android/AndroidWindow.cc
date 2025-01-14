/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "Window"
#include "../common/windowPrivate.hh"
#include "../common/screenPrivate.hh"
#include "android.hh"
#include "internal.hh"
#include <imagine/util/fd-utils.h>
#include <imagine/logger/logger.h>
#include <imagine/base/Base.hh>
#include <imagine/base/Screen.hh>
#include <imagine/base/sharedLibrary.hh>
#include <android/native_activity.h>
#include <android/native_window_jni.h>
#include <android/looper.h>

namespace Base
{

extern JavaInstMethod<jobject(jobject, jlong)> jPresentation;
static JavaInstMethod<void()> jPresentationDeinit{};
static int32_t (*ANativeWindow_setFrameRate)(ANativeWindow* window, float frameRate, int8_t compatibility){};

Window *deviceWindow()
{
	return Window::window(0);
}

static void initPresentationJNI(JNIEnv* env, jobject presentation)
{
	if(jPresentationDeinit)
		return; // already init
	logMsg("Setting up Presentation JNI functions");
	auto cls = env->GetObjectClass(presentation);
	jPresentationDeinit.setup(env, cls, "deinit", "()V");
	JNINativeMethod method[] =
	{
		{
			"onSurfaceCreated", "(JLandroid/view/Surface;)V",
			(void*)(void (*)(JNIEnv*, jobject, jlong, jobject))
			([](JNIEnv* env, jobject thiz, jlong windowAddr, jobject surface)
			{
				auto nWin = ANativeWindow_fromSurface(env, surface);
				auto &win = *((Window*)windowAddr);
				win.setNativeWindow(nWin);
			})
		},
		{
			"onSurfaceRedrawNeeded", "(J)V",
			(void*)(void (*)(JNIEnv*, jobject, jlong))
			([](JNIEnv* env, jobject thiz, jlong windowAddr)
			{
				auto &win = *((Window*)windowAddr);
				androidWindowNeedsRedraw(win, true);
			})
		},
		{
			"onSurfaceDestroyed", "(J)V",
			(void*)(void (*)(JNIEnv*, jobject, jlong))
			([](JNIEnv* env, jobject thiz, jlong windowAddr)
			{
				auto &win = *((Window*)windowAddr);
				ANativeWindow_release(win.nativeObject());
				win.setNativeWindow(nullptr);
			})
		},
		{
			"onWindowDismiss", "(J)V",
			(void*)(void (*)(JNIEnv*, jobject, jlong))
			([](JNIEnv* env, jobject thiz, jlong windowAddr)
			{
				auto &win = *((Window*)windowAddr);
				win.dismiss();
			})
		},
	};
	env->RegisterNatives(cls, method, std::size(method));
}

IG::Point2D<float> Window::pixelSizeAsMM(IG::Point2D<int> size)
{
	auto [xDPI, yDPI] = screen()->dpi();
	assert(xDPI && yDPI);
	float xdpi = surfaceRotationIsStraight(osRotation) ? xDPI : yDPI;
	float ydpi = surfaceRotationIsStraight(osRotation) ? yDPI : xDPI;
	return {((float)size.x / xdpi) * 25.4f, ((float)size.y / ydpi) * 25.4f};
}

IG::Point2D<float> Window::pixelSizeAsSMM(IG::Point2D<int> size)
{
	auto densityDPI = screen()->densityDPI();
	assert(densityDPI);
	return {((float)size.x / densityDPI) * 25.4f, ((float)size.y / densityDPI) * 25.4f};
}

bool Window::setValidOrientations(Orientation oMask)
{
	using namespace Base;
	logMsg("requested orientation change to %s", Base::orientationToStr(oMask));
	int toSet = -1;
	switch(oMask)
	{
		bdefault: toSet = -1; // SCREEN_ORIENTATION_UNSPECIFIED
		bcase VIEW_ROTATE_0: toSet = 1; // SCREEN_ORIENTATION_PORTRAIT
		bcase VIEW_ROTATE_90: toSet = 0; // SCREEN_ORIENTATION_LANDSCAPE
		bcase VIEW_ROTATE_180: toSet = 9; // SCREEN_ORIENTATION_REVERSE_PORTRAIT
		bcase VIEW_ROTATE_270: toSet = 8; // SCREEN_ORIENTATION_REVERSE_LANDSCAPE
		bcase VIEW_ROTATE_90 | VIEW_ROTATE_270: toSet = 6; // SCREEN_ORIENTATION_SENSOR_LANDSCAPE
		bcase VIEW_ROTATE_0 | VIEW_ROTATE_180: toSet = 7; // SCREEN_ORIENTATION_SENSOR_PORTRAIT
		bcase VIEW_ROTATE_ALL: toSet = 10; // SCREEN_ORIENTATION_FULL_SENSOR
	}
	jSetRequestedOrientation(jEnvForThread(), jBaseActivity, toSet);
	return true;
}

bool Window::requestOrientationChange(Orientation o)
{
	// no-op, OS manages orientation changes
	return false;
}

PixelFormat Window::defaultPixelFormat()
{
	return ((Config::ARM_ARCH && Config::ARM_ARCH < 7) || androidSDK() < 11) ? PIXEL_FMT_RGB565 : PIXEL_FMT_RGBA8888;
}

IG::ErrorCode Window::init(const WindowConfig &config)
{
	if(initialInit)
		return {};
	BaseWindow::init(config);
	if(!Config::BASE_MULTI_WINDOW && windows())
	{
		bug_unreachable("no multi-window support");
	}
	this->screen_ = &config.screen();

	initialInit = true;
	window_.push_back(this);
	if(window_.size() > 1)
	{
		assert(screen() != Screen::screen(0));
		auto env = jEnvForThread();
		jDialog = env->NewGlobalRef(jPresentation(env, Base::jBaseActivity, screen()->displayObject(), (jlong)this));
		initPresentationJNI(env, jDialog);
		logMsg("making dialog window:%p", jDialog);
	}
	else
	{
		logMsg("making device window");
	}
	pixelFormat = config.format();
	if(Base::androidSDK() < 11 && this == deviceWindow())
	{
		// In testing with CM7 on a Droid, not setting window format to match
		// what's used in ANativeWindow_setBuffersGeometry() may cause performance issues
		auto env = jEnvForThread();
		if(Config::DEBUG_BUILD)
			logMsg("setting window format to %d (current %d)", pixelFormat, jWinFormat(env, jBaseActivity));
		jSetWinFormat(env, jBaseActivity, pixelFormat);
	}
	// default to screen's size
	updateSize({screen()->width(), screen()->height()});
	contentRect.x2 = width();
	contentRect.y2 = height();
	return {};
}

void Window::deinit()
{
	assert(this != deviceWindow());
	if(jDialog)
	{
		logMsg("deinit dialog window:%p", jDialog);
		if(nWin)
		{
			ANativeWindow_release(nWin);
		}
		setNativeWindow(nullptr);
		auto env = jEnvForThread();
		jPresentationDeinit(env, jDialog);
		env->DeleteGlobalRef(jDialog);
		jDialog = {};
	}
	nWin = nullptr;
	initialInit = false;
}

void Window::show()
{
	postDraw();
}

IG::WindowRect Window::contentBounds() const
{
	return contentRect;
}

bool Window::hasSurface() const
{
	return nWin;
}

void AndroidWindow::updateContentRect(const IG::WindowRect &rect)
{
	contentRect = rect;
	surfaceChange.addContentRectResized();
}

bool AndroidWindow::operator ==(AndroidWindow const &rhs) const
{
	assert(initialInit);
	assert(rhs.initialInit);
	return jDialog == rhs.jDialog;
}

AndroidWindow::operator bool() const
{
	return initialInit;
}

void AndroidWindow::setNativeWindow(ANativeWindow *nWindow)
{
	if(nWin)
	{
		static_cast<Window*>(this)->unpostDraw();
		static_cast<Window*>(this)->surfaceChange.addDestroyed();
		nWin = nullptr;
	}
	if(!nWindow)
		return;
	nWin = nWindow;
	if(Config::DEBUG_BUILD)
		logMsg("creating window with native visual ID: %d with format: %d", pixelFormat, ANativeWindow_getFormat(nWindow));
	ANativeWindow_setBuffersGeometry(nWindow, 0, 0, pixelFormat);
}

NativeWindow Window::nativeObject() const
{
	return nWin;
}

void Window::setIntendedFrameRate(double rate)
{
	if(Base::androidSDK() < 30)
		return;
	if(unlikely(!nWin))
		return;
	if(unlikely(!ANativeWindow_setFrameRate))
	{
		auto lib = Base::openSharedLibrary("libnativewindow.so");
		Base::loadSymbol(ANativeWindow_setFrameRate, lib, "ANativeWindow_setFrameRate");
	}
	if(ANativeWindow_setFrameRate(nWin, rate, 0))
	{
		logErr("error in ANativeWindow_setFrameRate() with window:%p rate:%.2f", nWin, rate);
	}
}

int AndroidWindow::nativePixelFormat()
{
	return pixelFormat;
}

void androidWindowNeedsRedraw(Window &win, bool sync)
{
	win.setNeedsDraw(true);
	if(!appIsRunning())
	{
		logMsg("deferring window surface redraw until app resumes");
		return;
	}
	logMsg("window surface redraw needed");
	win.dispatchOnDraw(sync);
	if(sync)
	{
		// On some OS versions like CM7 on the HP Touchpad,
		// the very next frame is rendered incorrectly
		// (as if the window still has its previous size).
		// A similar issue occurs on the stock AT&T Xperia Play
		// when sliding the phone open from sleep with the
		// screen previously in portrait.
		// Post another window draw to work-around this
		win.postDraw();
	}
}

void AndroidWindow::setContentRect(const IG::WindowRect &rect, const IG::Point2D<int> &winSize)
{
	logMsg("content rect changed: %d:%d:%d:%d in %dx%d",
		rect.x, rect.y, rect.x2, rect.y2, winSize.x, winSize.y);
	updateContentRect(rect);
	auto &win = *static_cast<Window*>(this);
	if(win.updateSize(winSize))
	{
		// If the surface changed size, make sure
		// it's set again with eglMakeCurrent to avoid
		// the context possibly rendering to it with
		// the old size, as occurs on Intel HD Graphics
		surfaceChange.addReset();
	}
	win.postDraw();
}

void Window::setTitle(const char *name) {}

void Window::setAcceptDnd(bool on) {}

}
