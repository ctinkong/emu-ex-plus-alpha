#pragma once

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

#include <imagine/config/defs.hh>
#include <imagine/gfx/defs.hh>
#include "GLTask.hh"
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/base/GLContext.hh>
#include <imagine/base/MessagePort.hh>

namespace IG
{
class Semaphore;
}

namespace Base
{
class Window;
}

namespace Gfx
{

class DrawableHolder;
class RendererTask;
class RendererCommands;
class DrawContextSupport;

class GLRendererTask : public GLTask
{
public:
	using Command = GLTask::Command;
	using CommandMessage = GLTask::CommandMessage;

	GLRendererTask();
	GLRendererTask(const char *debugLabel, Renderer &r, Base::GLContext context, int threadPriority = 0);
	GLRendererTask(GLRendererTask &&o) = default;
	GLRendererTask &operator=(GLRendererTask &&o) = default;
	void initVBOs();
	GLuint getVBO();
	void initVAO();
	void initDefaultFramebuffer();
	GLuint defaultFBO() const { return defaultFB; }
	GLuint bindFramebuffer(Texture &tex);
	bool handleDrawableReset();
	void runInitialCommandsInGLThread(TaskContext ctx, DrawContextSupport &support);
	void setRenderer(Renderer *r);
	void verifyCurrentContext(Base::GLDisplay glDpy) const;
	RendererCommands makeRendererCommands(GLTask::TaskContext taskCtx, bool manageSemaphore,
		DrawableHolder &drawableHolder, Base::Window &win, Viewport viewport, Mat4 projMat);

	template<class Func>
	void run(Func &&del, bool awaitReply = false) { GLTask::run(std::forward<Func>(del), awaitReply); }

	template<class Func>
	void draw(DrawableHolder &drawableHolder, Base::Window &win, Base::WindowDrawParams winParams, DrawParams params,
		const Viewport &viewport, const Mat4 &projMat, Func &&del)
	{
		doPreDraw(drawableHolder, win, winParams, params);
		bool manageSemaphore = params.asyncMode() == DrawAsyncMode::PRESENT;
		bool awaitReply = params.asyncMode() != DrawAsyncMode::FULL;
		run([=, this, &drawableHolder, &win, &viewport, &projMat](TaskContext ctx)
			{
				auto cmds = makeRendererCommands(ctx, manageSemaphore, drawableHolder, win, viewport, projMat);
				del(drawableHolder, win, cmds);
			}, awaitReply);
	}

	// for iOS EAGLView renderbuffer management
	void setIOSDrawableDelegates();
	IG::Point2D<int> makeIOSDrawableRenderbuffer(void *layer, GLuint &colorRenderbuffer, GLuint &depthRenderbuffer);
	void deleteIOSDrawableRenderbuffer(GLuint colorRenderbuffer, GLuint depthRenderbuffer);

protected:
	Renderer *r{};
	#ifndef CONFIG_GFX_OPENGL_ES
	GLuint streamVAO = 0;
	std::array<GLuint, 6> streamVBO{};
	uint32_t streamVBOIdx = 0;
	#endif
	IG_enableMemberIf(Config::Gfx::GLDRAWABLE_NEEDS_FRAMEBUFFER, GLuint, defaultFB){};
	GLuint fbo = 0;
	bool resetDrawable = false;
	IG_enableMemberIf(Config::Gfx::OPENGL_DEBUG_CONTEXT, bool, debugEnabled){};

	void doPreDraw(DrawableHolder &drawableHolder, Base::Window &win, Base::WindowDrawParams winParams, DrawParams &params);
};

using RendererTaskImpl = GLRendererTask;

}
