#include "colors.hpp"
#include "core/fflags.hpp"
#include "core/launcher.hpp"
#include "core/news.hpp"
#include "core/settings.hpp"
#include "core/system.hpp"

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

	launcher::setLogger([](const std::string& text, launcher::LogLevel level) {
		logType type = logType::info;
		switch (level)
		{
		case launcher::LogLevel::Success: type = logType::success; break;
		case launcher::LogLevel::Error: type = logType::error; break;
		case launcher::LogLevel::Warn: type = logType::warn; break;
		default: break;
		}
		items::sendLog(text, type);
	});
	launcher::startMonitor();
	news::fetchAsync();
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

static void homePage(ImGuiWindow* window)
{
	SetCursorPos({ 20, 100 });
	misc::child("NEWS", { window->Size.x - 40, 347 });
	{
		ImGuiWindow* c = GetCurrentWindow();

		PushFont(f.montS[1]);
		draw::text("WHAT'S NEW?", { 22, 22 }, colors::Lwhite);
		PopFont();

		news::State state = news::state();
		if (state == news::State::Ready)
		{
			news::Item item = news::item();
			std::string title = item.title;
			if (!item.date.empty())
				title += " -- " + item.date;

			SetCursorPos({ 22, 77 });
			misc::announcement(title, item.body);
			Dummy({ 1, 22 });
		}
		else if (state == news::State::None)
		{
			PushFont(f.montM[1]);
			_V2 size = _C("no news");
			SetCursorPos({ (c->Size.x - size.x) / 2, (c->Size.y - size.y) / 2 });
			draw::text("no news", {}, colors::Gray);
			PopFont();
		}
	}
	EndChild();
}

static void launchPage(ImGuiWindow* window)
{
	SetCursorPos({ 20, 100 });
	misc::child("LAUNCH", { window->Size.x - 40, 347 });
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

static void statePage(ImGuiWindow* window)
{
	SetCursorPos({ 20, 100 });
	misc::child("STATE", { window->Size.x - 40, 347 });
	{
		launcher::Snapshot snap = launcher::snapshot();

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 13 });
		{
			PushFont(f.montM[1]);

			SetCursorPos({ 30, 30 });
			misc::row("Rave Version: ", "V" RAVE_VERSION_STR);

			SetCursorPosX(30);
			misc::row("Windows: ", windowsName);

			SetCursorPosX(30);
			misc::row("Latest Roblox: ", snap.latestKnown ? snap.latest : "Checking...");

			PopFont();
		}
		PopStyleVar();
	}
	EndChild();
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
	static int tab = tabs::home;

	logo::refresh(accentNow);

	misc::begin(RAVE_NAME);
	{
		ImGuiWindow* window = GetCurrentWindow();

		PushFont(f.icons[0]);
		SetCursorPos({ window->Size.x - 20 - _C("a").x, 38});
		if (items::iconButton("a"))
			ui::RequestClose();
		PopFont();

		if (tab == tabs::home)
			homePage(window);
		else if (tab == tabs::launch)
			launchPage(window);
		else if (tab == tabs::state)
			statePage(window);

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 16, 0 });
		{
			PushFont(f.montM[1]);
			SetCursorPos({ 20, 40 });

			if (items::tab("Home", tab == tabs::home))
				tab = tabs::home;

			SameLine();
			if (items::tab("Launch", tab == tabs::launch))
				tab = tabs::launch;

			SameLine();
			if (items::tab("State", tab == tabs::state))
				tab = tabs::state;

			if (images::logo)
				draw::image(images::logo, { (window->Size.x - images::logoSize.x) / 2, 48 - images::logoSize.y / 2 }, images::logoSize, colors::White, 0);

			PopFont();
		}
		PopStyleVar();

		fflagsEditor(window);
		settingsWindow(window);
	}
	ImGui::End();
}
