#include "colors.hpp"
#include "core/launcher.hpp"
#include "core/news.hpp"
#include "core/system.hpp"

static std::string windowsName;

void ui::BeforeLoop(ID3D11Device* dvc) {
	f.RenderFonts();
	misc::set_blur(window::dx::hwnd);

	HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(3), RT_RCDATA);
	HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
	if (mem)
		images::logo = gfx::loadImage(dvc, LockResource(mem), SizeofResource(nullptr, res), 30, &images::logoSize);

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
	case launcher::Task::Closing: return "CLOSING...";
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
				PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 8 });
				items::renderLogs();
				PopStyleVar();
			}
			EndChild();

			SetCursorPosX(20);
			if (items::button(launchLabel() + "###LAUNCH", { c->Size.x - GetCursorPosX() * 2 , 45 }))
				launcher::launch(false);

			SetCursorPosX(20);
			if (items::button("REINSTALL ROBLOX", { GetItemRectSize().x / 2 - 6, 45 }))
				launcher::launch(true);

			SameLine();
			if (items::button("CLOSE ROBLOX", { GetItemRectSize().x, 45 }))
				launcher::closeRoblox();

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
		bool upToDate = snap.latestKnown && !snap.installed.empty() && snap.installed == snap.latest;

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 13 });
		{
			PushFont(f.montM[1]);

			SetCursorPos({ 30, 30 });
			misc::row("Rave Version: ", "V" RAVE_VERSION_STR, rowStatus::info);

			SetCursorPosX(30);
			misc::row("Windows: ", windowsName, rowStatus::info);

			SetCursorPosX(30);
			misc::separator();

			SetCursorPosX(30);
			misc::row("Administrator: ", app::elevated ? "Granted" : "Missing", app::elevated ? rowStatus::good : rowStatus::bad);

			SetCursorPosX(30);
			misc::row("Roblox Installed: ", snap.installed.empty() ? "Not installed" : snap.installed, snap.installed.empty() ? rowStatus::bad : rowStatus::good);

			SetCursorPosX(30);
			misc::row("Latest Roblox: ", snap.latestKnown ? snap.latest : "Checking...", rowStatus::accent);

			SetCursorPosX(30);
			misc::row("Up To Date: ", !snap.latestKnown ? "Checking..." : upToDate ? "Yes" : "No", !snap.latestKnown ? rowStatus::accent : upToDate ? rowStatus::good : rowStatus::bad);

			SetCursorPosX(30);
			misc::row("Roblox Running: ", snap.running ? "Yes" : "No", snap.running ? rowStatus::good : rowStatus::accent);

			SetCursorPosX(30);
			misc::row("Anticheat: ", "Coming with V0.2", rowStatus::accent);

			PopFont();
		}
		PopStyleVar();
	}
	EndChild();
}

void ui::Render() {
	static int tab = tabs::home;

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
	}
	ImGui::End();
}
