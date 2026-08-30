#pragma once
// Ported from jew-dick-hack: src/gui/animation/animation.{h,cpp}
// Easing helpers used for menu / window transitions.
#include <cmath>

namespace anim
{
	// 0..1 exponential ease-out. time = seconds since start, delay = start
	// offset in seconds, duration = total seconds of the animation.
	inline float ease_expo(float time, float delay, float duration)
	{
		if (duration <= 0.f)
			return 1.f;
		if (time <= delay)
			return 0.f;

		float t = (time - delay) / duration;
		if (t < 0.f) t = 0.f;
		if (t > 1.f) t = 1.f;
		if (t >= 1.f)
			return 1.f;

		return 1.f - std::pow(2.f, -10.f * t);
	}

	inline float ease_cubic_out(float progress)
	{
		if (progress < 0.f) progress = 0.f;
		if (progress > 1.f) progress = 1.f;
		return 1.f - std::pow(1.f - progress, 3.f);
	}

	inline float ease_cubic_in(float progress)
	{
		if (progress < 0.f) progress = 0.f;
		if (progress > 1.f) progress = 1.f;
		return progress * progress * progress;
	}
}
