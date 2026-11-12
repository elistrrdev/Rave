#include "colors.hpp"
#include "core/accounts.hpp"
#include "core/fflags.hpp"
#include "core/launcher.hpp"
#include "core/settings.hpp"
#include "core/sysinfo.hpp"
#include "core/system.hpp"
#include <thread>

static std::string windowsName;

namespace logo
{
	static ID3D11Device* device = nullptr;
	static gfx::Image original;
	static uint32_t shown = 0xFFFFFFFF;
	static const uint32_t kBase = 0x6addbb;

	static float luminance(float r, float g, float b)
	{
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	}

	static void refresh(uint32_t accent)
	{
		if (accent == shown || original.pixels.empty())
			return;

		gfx::Image img = original;
		if (accent != kBase)
		{
			float br = (float)((kBase >> 16) & 0xFF), bg = (float)((kBase >> 8) & 0xFF), bb = (float)(kBase & 0xFF);
			float ar = (float)((accent >> 16) & 0xFF), ag = (float)((accent >> 8) & 0xFF), ab = (float)(accent & 0xFF);
			float baseLum = luminance(br, bg, bb);
			for (size_t i = 0; i < img.pixels.size(); i += 4)
			{
				if (img.pixels[i + 3] == 0)
					continue;
				float lum = luminance(img.pixels[i], img.pixels[i + 1], img.pixels[i + 2]);
				float r, g, b;
				if (lum >= baseLum)
				{
					float t = std::clamp((lum - baseLum) / (255.f - baseLum), 0.f, 1.f);
					r = ar + (255.f - ar) * t;
					g = ag + (255.f - ag) * t;
					b = ab + (255.f - ab) * t;
				}
				else
				{
					float t = lum / baseLum;
					r = ar * t;
					g = ag * t;
					b = ab * t;
				}
				img.pixels[i] = (unsigned char)std::clamp(r + 0.5f, 0.f, 255.f);
				img.pixels[i + 1] = (unsigned char)std::clamp(g + 0.5f, 0.f, 255.f);
				img.pixels[i + 2] = (unsigned char)std::clamp(b + 0.5f, 0.f, 255.f);
			}
		}

		ID3D11ShaderResourceView* view = gfx::createTexture(device, img);
		if (!view)
			return;
		if (images::logo)
			images::logo->Release();
		images::logo = view;
		images::logoSize = _V2((float)img.w, (float)img.h);
		shown = accent;
	}
}

static uint32_t accentNow = 0x6addbb;

static void applyAccent(uint32_t rgb)
{
	accentNow = rgb;
	colors::setAccent(rgb);
}

namespace prefs
{
	static bool openRequest = false;
	static settings::Values draft, original;
	static const char* kPopup = "SETTINGS";

	static void open()
	{
		draft = original = settings::get();
		openRequest = true;
	}

	static void saveAndClose()
	{
		bool changed = draft.behavior != original.behavior || draft.accent != original.accent;
		if (changed)
		{
			settings::set(draft);
			if (settings::save())
				items::sendLog("Settings saved", logType::success);
			else
				items::sendLog("Could not write settings.json", logType::error);
		}
		CloseCurrentPopup();
	}
}

namespace editor
{
	static bool openRequest = false;
	static std::string text;
	static std::string original;
	static std::string status;
	static _V4 statusColor;

	static const char* kPopup = "FFLAGS EDITOR";
	static const char* kHint = "{\n    \"DFIntTaskSchedulerTargetFps\": 9999\n}";

	static void open()
	{
		text = fflags::load();
		original = text;
		status.clear();
		openRequest = true;
	}

	static bool saveAndClose()
	{
		if (text == original)
		{
			ClearActiveID();
			CloseCurrentPopup();
			return true;
		}
		std::string error;
		if (!fflags::save(text, error))
		{
			status = error;
			statusColor = colors::Red;
			return false;
		}
		ClearActiveID();
		CloseCurrentPopup();
		items::sendLog("FFlags saved", logType::success);
		return true;
	}

