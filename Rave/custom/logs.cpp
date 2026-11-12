#include "colors.hpp"
#include <mutex>

struct __Logs__
{
	std::string content;
	logType type;
};

static std::mutex logsLock;
static std::vector<__Logs__> logs;

static _V4 logColor(logType type)
{
	switch (type)
	{
	case logType::success: return colors::Green;
	case logType::error: return colors::Red;
	case logType::warn: return colors::Main;
	default: return colors::Info;
	}
}

auto items::sendLog(std::string log, logType type) noexcept -> void
{
	std::lock_guard<std::mutex> guard(logsLock);
	logs.push_back({ "[" + getLocalTime() + "] - " + log, type });
	while (logs.size() > 8)
		logs.erase(logs.begin());
}

auto items::renderLogs() noexcept -> void
{
	std::lock_guard<std::mutex> guard(logsLock);

	for (auto& it : logs)
		draw::text(it.content, {}, logColor(it.type));
}
