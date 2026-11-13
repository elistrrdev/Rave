#include "colors.hpp"

namespace buttoncol
{
	float rounding = 0;

	_V4 textC = RGBA(11, 11, 11, 255);
	_V4 textAct = RGBA(6, 6, 6, 255);
}

auto items::button(const std::string& label, const _V2& size) noexcept -> bool
{
	using namespace buttoncol;

	ImGuiWindow* window = GetCurrentWindow();

	std::string shown = label.substr(0, label.find("##"));
	_V2 lSize(_C(shown.c_str()));
	_V2 pos = window->DC.CursorPos;

	bool r = InvisibleButton(label.c_str(), size);
	bool act(IsItemActive()); bool hov(IsItemHovered());

	_V4 bgcol = act ? colors::Main : hov ? colors::Second : colors::Main;
	_V4 textcol = act ? textAct : textC;

	window->DrawList->AddRectFilled(pos,
		pos + size,
		_CO(bgcol),
		rounding
	);

	window->DrawList->AddText(
		pos + (size - lSize) / 2,
		_CO(textcol),
		shown.c_str()
	);

	return r;
}

auto items::choice(const std::string& label, bool selected, const _V2& size, float rounding) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();

	std::string shown = label.substr(0, label.find("##"));
	_V2 lSize(_C(shown.c_str()));
	_V2 pos = window->DC.CursorPos;

	bool r = InvisibleButton(label.c_str(), size);
	bool hov = IsItemHovered();

	_V4 bgcol = selected ? colors::Main : hov ? _V4(RGBA(255, 255, 255, 31)) : _V4(RGBA(255, 255, 255, 15));
	_V4 textcol = selected ? buttoncol::textC : colors::Lwhite;

	window->DrawList->AddRectFilled(pos, pos + size, _CO(bgcol), rounding);
	window->DrawList->AddText(pos + (size - lSize) / 2, _CO(textcol), shown.c_str());

	return r;
}

auto items::danger(const std::string& label, const _V2& size, float rounding) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();

	std::string shown = label.substr(0, label.find("##"));
	_V2 lSize(_C(shown.c_str()));
	_V2 pos = window->DC.CursorPos;

	bool r = InvisibleButton(label.c_str(), size);
	bool act = IsItemActive();
	bool hov = IsItemHovered();

	_V4 bgcol = act ? colors::Red : hov ? colors::hoverOf(colors::Red) : colors::Red;
	_V4 textcol = act ? buttoncol::textAct : buttoncol::textC;

	window->DrawList->AddRectFilled(pos, pos + size, _CO(bgcol), rounding);
	window->DrawList->AddText(pos + (size - lSize) / 2, _CO(textcol), shown.c_str());

	return r;
}

auto items::swatch(const std::string& id, uint32_t rgb, bool selected, const _V2& size) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();
	_V2 pos = window->DC.CursorPos;

	bool r = InvisibleButton(id.c_str(), size);
	bool hov = IsItemHovered();

	_V4 main = colors::fromRgb(rgb);
	_V4 hover = colors::hoverOf(main);
	_V4 soft = colors::withAlpha(main, 0.35f);

	const float rounding = 6.f;
	float first = std::floor(size.x * 0.5f);
	float second = std::floor(size.x * 0.78f);

	window->DrawList->AddRectFilled(pos, pos + _V2(first, size.y), _CO(main), rounding, ImDrawFlags_RoundCornersLeft);
	window->DrawList->AddRectFilled(pos + _V2(first, 0), pos + _V2(second, size.y), _CO(hover), 0.f);
	window->DrawList->AddRectFilled(pos + _V2(second, 0), pos + size, _CO(_V4(RGBA(16, 18, 18, 255))), rounding, ImDrawFlags_RoundCornersRight);
	window->DrawList->AddRectFilled(pos + _V2(second, 0), pos + size, _CO(soft), rounding, ImDrawFlags_RoundCornersRight);

	if (selected)
		window->DrawList->AddRect(pos - _V2(3, 3), pos + size + _V2(3, 3), _CO(colors::Lwhite), rounding + 2.f, 0, 2.f);
	else if (hov)
		window->DrawList->AddRect(pos - _V2(2, 2), pos + size + _V2(2, 2), _CO(_V4(RGBA(228, 228, 228, 110))), rounding + 2.f, 0, 1.f);

	return r;
}