	static int resize(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			std::string* str = (std::string*)data->UserData;
			str->resize(data->BufTextLen);
			data->Buf = str->data();
		}
		return 0;
	}
}

void ui::BeforeLoop(ID3D11Device* dvc) {
	f.RenderFonts();
	misc::set_blur(window::dx::hwnd);

	applyAccent(settings::get().accent);

	logo::device = dvc;
	HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(3), RT_RCDATA);
	HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
	if (mem && gfx::decodeImage(LockResource(mem), SizeofResource(nullptr, res), 30, logo::original))
		logo::refresh(accentNow);

	launcher::setWindow(window::dx::hwnd);

	windowsName = sys::windowsName();
	std::thread([] { sysinfo::get(); }).detach();

	launcher::Logger logger = [](const std::string& text, launcher::LogLevel level) {
		logType type = logType::info;
		switch (level)
		{
		case launcher::LogLevel::Success: type = logType::success; break;
		case launcher::LogLevel::Error: type = logType::error; break;
		case launcher::LogLevel::Warn: type = logType::warn; break;
		default: break;
		}
		items::sendLog(text, type);
	};
	launcher::setLogger(logger);
	accounts::setLogger(logger);
	accounts::load();
	launcher::startMonitor();
}

static std::string launchLabel()
{
	switch (launcher::task())
	{
	case launcher::Task::Checking: return "CHECKING...";
	case launcher::Task::Installing:
	{
		int percent = (int)(launcher::progress() * 100.f + 0.5f);
		return "INSTALLING " + std::to_string(std::clamp(percent, 0, 100)) + "%";
	}
	case launcher::Task::Launching: return "LAUNCHING...";
	default: return "LAUNCH ROBLOX";
	}
}

static float wrapText(ImGuiWindow* window, const _V2& localPos, const _V4& color, const std::string& text, float width)
{
	_V2 pos = window->Pos + localPos - _V2(window->Scroll.x, window->Scroll.y);
	window->DrawList->AddText(GetFont(), GetFontSize(), pos, _CO(color), text.c_str(), nullptr, width);
	return CalcTextSize(text.c_str(), nullptr, false, width).y;
}

namespace avatars
{
	static std::map<uint64_t, ID3D11ShaderResourceView*> textures;

	static ID3D11ShaderResourceView* get(uint64_t id)
	{
		auto it = textures.find(id);
		if (it != textures.end())
			return it->second;
		std::string png;
		if (!accounts::avatarPng(id, png))
			return nullptr;
		gfx::Image img;
		ID3D11ShaderResourceView* view = nullptr;
		if (gfx::decodeImage(png.data(), png.size(), 40, img, false))
			view = gfx::createTexture(logo::device, img);
		textures[id] = view;
		return view;
	}

	static void forget(uint64_t id)
	{
		auto it = textures.find(id);
		if (it == textures.end())
			return;
		if (it->second)
			it->second->Release();
		textures.erase(it);
	}
}

namespace addacc
{
	static bool openRequest = false;
	static bool cookieMode = false;
	static char cookie[4096];
	static unsigned addedAtOpen = 0;
	static unsigned qrShown = 0xFFFFFFFF;
	static ID3D11ShaderResourceView* qrTexture = nullptr;
	static const char* kPopup = "ADD ACCOUNT";

	static void clearCookie()
	{
		SecureZeroMemory(cookie, sizeof(cookie));
	}

	static void open()
	{
		cookieMode = false;
		clearCookie();
		accounts::resetPaste();
		addedAtOpen = accounts::addedCount();
		accounts::startQuick();
		openRequest = true;
	}

	static void close()
	{
		accounts::cancelQuick();
		accounts::resetPaste();
		clearCookie();
		ClearActiveID();
		CloseCurrentPopup();
	}

