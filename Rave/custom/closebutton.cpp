#include "colors.hpp"

auto items::closeButton(const std::string& id, const _V2& size) noexcept -> bool
{
	ImGuiWindow* window = GetCurrentWindow();

	_V2 pos = window->DC.CursorPos;
	bool r = InvisibleButton(id.c_str(), size);
	bool hov = IsItemHovered();

	ImU32 col = _CO(hov ? colors::Lwhite : colors::Info);
	const float pad = 1.5f;
	const float thickness = 1.6f;
	window->DrawList->AddLine(pos + _V2(pad, pad), pos + size - _V2(pad, pad), col, thickness);
	window->DrawList->AddLine(pos + _V2(size.x - pad, pad), pos + _V2(pad, size.y - pad), col, thickness);

	return r;
}
