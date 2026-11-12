#include "colors.hpp"

namespace buttoncol
{
	float rounding = 0;

	_V4 bgCol = colors::Main;
	_V4 bgHov = colors::Second;
	_V4 bgAct = colors::Main;

	_V4 textC = RGBA(11, 11, 11, 255);
	_V4 textHov = RGBA(11, 11, 11, 255);
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

	_V4 bgcol = act ? bgAct : hov ? bgHov : bgCol;
	_V4 textcol = act ? textAct : hov ? textHov : textC;

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