	static void refreshQr(unsigned version)
	{
		if (version == qrShown)
			return;
		qrShown = version;
		if (qrTexture)
		{
			qrTexture->Release();
			qrTexture = nullptr;
		}
		std::string png = accounts::qrPng();
		gfx::Image img;
		if (!png.empty() && gfx::decodeImage(png.data(), png.size(), 132, img, false, true))
			qrTexture = gfx::createTexture(logo::device, img);
	}

	static std::string spacedCode(const std::string& code)
	{
		if (code.empty())
			return "- - - - - -";
		std::string out;
		for (size_t i = 0; i < code.size(); ++i)
		{
			if (i)
				out += ' ';
			out += code[i];
		}
		return out;
	}
}

static void homePage(ImGuiWindow* window)
{
	SetCursorPos({ 20, 100 });
	misc::child("HOME", { window->Size.x - 40, 347 });
	{
		ImGuiWindow* c = GetCurrentWindow();
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 12, 12 });
		{
			PushFont(f.montM[0]);

			SetCursorPos({ 20, 20 });
			misc::child2("LOGS", { c->Size.x - 40, 190 });
			{
				PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 7 });
				items::renderLogs();
				PopStyleVar();
			}
			EndChild();

			SetCursorPosX(20);
			if (items::button(launchLabel() + "###LAUNCH", { c->Size.x - GetCursorPosX() * 2 , 45 }))
				launcher::launch();

			SetCursorPosX(20);
			if (items::button("FFLAGS EDITOR", { GetItemRectSize().x / 2 - 6, 45 }))
				editor::open();

			SameLine();
			if (items::button("SETTINGS", { GetItemRectSize().x, 45 }))
				prefs::open();

			PopFont();
		}
		PopStyleVar();
	}
	EndChild();
}

static void accountRow(const accounts::Account& account, bool selected, float width)
{
	static uint64_t confirmId = 0;
	static double confirmUntil = 0;

	ImGuiWindow* w = GetCurrentWindow();
	ImDrawList* dl = w->DrawList;
	const _V2 pos = w->DC.CursorPos;
	const float height = 58;

	_V4 bg = selected ? colors::withAlpha(colors::Main, 0.10f) : _V4(RGBA(255, 255, 255, 8));
	dl->AddRectFilled(pos, pos + _V2(width, height), _CO(bg), 8.f);
	if (selected)
		dl->AddRect(pos, pos + _V2(width, height), _CO(colors::withAlpha(colors::Main, 0.55f)), 8.f, 0, 1.f);

	const _V2 avatarPos = pos + _V2(10, 9);
	const _V2 avatarSize = { 40, 40 };
	dl->AddRectFilled(avatarPos, avatarPos + avatarSize, _CO(_V4(RGBA(255, 255, 255, 18))), 20.f);
	if (ID3D11ShaderResourceView* tex = avatars::get(account.id))
	{
		dl->AddImageRounded((ImTextureID)tex, avatarPos, avatarPos + avatarSize, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE, 20.f);
	}
	else if (!account.name.empty())
	{
		PushFont(f.montS[0]);
		std::string initial(1, (char)toupper((unsigned char)account.name[0]));
		_V2 size = _C(initial.c_str());
		dl->AddText(avatarPos + (avatarSize - size) / 2, _CO(colors::Lwhite), initial.c_str());
		PopFont();
	}

	std::string title = account.displayName.empty() ? account.name : account.displayName;
	PushFont(f.montM[1]);
	dl->AddText(pos + _V2(62, 9), _CO(colors::Lwhite), title.c_str());
	PopFont();

	std::string sub = "@" + account.name;
	_V4 subColor = colors::Gray;
	if (account.expired)
	{
		sub += " - login expired, add it again";
		subColor = colors::Red;
	}
	dl->AddText(pos + _V2(62, 33), _CO(subColor), sub.c_str());

	const _V2 btn = { 92, 32 };
	const float btnY = pos.y + (height - btn.y) / 2;
	const std::string id = std::to_string(account.id);

	SetCursorScreenPos({ pos.x + width - 10 - btn.x, btnY });
	bool confirming = confirmId == account.id && GetTime() < confirmUntil;
	if (items::choice((confirming ? "SURE?##rm" : "REMOVE##rm") + id, false, btn))
	{
		if (confirming)
		{
			confirmId = 0;
			avatars::forget(account.id);
			accounts::remove(account.id);
		}
		else
		{
			confirmId = account.id;
			confirmUntil = GetTime() + 3.0;
		}
	}

	SetCursorScreenPos({ pos.x + width - 10 - btn.x * 2 - 8, btnY });
	if (items::choice((selected ? "IN USE##use" : "USE##use") + id, selected, btn) && !selected)
		accounts::select(account.id);

	SetCursorScreenPos(pos);
	Dummy({ width, height });
}

