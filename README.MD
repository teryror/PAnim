# PAnim - Programmatic Animation in C

Inspired by [manim](https://github.com/3b1b/manim/), the animation engine used
for the educational math videos on [3Blue1Brown](https://www.youtube.com/3blue1brown),
PAnim is intended to be used in the creation of educational computer science
videos.

One of the primary design goals is the ability to annotate a C program in a
manner similar to printf-debugging to make a valid scene description, i.e. the
ability to focus on the algorithm to be presented, rather than its animation.

Another design goal is to allow previewing the animation without having to
render it to a video file. Hot loading scene description code is another avenue
of workflow optimization, though it is not high on the list of priorities right
now.

## Development Plan

The basic idea is to use [SDL2](https://www.libsdl.org/) to render animations
to a window, and every frame, grab the image data and send it to [libavcodec]
(https://ffmpeg.org/) for encoding.

The animation engine itself will likely break into two parts:

* an API for constructing a scene consisting of a collection of _animation
  objects_, and a _timeline of animation events_,
* a system for playing back scenes in a deterministic fashion, i.e. if it
  misses the target frame time, the animation preview will slow down - but the
  generated video will be the same every time.

The implication here is that the scene description code has to run from start
to finnish before the animation can be viewed, which may become a problem for
animations of slower algorithms. Generally, though, the animation of a
program will always be slower than the actual program executing at CPU
speed, so this is not expected to become a serious issue, especially when
compared to compile times.

To avoid over-engineering an over-generalized solution, the system will
initially be quite minimal, and extensions will be necessary for almost all new
animations. It is an open question how the uniqueness of each animation should
be accomodated in the long run.

## Building

Currently, only building on Windows with MSVC is supported, and only MSVC
version 19 was tested. You should be able to use other versions, or even
[clang-cl](https://clang.llvm.org/docs/UsersManual.html#clang-cl), however.

All dependencies are checked into the repository, you should be able to simply
run __build.bat__ from the repository root, without installing any additional
software. This will build __bin\panim.exe__ from __src\panim.c__.

Should you encounter issues, refer to __vid_test.c__ and __sdl_test.c__ to
verify that both libavcodec and SDL work in your environment.
