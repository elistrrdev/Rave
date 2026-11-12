#include "colors.hpp"

auto items::tab(const std::string& label, bool cond) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();

	_V2 pos = window->DC.CursorPos;
	_V2 size = _C(label.c_str());

	bool r = InvisibleButton(label.c_str(), size);
	bool hov = IsItemHovered();

	window->DrawList->AddText(
		pos,
		_CO(cond ? colors::Main : hov ? colors::Lwhite : colors::Gray),
		label.c_str()
	);

	return r;
}