static void accountsPage(ImGuiWindow* window)
{
	SetCursorPos({ 20, 100 });
	misc::child("ACCOUNTS", { window->Size.x - 40, 347 });
	{
		ImGuiWindow* c = GetCurrentWindow();
		const float inner = c->Size.x - 40;

		PushFont(f.montM[0]);

		std::vector<accounts::Account> list = accounts::list();
		uint64_t selected = accounts::selected();

		SetCursorPos({ 20, 20 });
		misc::child2("ACCOUNT_LIST", { inner, 250 });
		{
			ImGuiWindow* l = GetCurrentWindow();
			if (list.empty())
			{
				PushFont(f.montM[1]);
				const char* title = "No accounts added yet";
				_V2 ts = _C(title);
				draw::text(title, { (l->Size.x - ts.x) / 2, 92 }, colors::Lwhite);
				PopFont();

				const char* hint = "Add an account and Rave starts Roblox already signed in.";
				_V2 hs = _C(hint);
				draw::text(hint, { (l->Size.x - hs.x) / 2, 124 }, colors::Gray);
			}
			else
			{
				PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 8 });
				SetCursorPos({ 0, 1 });
				for (const auto& account : list)
					accountRow(account, account.id == selected, l->Size.x);
				PopStyleVar();
			}
		}
		EndChild();

		const float half = std::floor((inner - 12) / 2);
		SetCursorPos({ 20, c->Size.y - 20 - 45 });
		if (items::button("ADD ACCOUNT", { half, 45 }))
			addacc::open();

		SetCursorPos({ 20 + half + 12, c->Size.y - 20 - 45 });
		if (items::choice("USE ROBLOX'S OWN LOGIN", selected == 0, { inner - half - 12, 45 }))
			accounts::select(0);

		PopFont();
	}
	EndChild();
}

static void infoPage(ImGuiWindow* window)
{
	static double copiedAt = -10.0;

	SetCursorPos({ 20, 100 });
	misc::child("INFO", { window->Size.x - 40, 347 });
	{
		ImGuiWindow* c = GetCurrentWindow();
		launcher::Snapshot snap = launcher::snapshot();
		const sysinfo::Specs& specs = sysinfo::get();

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 13 });
		{
			PushFont(f.montM[1]);

			SetCursorPos({ 30, 30 });
			misc::row("Rave Version: ", "V" RAVE_VERSION_STR);

			SetCursorPosX(30);
			misc::row("Windows: ", windowsName);

			SetCursorPosX(30);
			misc::row("Latest Roblox: ", snap.latestKnown ? snap.latest : "Checking...");

			SetCursorPosX(30);
			misc::row("CPU: ", specs.cpu);

			SetCursorPosX(30);
			misc::row("GPU: ", specs.gpu);

			SetCursorPosX(30);
			misc::row("RAM: ", specs.ram);

			SetCursorPosX(30);
			misc::row("Display: ", specs.display);

			SetCursorPosX(30);
			misc::row("HardwareID: ", specs.hwid);

			_V2 rowMin = GetItemRectMin(), rowMax = GetItemRectMax();
			bool hovered = IsWindowHovered() && IsMouseHoveringRect(rowMin, rowMax);
			if (hovered)
			{
				SetMouseCursor(ImGuiMouseCursor_Hand);
				if (IsMouseClicked(ImGuiMouseButton_Left))
				{
					SetClipboardText(specs.hwid.c_str());
					copiedAt = GetTime();
				}
			}

			PopFont();

			PushFont(f.montM[0]);
			const char* note = GetTime() - copiedAt < 2.0 ? "copied" : hovered ? "click to copy" : nullptr;
			if (note)
				c->DrawList->AddText(_V2(rowMax.x + 12, rowMax.y - GetFontSize() - 1), _CO(colors::Gray), note);
			PopFont();
		}
		PopStyleVar();
	}
	EndChild();
}

