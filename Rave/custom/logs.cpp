#include "colors.hpp"
#include <mutex>

struct __Logs__
{
	std::string content;
	_V4 color;
};

static std::mutex logsLock;
static std::vector<__Logs__> logs;

static std::map<logType, _V4> logColors =
{
	{logType::success, colors::Green},
	{logType::error, colors::Red},
	{logType::warn, colors::Accent},
	{logType::info, colors::Info}
};

auto items::sendLog(std::string log, logType type) noexcept -> void
{
	std::lock_guard<std::mutex> guard(logsLock);
	logs.push_back({ "[" + getLocalTime() + "] - " + log, getColor(logColors, type) });
	while (logs.size() > 8)
		logs.erase(logs.begin());
}

auto items::renderLogs() noexcept -> void
{
	std::lock_guard<std::mutex> guard(logsLock);

	for (auto& it : logs)
		draw::text(it.content, {}, it.color);
}
