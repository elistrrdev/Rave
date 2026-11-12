#include "colors.hpp"

namespace tabcol
{
	_V4 textC = colors::Gray;
	_V4 textHov = colors::Lwhite;
	_V4 textAct = colors::Main;
}

auto items::tab(const std::string& label, bool cond) noexcept -> bool
{
	using namespace tabcol;

	ImGuiWindow* window = GetCurrentWindow();

	_V2 pos = window->DC.CursorPos;
	_V2 size = _C(label.c_str());

	bool r = InvisibleButton(label.c_str(), size);
	bool hov = IsItemHovered();

	window->DrawList->AddText(
		pos,
		_CO(cond ? textAct : hov ? textHov : textC),
		label.c_str()
	);

	return r;
}