static void addAccountWindow(ImGuiWindow* window)
{
	if (addacc::openRequest)
	{
		OpenPopup(addacc::kPopup);
		addacc::openRequest = false;
	}

	const _V2 size = { 520, 360 };
	SetNextWindowPos(window->Pos + (window->Size - size) / 2, ImGuiCond_Always);
	SetNextWindowSize(size, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	if (!BeginPopupModal(addacc::kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
	{
		PopStyleVar();
		return;
	}

	if (IsKeyPressed(ImGuiKey_Escape, false) || accounts::addedCount() != addacc::addedAtOpen)
	{
		addacc::close();
		EndPopup();
		PopStyleVar();
		return;
	}

	ImGuiWindow* popup = GetCurrentWindow();
	const float inner = popup->Size.x - 40;

	SetCursorPos({ 20, 20 });
	misc::renderMulticolorText({ "ADD ", "ACCOUNT" }, { f.montS[1], f.montS[1] }, { colors::Main, colors::Lwhite });

	PushFont(f.montM[0]);

	const float gap = 10;
	const _V2 modeSize = { std::floor((inner - gap) / 2), 34 };
	SetCursorPos({ 20, 64 });
	if (items::choice("QUICK LOGIN", !addacc::cookieMode, modeSize) && addacc::cookieMode)
	{
		addacc::cookieMode = false;
		addacc::clearCookie();
		accounts::resetPaste();
		accounts::startQuick();
	}
	SetCursorPos({ 20 + modeSize.x + gap, 64 });
	if (items::choice("COOKIE", addacc::cookieMode, { inner - modeSize.x - gap, 34 }) && !addacc::cookieMode)
	{
		addacc::cookieMode = true;
		accounts::cancelQuick();
	}

	if (!addacc::cookieMode)
	{
		accounts::Quick q = accounts::quick();
		addacc::refreshQr(q.qrVersion);

		const _V2 qrPos = { 20, 112 };
		const _V2 qrBox = { 156, 156 };
		_V2 screen = popup->Pos + qrPos;
		if (q.state == accounts::QuickState::Failed)
		{
			popup->DrawList->AddRectFilled(screen, screen + qrBox, _CO(_V4(RGBA(255, 255, 255, 12))), 8.f);
			const _V2 btn = { 120, 34 };
			SetCursorPos(qrPos + (qrBox - btn) / 2);
			if (items::choice("NEW CODE", false, btn))
				accounts::startQuick();
		}
		else if (addacc::qrTexture)
		{
			popup->DrawList->AddRectFilled(screen, screen + qrBox, IM_COL32_WHITE, 8.f);
			popup->DrawList->AddImage((ImTextureID)addacc::qrTexture, screen + _V2(12, 12), screen + _V2(144, 144));
		}
		else
		{
			popup->DrawList->AddRectFilled(screen, screen + qrBox, _CO(_V4(RGBA(255, 255, 255, 12))), 8.f);
			_V2 dots = _C("...");
			popup->DrawList->AddText(screen + (qrBox - dots) / 2, _CO(colors::Gray), "...");
		}

		const float x = 196;
		const float col = popup->Size.x - 20 - x;
		draw::text("YOUR CODE", { x, 112 }, colors::Gray);

		PushFont(f.montS[1]);
		popup->DrawList->AddText(popup->Pos + _V2(x, 132), _CO(colors::Main), addacc::spacedCode(q.code).c_str());
		PopFont();

		float y = 176;
		y += wrapText(popup, { x, y }, colors::Lwhite, "On a signed in device, open Roblox Settings > Quick Log In and enter this code, or scan the QR code.", col);

		_V4 statusColor = colors::Gray;
		switch (q.state)
		{
		case accounts::QuickState::Linked: statusColor = colors::Main; break;
		case accounts::QuickState::SigningIn: statusColor = colors::Main; break;
		case accounts::QuickState::Done: statusColor = colors::Green; break;
		case accounts::QuickState::Failed: statusColor = colors::Red; break;
		default: break;
		}
		if (!q.message.empty())
			wrapText(popup, { x, std::max(y + 12, 236.f) }, statusColor, q.message, col);
	}
	else
	{
		float y = 112;
		y += wrapText(popup, { 20, y }, colors::Lwhite, "Paste the .ROBLOSECURITY cookie of the account. Rave checks it with Roblox and keeps it encrypted on this PC.", inner);
		y += 8;
		wrapText(popup, { 20, y }, colors::Red, "Never send this cookie to anyone. Whoever has it can use the account without the password.", inner);

		const _V2 boxPos = { 20, 196 };
		const _V2 boxSize = { inner, 38 };
		PushStyleVar(ImGuiStyleVar_FramePadding, { 12, 10 });
		PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const _V4 boxColor = RGBA(255, 255, 255, 12);
		PushStyleColor(ImGuiCol_FrameBg, boxColor);
		PushStyleColor(ImGuiCol_FrameBgHovered, boxColor);
		PushStyleColor(ImGuiCol_FrameBgActive, boxColor);
		PushStyleColor(ImGuiCol_Text, colors::Lwhite);
		SetCursorPos(boxPos);
		SetNextItemWidth(boxSize.x);
		InputText("##cookie", addacc::cookie, sizeof(addacc::cookie), ImGuiInputTextFlags_Password);
		bool active = IsItemActive();
		PopStyleColor(4);
		PopStyleVar(2);
		if (addacc::cookie[0] == 0 && !active)
			popup->DrawList->AddText(popup->Pos + boxPos + _V2(12, 10), _CO(colors::Info), "Paste the cookie here");

		const _V2 pasteSize = { 150, 36 };
		SetCursorPos({ 20, 244 });
		if (items::choice("PASTE", false, pasteSize))
		{
			const char* clip = GetClipboardText();
			addacc::clearCookie();
			if (clip)
				strncpy_s(addacc::cookie, sizeof(addacc::cookie), clip, _TRUNCATE);
		}

		accounts::Paste paste = accounts::paste();
		bool checking = paste.state == accounts::PasteState::Checking;
		SetCursorPos({ 20 + pasteSize.x + gap, 244 });
		if (items::button(checking ? "CHECKING...###ADDCOOKIE" : "ADD ACCOUNT###ADDCOOKIE", { inner - pasteSize.x - gap, 36 }) && !checking)
			accounts::addCookie(addacc::cookie);

		if (!paste.message.empty())
		{
			_V4 color = paste.state == accounts::PasteState::Failed ? colors::Red : paste.state == accounts::PasteState::Done ? colors::Green : colors::Gray;
			draw::text(paste.message, { 20, 282 }, color);
		}
	}

	SetCursorPos({ 20, popup->Size.y - 20 - 40 });
	if (items::button("CLOSE", { inner, 40 }))
		addacc::close();

	PopFont();

	EndPopup();
	PopStyleVar();
}

static void fflagsEditor(ImGuiWindow* window)
{
	if (editor::openRequest)
	{
		OpenPopup(editor::kPopup);
		editor::openRequest = false;
	}

	const _V2 size = { 520, 360 };
	SetNextWindowPos(window->Pos + (window->Size - size) / 2, ImGuiCond_Always);
	SetNextWindowSize(size, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	if (!BeginPopupModal(editor::kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
	{
		PopStyleVar();
		return;
	}

	if (IsKeyPressed(ImGuiKey_Escape, false) && editor::saveAndClose())
	{
		EndPopup();
		PopStyleVar();
		return;
	}

	ImGuiWindow* popup = GetCurrentWindow();

	SetCursorPos({ 20, 20 });
	misc::renderMulticolorText({ "FFLAGS ", "EDITOR" }, { f.montS[1], f.montS[1] }, { colors::Main, colors::Lwhite });

	PushFont(f.montM[0]);

	const _V2 btnSize = { 130, 32 };
	SetCursorPos({ popup->Size.x - 20 - btnSize.x * 2 - 10, 16 });
	if (items::button("EXPORT FFLAGS", btnSize))
	{
		SetClipboardText(editor::text.c_str());
		editor::status = "FFlags copied to the clipboard";
		editor::statusColor = colors::Green;
	}

	SameLine(0, 10);
	if (items::button("IMPORT FFLAGS", btnSize))
	{
		const char* clip = GetClipboardText();
		std::string incoming = clip ? clip : "";
		std::string error;
		if (incoming.find_first_not_of(" \t\r\n") == std::string::npos)
		{
			editor::status = "The clipboard is empty";
			editor::statusColor = colors::Red;
		}
		else if (!fflags::validate(incoming, error))
		{
			editor::status = "Clipboard: " + error;
			editor::statusColor = colors::Red;
		}
		else
		{
			ClearActiveID();
			editor::text = incoming;
			editor::status = "FFlags imported from the clipboard";
			editor::statusColor = colors::Green;
		}
	}

	const _V2 boxPos = { 20, 64 };
	const _V2 boxSize = { popup->Size.x - 40, 212 };

	PushStyleVar(ImGuiStyleVar_FramePadding, { 12, 10 });
	PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
	const _V4 boxColor = RGBA(255, 255, 255, 12);
	PushStyleColor(ImGuiCol_FrameBg, boxColor);
	PushStyleColor(ImGuiCol_FrameBgHovered, boxColor);
	PushStyleColor(ImGuiCol_FrameBgActive, boxColor);
	PushStyleColor(ImGuiCol_Text, colors::Lwhite);

	SetCursorPos(boxPos);
	InputTextMultiline("##fflags", editor::text.data(), editor::text.capacity() + 1, boxSize, ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, editor::resize, &editor::text);

	if (editor::text.empty() && !IsItemActive())
		popup->DrawList->AddText(popup->Pos + boxPos + _V2(12, 10), _CO(colors::Info), editor::kHint);

	PopStyleColor(4);
	PopStyleVar(2);

	if (!editor::status.empty())
		draw::text(editor::status, { 20, 284 }, editor::statusColor);

	SetCursorPos({ 20, popup->Size.y - 20 - 40 });
	if (items::button("SAVE", { popup->Size.x - 40, 40 }))
		editor::saveAndClose();

	PopFont();

	EndPopup();
	PopStyleVar();
}

static void settingsWindow(ImGuiWindow* window)
{
	if (prefs::openRequest)
	{
		OpenPopup(prefs::kPopup);
		prefs::openRequest = false;
	}

	const _V2 size = { 520, 360 };
	SetNextWindowPos(window->Pos + (window->Size - size) / 2, ImGuiCond_Always);
	SetNextWindowSize(size, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	if (!BeginPopupModal(prefs::kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
	{
		PopStyleVar();
		return;
	}

	if (IsKeyPressed(ImGuiKey_Escape, false))
	{
		prefs::saveAndClose();
		EndPopup();
		PopStyleVar();
		return;
	}

	ImGuiWindow* popup = GetCurrentWindow();
	const float inner = popup->Size.x - 40;

	SetCursorPos({ 20, 20 });
	misc::renderMulticolorText({ "RAVE ", "SETTINGS" }, { f.montS[1], f.montS[1] }, { colors::Main, colors::Lwhite });

	PushFont(f.montM[0]);

	draw::text("BEHAVIOR ON START", { 20, 72 }, colors::Lwhite);

	struct Option { const char* label; settings::StartBehavior value; };
	const Option options[] = {
		{ "KEEP OPEN", settings::StartBehavior::KeepOpen },
		{ "MINIMIZE", settings::StartBehavior::Minimize },
		{ "HIDE", settings::StartBehavior::Hide },
	};
	const float gap = 10;
	const _V2 choiceSize = { std::floor((inner - gap * 2) / 3), 38 };
	for (int i = 0; i < 3; ++i)
	{
		SetCursorPos({ 20 + (choiceSize.x + gap) * i, 96 });
		if (items::choice(options[i].label, prefs::draft.behavior == options[i].value, choiceSize))
			prefs::draft.behavior = options[i].value;
	}

	draw::text("What Rave does when Roblox starts.", { 20, 144 }, colors::Gray);
	draw::text("HIDE brings Rave back when Roblox closes.", { 20, 164 }, colors::Gray);

	draw::text("ACCENT COLOR", { 20, 204 }, colors::Lwhite);

	int count = 0;
	const settings::Accent* accents = settings::accents(count);
	const _V2 swatchSize = { std::floor((inner - gap * (count - 1)) / count), 40 };
	for (int i = 0; i < count; ++i)
	{
		SetCursorPos({ 20 + (swatchSize.x + gap) * i, 230 });
		if (items::swatch(std::string("##accent_") + accents[i].name, accents[i].rgb, prefs::draft.accent == accents[i].rgb, swatchSize))
		{
			prefs::draft.accent = accents[i].rgb;
			applyAccent(accents[i].rgb);
		}
	}

	SetCursorPos({ 20, popup->Size.y - 20 - 40 });
	if (items::button("SAVE", { inner, 40 }))
		prefs::saveAndClose();

	PopFont();

	EndPopup();
	PopStyleVar();
}

void ui::Render() {
	static Tab tab = Tab::Home;

	logo::refresh(accentNow);

	misc::begin(RAVE_NAME);
	{
		ImGuiWindow* window = GetCurrentWindow();

		SetCursorPos({ window->Size.x - 20 - 14, 41 });
		if (items::closeButton("##close", { 14, 14 }))
			ui::RequestClose();

		if (tab == Tab::Home)
			homePage(window);
		else if (tab == Tab::Accounts)
			accountsPage(window);
		else if (tab == Tab::Info)
			infoPage(window);

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 16, 0 });
		{
			PushFont(f.montM[1]);
			SetCursorPos({ 20, 40 });

			if (items::tab("Home", tab == Tab::Home))
				tab = Tab::Home;

			SameLine();
			if (items::tab("Accounts", tab == Tab::Accounts))
				tab = Tab::Accounts;

			SameLine();
			if (items::tab("Info", tab == Tab::Info))
				tab = Tab::Info;

			if (images::logo)
				draw::image(images::logo, { (window->Size.x - images::logoSize.x) / 2, 48 - images::logoSize.y / 2 }, images::logoSize, colors::White, 0);

			PopFont();
		}
		PopStyleVar();

		fflagsEditor(window);
		settingsWindow(window);
		addAccountWindow(window);
	}
	ImGui::End();
}
