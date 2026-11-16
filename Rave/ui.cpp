#include "colors.hpp"
#include "core/accounts.hpp"
#include "core/discord.hpp"
#include "core/fflags.hpp"
#include "core/launcher.hpp"
#include "core/paths.hpp"
#include "core/protocol.hpp"
#include "core/settings.hpp"
#include "core/skins.hpp"
#include "core/sysinfo.hpp"
#include "core/system.hpp"
#include "core/updater.hpp"
#include <mutex>
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
	static int page = 0;
	static const char* kPopup = "SETTINGS";

	static void open()
	{
		draft = original = settings::get();
		page = 0;
		openRequest = true;
	}

	static void saveAndClose()
	{
		if (draft.websiteJoins != original.websiteJoins)
		{
			std::string error;
			bool ok = draft.websiteJoins ? protocol::enable(error) : protocol::disable(error);
			if (ok)
				items::sendLog(draft.websiteJoins ? "Play on roblox.com now starts Roblox through Rave" : "Website joins given back to the Roblox launcher", logType::success);
			else
			{
				items::sendLog("Website joins: " + error, logType::error);
				draft.websiteJoins = original.websiteJoins;
			}
		}

		bool changed = draft.behavior != original.behavior || draft.accent != original.accent ||
			draft.websiteJoins != original.websiteJoins || draft.discordStatus != original.discordStatus || draft.bestServer != original.bestServer;
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
	static bool presetsView = false;

	static const char* kPopup = "FFLAGS EDITOR";
	static const char* kHint = "{\n    \"FFlagDebugSkyGray\": \"True\"\n}";

	static void open()
	{
		text = fflags::load();
		original = text;
		status.clear();
		presetsView = false;
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
	skins::init([](const std::string& text, skins::Level level) {
		logType type = logType::info;
		switch (level)
		{
		case skins::Level::Success: type = logType::success; break;
		case skins::Level::Error: type = logType::error; break;
		case skins::Level::Warn: type = logType::warn; break;
		default: break;
		}
		items::sendLog(text, type);
	});
	launcher::startMonitor();
	if (settings::get().websiteJoins && !protocol::registered())
	{
		std::string error;
		bool before = paths::exists(paths::localData() + L"\\protocol.json");
		if (protocol::enable(error))
			items::sendLog(before ? "Rave took the website joins back from the Roblox launcher" : "Play on roblox.com now starts Roblox through Rave", logType::info);
	}
	discord::start();
	updater::start(window::dx::hwnd);
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
	static bool autoOpened = false;
	static std::string lastAutoCode;
	static const char* kPopup = "ADD ACCOUNT";
	static const wchar_t* kLoginUrl = L"https://www.roblox.com/crossdevicelogin/ConfirmCode";

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
		autoOpened = false;
		lastAutoCode.clear();
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

namespace skinui
{
	static bool openRequest = false;
	static size_t game = 0;
	static size_t target = 0;
	static std::string shown;
	static double lastRefresh = 0;
	static std::map<std::string, ID3D11ShaderResourceView*> textures;
	static std::vector<ID3D11ShaderResourceView*> retired;
	static const char* kPopup = "GAME SKINS";

	static void releaseAll()
	{
		for (auto& [key, view] : textures)
			if (view)
				retired.push_back(view);
		textures.clear();
	}

	static void releaseRetired()
	{
		for (ID3D11ShaderResourceView* view : retired)
			view->Release();
		retired.clear();
	}

	static void pickTarget(size_t g, size_t t)
	{
		if (g != game || t != target)
			releaseAll();
		game = g;
		target = t;
		shown = skins::selected(game, target);
	}

	static void open()
	{
		skins::refreshLocal();
		lastRefresh = GetTime();
		const auto& list = skins::games();
		game = game < list.size() ? game : 0;
		target = game < list.size() && target < list[game].targets.size() ? target : 0;
		shown = skins::selected(game, target);
		openRequest = true;
	}

	static void close()
	{
		releaseAll();
		CloseCurrentPopup();
	}

	static ID3D11ShaderResourceView* art(bool preview, const std::string& key, bool& failed)
	{
		failed = false;
		std::string id = std::string(preview ? "p|" : "t|") + std::to_string(game) + "|" + std::to_string(target) + "|" + key;
		auto it = textures.find(id);
		if (it != textures.end())
		{
			failed = it->second == nullptr;
			return it->second;
		}
		img::Rgba pixels;
		skins::Art state = preview ? skins::preview(game, target, key, pixels) : skins::thumb(game, target, key, pixels);
		if (state == skins::Art::Pending)
			return nullptr;
		ID3D11ShaderResourceView* view = nullptr;
		if (state == skins::Art::Ready)
		{
			gfx::Image image;
			image.pixels = std::move(pixels.px);
			image.w = pixels.w;
			image.h = pixels.h;
			view = gfx::createTexture(logo::device, image);
		}
		textures[id] = view;
		failed = view == nullptr;
		return view;
	}

	static std::string ellipsize(const std::string& text, float width)
	{
		if (CalcTextSize(text.c_str()).x <= width)
			return text;
		std::string cut = text;
		while (!cut.empty() && CalcTextSize((cut + "...").c_str()).x > width)
			cut.pop_back();
		while (!cut.empty() && cut.back() == ' ')
			cut.pop_back();
		return cut + "...";
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

			const float row = c->Size.x - 40;
			const float third = std::floor((row - 24) / 3);

			SetCursorPosX(20);
			if (items::button(launchLabel() + "###LAUNCH", { row, 45 }))
				launcher::launch();

			SetCursorPosX(20);
			if (items::button("FFLAGS EDITOR", { third, 45 }))
				editor::open();

			SameLine();
			if (items::button("SKINS", { third, 45 }))
				skinui::open();

			SameLine();
			if (items::button("SETTINGS", { row - third * 2 - 24, 45 }))
				prefs::open();

			PopFont();
		}
		PopStyleVar();
	}
	EndChild();
}

namespace removal
{
	uint64_t id = 0;
	double until = 0;

	void reset()
	{
		id = 0;
		until = 0;
	}
}

static void accountRow(const accounts::Account& account, bool selected, float width)
{
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
	bool confirming = removal::id == account.id && GetTime() < removal::until;
	if (confirming ? items::danger("SURE?##rm" + id, btn) : items::choice("REMOVE##rm" + id, false, btn))
	{
		if (confirming)
		{
			removal::reset();
			avatars::forget(account.id);
			accounts::remove(account.id);
		}
		else
		{
			removal::id = account.id;
			removal::until = GetTime() + 3.0;
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
		if (items::choice("USE ROBLOX'S OWN LOGIN", selected == 0, { inner - half - 12, 45 }, 0.f))
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

		PushFont(f.montM[1]);

		float y = 28;
		const float step = 30;
		auto rowAt = [&](const char* label, const std::string& value) {
			SetCursorPos({ 30, y });
			misc::row(label, value);
			y += step;
		};

		rowAt("Rave Version: ", "V" RAVE_VERSION_STR);
		rowAt("Latest Roblox: ", snap.latestKnown ? snap.latest : "Checking...");

		y += 5;
		draw::rect({ 30, y }, { c->Size.x - 60, 1 }, colors::withAlpha(colors::Lwhite, 0.10f));
		y += 16;

		rowAt("Windows: ", windowsName);
		rowAt("CPU: ", specs.cpu);
		rowAt("GPU: ", specs.gpu);
		rowAt("RAM: ", specs.ram);
		rowAt("Display: ", specs.display);
		rowAt("HardwareID: ", specs.hwid);

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

		if (!q.code.empty() && q.code != addacc::lastAutoCode)
		{
			addacc::lastAutoCode = q.code;
			SetClipboardText(q.code.c_str());
			if (!addacc::autoOpened)
			{
				addacc::autoOpened = true;
				sys::openUrl(addacc::kLoginUrl);
			}
		}

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
		y += wrapText(popup, { x, y }, colors::Lwhite, "Rave copied this code and opened the Quick Log In page in your browser. Enter or paste it there, or scan the QR code on your phone.", col);

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

static void presetRow(const fflags::Preset& preset, size_t index, float width)
{
	ImGuiWindow* w = GetCurrentWindow();
	ImDrawList* dl = w->DrawList;
	const _V2 pos = w->DC.CursorPos;
	const float height = 48;

	dl->AddRectFilled(pos, pos + _V2(width, height), _CO(_V4(RGBA(255, 255, 255, 8))), 8.f);

	dl->AddText(pos + _V2(12, 6), _CO(colors::Lwhite), preset.name);
	dl->AddText(pos + _V2(12, 26), _CO(colors::Gray), preset.info);

	const _V2 btn = { 76, 28 };
	SetCursorScreenPos({ pos.x + width - 10 - btn.x, pos.y + (height - btn.y) / 2 });
	if (items::choice("APPLY##preset" + std::to_string(index), false, btn))
	{
		std::string error;
		if (fflags::applyPreset(editor::text, preset, error))
		{
			editor::status = std::string(preset.name) + " pasted into the editor";
			editor::statusColor = colors::Green;
		}
		else
		{
			editor::status = error;
			editor::statusColor = colors::Red;
		}
	}

	SetCursorScreenPos(pos);
	Dummy({ width, height });
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
	const _V2 boxSize = { popup->Size.x - 40, 200 };
	const float saveY = popup->Size.y - 20 - 40;

	if (editor::presetsView)
	{
		SetCursorPos(boxPos);
		misc::child2("FFLAG_PRESETS", boxSize);
		{
			PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 6 });
			SetCursorPos({ 0, 0 });
			const float rowWidth = GetContentRegionAvail().x;
			const auto& list = fflags::presets();
			for (size_t i = 0; i < list.size(); ++i)
				presetRow(list[i], i, rowWidth);
			PopStyleVar();
		}
		EndChild();
	}
	else
	{
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
	}

	const float boxEnd = boxPos.y + boxSize.y;
	const float statusY = std::floor(boxEnd + (saveY - boxEnd - GetFontSize()) / 2);
	if (!editor::status.empty())
		draw::text(editor::status, { 20, statusY }, editor::statusColor);
	else if (editor::presetsView)
		draw::text("Presets only use flags that Roblox still accepts.", { 20, statusY }, colors::Gray);

	const float presetsWidth = 170;
	SetCursorPos({ 20, saveY });
	if (items::choice(editor::presetsView ? "BACK TO EDITOR" : "PRESETS", false, { presetsWidth, 40 }, 0.f))
	{
		ClearActiveID();
		editor::presetsView = !editor::presetsView;
		editor::status.clear();
	}

	SetCursorPos({ 20 + presetsWidth + 12, saveY });
	if (items::button("SAVE", { popup->Size.x - 40 - presetsWidth - 12, 40 }))
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

	{
		const char* pages[] = { "GENERAL", "ROBLOX" };
		float x = popup->Size.x - 20;
		for (int i = 1; i >= 0; --i)
		{
			const float wBtn = 110;
			x -= wBtn;
			SetCursorPos({ x, 16 });
			if (items::choice(std::string(pages[i]) + "##settingspage", prefs::page == i, { wBtn, 32 }))
				prefs::page = i;
			x -= 10;
		}
	}

	if (prefs::page == 0)
	{
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
	}
	else
	{
		struct Switch { const char* label; const char* note; bool* value; };
		const Switch switches[] = {
			{ "WEBSITE JOINS", "Play on roblox.com starts Roblox through Rave. These joins use the account signed in on the website, not the one chosen in Rave.", &prefs::draft.websiteJoins },
			{ "BEST SERVER", "Website joins go to the open server closest to you. Not used for friends or private servers.", &prefs::draft.bestServer },
			{ "DISCORD STATUS", "Shows the game you play, or that you are in Rave, in your Discord status while Discord is open on this PC.", &prefs::draft.discordStatus },
		};
		const _V2 toggle = { 58, 32 };
		const float noteW = inner - toggle.x * 2 - 6 - 20;
		const float bx = popup->Size.x - 20 - toggle.x * 2 - 6;
		float y = 70;
		for (int i = 0; i < 3; ++i)
		{
			draw::text(switches[i].label, { 20, y }, colors::Lwhite);
			wrapText(popup, { 20, y + 22 }, colors::Gray, switches[i].note, noteW);
			SetCursorPos({ bx, y + 6 });
			if (items::choice(std::string("ON##switch") + std::to_string(i), *switches[i].value, toggle))
				*switches[i].value = true;
			SetCursorPos({ bx + toggle.x + 6, y + 6 });
			if (items::choice(std::string("OFF##switch") + std::to_string(i), !*switches[i].value, toggle))
				*switches[i].value = false;
			y += 74;
		}
	}

	SetCursorPos({ 20, popup->Size.y - 20 - 40 });
	if (items::button("SAVE", { inner, 40 }))
		prefs::saveAndClose();

	PopFont();

	EndPopup();
	PopStyleVar();
}

static void weaponRow(size_t index, const skins::Target& t, bool active, bool changed, float width)
{
	ImGuiWindow* w = GetCurrentWindow();
	ImDrawList* dl = w->DrawList;
	const _V2 pos = w->DC.CursorPos;
	const _V2 size = { width, 36 };

	bool clicked = InvisibleButton(("##weapon" + std::to_string(index)).c_str(), size);
	bool hov = IsItemHovered();

	_V4 bg = active ? colors::withAlpha(colors::Main, 0.12f) : hov ? _V4(RGBA(255, 255, 255, 18)) : _V4(RGBA(255, 255, 255, 8));
	dl->AddRectFilled(pos, pos + size, _CO(bg), 6.f);
	if (active)
		dl->AddRect(pos, pos + size, _CO(colors::withAlpha(colors::Main, 0.55f)), 6.f, 0, 1.f);

	const float textMax = width - 12 - (changed ? 22 : 12);
	std::string label = skinui::ellipsize(t.name, textMax);
	dl->AddText(pos + _V2(12, (size.y - GetFontSize()) / 2), _CO(active ? colors::Lwhite : colors::Gray), label.c_str());
	if (changed)
		dl->AddCircleFilled(pos + _V2(width - 14, size.y / 2), 4.f, _CO(colors::Main), 16);

	if (clicked && !active)
		skinui::pickTarget(skinui::game, index);
}

static void skinRow(const skins::Skin& skin, size_t index, bool inUse, bool shownNow, float width)
{
	ImGuiWindow* w = GetCurrentWindow();
	ImDrawList* dl = w->DrawList;
	const _V2 pos = w->DC.CursorPos;
	const _V2 size = { width, 40 };

	bool clicked = InvisibleButton(("##skin" + std::to_string(index)).c_str(), size);
	bool hov = IsItemHovered();

	_V4 bg = inUse ? colors::withAlpha(colors::Main, 0.12f) : shownNow ? _V4(RGBA(255, 255, 255, 22)) : hov ? _V4(RGBA(255, 255, 255, 16)) : _V4(RGBA(255, 255, 255, 8));
	dl->AddRectFilled(pos, pos + size, _CO(bg), 6.f);
	if (inUse)
		dl->AddRect(pos, pos + size, _CO(colors::withAlpha(colors::Main, 0.55f)), 6.f, 0, 1.f);
	else if (shownNow)
		dl->AddRect(pos, pos + size, _CO(_V4(RGBA(228, 228, 228, 70))), 6.f, 0, 1.f);

	const _V2 thumbPos = pos + _V2(8, 6);
	const _V2 thumbSize = { 28, 28 };
	dl->AddRectFilled(thumbPos, thumbPos + thumbSize, _CO(_V4(RGBA(255, 255, 255, 14))), 4.f);
	bool failed = false;
	if (ID3D11ShaderResourceView* tex = skinui::art(false, skin.key, failed))
		dl->AddImageRounded((ImTextureID)tex, thumbPos, thumbPos + thumbSize, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE, 4.f);

	std::string label = skinui::ellipsize(skin.name, width - 56 - 10);
	dl->AddText(pos + _V2(46, (size.y - GetFontSize()) / 2), _CO(inUse || shownNow ? colors::Lwhite : colors::Gray), label.c_str());

	if (clicked)
		skinui::shown = skin.key;
}

static void skinsWindow(ImGuiWindow* window)
{
	skinui::releaseRetired();

	if (skinui::openRequest)
	{
		OpenPopup(skinui::kPopup);
		skinui::openRequest = false;
	}

	const _V2 size = { 620, 410 };
	SetNextWindowPos(window->Pos + (window->Size - size) / 2, ImGuiCond_Always);
	SetNextWindowSize(size, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	if (!BeginPopupModal(skinui::kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
	{
		PopStyleVar();
		return;
	}

	if (IsKeyPressed(ImGuiKey_Escape, false))
	{
		skinui::close();
		EndPopup();
		PopStyleVar();
		return;
	}

	if (GetTime() - skinui::lastRefresh > 2.0)
	{
		skins::refreshLocal();
		skinui::lastRefresh = GetTime();
	}

	ImGuiWindow* popup = GetCurrentWindow();
	const float inner = popup->Size.x - 40;
	const auto& games = skins::games();

	SetCursorPos({ 20, 20 });
	misc::renderMulticolorText({ "GAME ", "SKINS" }, { f.montS[1], f.montS[1] }, { colors::Main, colors::Lwhite });

	PushFont(f.montM[0]);

	if (games.empty())
	{
		draw::text("No games found.", { 20, 80 }, colors::Gray);
		SetCursorPos({ 20, popup->Size.y - 20 - 40 });
		if (items::button("CLOSE", { inner, 40 }))
			skinui::close();
		PopFont();
		EndPopup();
		PopStyleVar();
		return;
	}

	{
		const float gap = 10;
		float x = popup->Size.x - 20;
		for (size_t i = games.size(); i-- > 0;)
		{
			std::string label = games[i].name;
			for (char& ch : label)
				ch = (char)toupper((unsigned char)ch);
			float wBtn = std::max(120.f, CalcTextSize(label.c_str()).x + 32);
			x -= wBtn;
			SetCursorPos({ x, 16 });
			if (items::choice(label + "##game" + std::to_string(i), skinui::game == i, { wBtn, 32 }) && skinui::game != i)
				skinui::pickTarget(i, 0);
			x -= gap;
		}
	}

	const skins::Game& g = games[skinui::game];
	if (skinui::target >= g.targets.size())
		skinui::pickTarget(skinui::game, 0);

	const float top = 64;
	const float listH = 254;
	const float colW = 180;
	const float gap = 12;
	const float previewX = 20 + colW * 2 + gap * 2;
	const float previewW = popup->Size.x - 20 - previewX;

	SetCursorPos({ 20, top });
	misc::child2("SKIN_WEAPONS", { colW, listH });
	{
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 6 });
		SetCursorPos({ 0, 0 });
		for (size_t i = 0; i < g.targets.size(); ++i)
			weaponRow(i, g.targets[i], i == skinui::target, !skins::selected(skinui::game, i).empty(), colW);
		PopStyleVar();
	}
	EndChild();

	const std::string inUse = skins::selected(skinui::game, skinui::target);
	std::vector<skins::Skin> list = skins::list(skinui::game, skinui::target);
	bool shownExists = false;
	for (const auto& s : list)
		if (s.key == skinui::shown)
			shownExists = true;
	if (!shownExists)
		skinui::shown = inUse;

	SetCursorPos({ 20 + colW + gap, top });
	misc::child2("SKIN_LIST", { colW, listH });
	{
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 6 });
		SetCursorPos({ 0, 0 });
		for (size_t i = 0; i < list.size(); ++i)
			skinRow(list[i], i, list[i].key == inUse, list[i].key == skinui::shown, colW);
		PopStyleVar();
	}
	EndChild();

	const skins::Skin* current = nullptr;
	for (const auto& s : list)
		if (s.key == skinui::shown)
			current = &s;

	const _V2 imgPos = popup->Pos + _V2(previewX, top);
	const _V2 imgSize = { previewW, std::floor(previewW * 264.f / 432.f) };
	popup->DrawList->AddRectFilled(imgPos, imgPos + imgSize, _CO(_V4(RGBA(255, 255, 255, 12))), 8.f);
	if (current)
	{
		bool failed = false;
		ID3D11ShaderResourceView* tex = skinui::art(true, current->key, failed);
		if (tex)
			popup->DrawList->AddImageRounded((ImTextureID)tex, imgPos, imgPos + imgSize, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE, 8.f);
		else
		{
			const char* note = failed ? "No preview" : "...";
			_V2 ns = _C(note);
			popup->DrawList->AddText(imgPos + (imgSize - ns) / 2, _CO(colors::Gray), note);
		}

		float y = top + imgSize.y + 14;
		PushFont(f.montM[1]);
		draw::text(skinui::ellipsize(current->name, previewW), { previewX, y }, colors::Lwhite);
		PopFont();
		y += 26;
		draw::text(skinui::ellipsize(current->source, previewW), { previewX, y }, colors::Gray);
		y += 22;
		bool active = current->key == inUse;
		draw::text(active ? "In use" : "Not in use", { previewX, y }, active ? colors::Main : colors::Info);

		SetCursorPos({ previewX, top + listH - 40 });
		if (active)
			items::choice("IN USE##useskin", true, { previewW, 40 });
		else if (items::button((current->kind == skins::Kind::Original ? std::string("USE ORIGINAL") : std::string("USE THIS SKIN")) + "##useskin", { previewW, 40 }))
			skins::select(skinui::game, skinui::target, current->key);
	}

	std::string status = skins::status();
	if (status.empty())
		status = "Skins replace textures in your Roblox cache. Only you can see them.";
	draw::text(skinui::ellipsize(status, inner), { 20, top + listH + 12 }, colors::Gray);

	const float folderW = 170;
	SetCursorPos({ 20, popup->Size.y - 20 - 40 });
	if (items::choice("SKINS FOLDER", false, { folderW, 40 }, 0.f))
		skins::openFolder(skinui::game);

	SetCursorPos({ 20 + folderW + 12, popup->Size.y - 20 - 40 });
	if (items::button("CLOSE", { inner - folderW - 12, 40 }))
		skinui::close();

	PopFont();

	EndPopup();
	PopStyleVar();
}

void ui::Render() {
	static Tab tab = Tab::Home;
	const Tab shownTab = tab;

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

			if (tab != shownTab)
				removal::reset();

			if (images::logo)
				draw::image(images::logo, { (window->Size.x - images::logoSize.x) / 2, 48 - images::logoSize.y / 2 }, images::logoSize, colors::White, 0);

			PopFont();
		}
		PopStyleVar();

		fflagsEditor(window);
		settingsWindow(window);
		addAccountWindow(window);
		skinsWindow(window);
	}
	ImGui::End();
}
