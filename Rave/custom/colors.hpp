#pragma once
#include "custom.hpp"

namespace colors
{
	inline _V4 Bg = RGBA(11, 11, 11, 51);

	inline _V4 Main = RGBA(106, 221, 187, 255);
	inline _V4 Second = RGBA(64, 214, 170, 255);

	inline _V4 Gray = RGBA(152, 152, 152, 255);
	inline _V4 Lwhite = RGBA(228, 228, 228, 255);

	inline _V4 Green = RGBA(136, 212, 129, 255);
	inline _V4 Red = RGBA(213, 84, 84, 255);
	inline _V4 Info = RGBA(101, 101, 101, 255);

	inline _V4 itemBg = RGBA(11, 11, 11, 77);

	inline _V4 White = _V4(1, 1, 1, 1);
	inline _V4 Transparent = _V4(0, 0, 0, 0);

	inline _V4 fromRgb(uint32_t rgb, float alpha = 1.f)
	{
		return _V4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha);
	}

	inline _V4 hoverOf(const _V4& base)
	{
		float h, s, v;
		ColorConvertRGBtoHSV(base.x, base.y, base.z, h, s, v);
		s = std::min(1.f, s * 1.35f);
		v = v * 0.97f;
		_V4 out(0, 0, 0, base.w);
		ColorConvertHSVtoRGB(h, s, v, out.x, out.y, out.z);
		return out;
	}

	inline void setAccent(uint32_t rgb)
	{
		Main = fromRgb(rgb);
		Second = hoverOf(Main);
	}

	inline _V4 withAlpha(const _V4& c, float alpha)
	{
		return _V4(c.x, c.y, c.z, alpha);
	}
}
