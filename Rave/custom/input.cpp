#include "colors.hpp"

namespace input
{
	inline _V4 line = colors::Main;
	inline _V4 lineAct = colors::Second;

	inline _V4 TextC = colors::Gray;
	inline _V4 TextAct = colors::Lwhite;
}

auto items::input(const std::string& label, char* buf, size_t bufsize) noexcept -> bool
{
	using namespace input;
	ImGuiWindow* window = GetCurrentWindow();

	_V2 pos(window->DC.CursorPos);
	_V2 _size(window->Size.x - GetCursorPosX() * 2, 6);

	std::string L = "###" + label;

	PushStyleVar(ImGuiStyleVar_FramePadding, _V2(0, _size.y));
	PushStyleColor(ImGuiCol_FrameBg, colors::Transparent);
	PushStyleColor(ImGuiCol_Text, TextAct);

	_V2 size(_size.x, GetFrameHeight());

	bool filled = strlen(buf) != 0;
	if (!filled)
		window->DrawList->AddText(pos + _V2(0, _size.y), _CO(TextC), label.c_str());

	SetNextItemWidth(size.x);
	bool r = InputText(L.c_str(), buf, bufsize, 0);
	bool act = filled || IsItemActive();

	float fact = 1;
	window->DrawList->AddRectFilled(pos + _V2(0, size.y - fact * 2), pos + size - _V2(0, fact), _CO(act ? lineAct : line));
	if (act)
		window->DrawList->AddShadowRect(pos + _V2(0, size.y - fact * 2), pos + size - _V2(0, fact), _CO(lineAct), 15.f, {});

	PopStyleVar();
	PopStyleColor(2);

	return r;
}
