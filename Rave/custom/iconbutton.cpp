#include "colors.hpp"

namespace iconcol
{
	_V4 iconC = colors::Info;
	_V4 iconAct = colors::Lwhite;
}

auto items::iconButton(const std::string& icon) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();
	using namespace iconcol;

	_V2 pos = window->DC.CursorPos;
	_V2 size = _C(icon.c_str());

	bool r = InvisibleButton(icon.c_str(), size);
	bool hov = IsItemHovered();

	window->DrawList->AddText(
		pos,
		_CO(hov ? iconAct : iconC),
		icon.c_str()
	);

	return r;
}
