#include <sstream>

#include "utils/file.h"
#include "utils/logger.h"
#include "utils/string.h"
#include "webserver.h"

namespace
{
constexpr auto kUaBlockReloadInterval = std::chrono::seconds(5);
constexpr const char *kDefaultUaBlockKeywordsPath = "base/ua_block_keywords.list";

static string_array parse_ua_block_keywords(const std::string &content)
{
    string_array keywords;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line))
    {
        auto comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.erase(comment);
        }
        line = trimWhitespace(line, true, true);
        if (line.empty())
        {
            continue;
        }
        keywords.emplace_back(toLower(line));
    }
    return keywords;
}
} // namespace

void WebServer::reload_ua_block_keywords_if_needed_locked()
{
    auto now = std::chrono::steady_clock::now();
    if (now < ua_block_keywords_next_reload)
    {
        return;
    }
    ua_block_keywords_next_reload = now + kUaBlockReloadInterval;

    std::string content;
    std::string loaded_from;
    if (fileExist(ua_block_keywords_path))
    {
        loaded_from = ua_block_keywords_path;
        content = fileGet(ua_block_keywords_path);
    }
    else if (ua_block_keywords_path != kDefaultUaBlockKeywordsPath && fileExist(kDefaultUaBlockKeywordsPath))
    {
        loaded_from = kDefaultUaBlockKeywordsPath;
        content = fileGet(kDefaultUaBlockKeywordsPath);
    }

    if (loaded_from.empty())
    {
        if (!ua_block_keywords_missing_warned)
        {
            writeLog(0, "UA blocker keyword file not found, blocker has no active patterns. Checked '" + ua_block_keywords_path + "' and '" + kDefaultUaBlockKeywordsPath + "'.", LOG_LEVEL_WARNING);
            ua_block_keywords_missing_warned = true;
        }
        ua_block_keywords.clear();
        ua_block_keywords_loaded_from.clear();
        return;
    }

    auto new_keywords = parse_ua_block_keywords(content);
    ua_block_keywords_missing_warned = false;
    if (loaded_from != ua_block_keywords_loaded_from || new_keywords != ua_block_keywords)
    {
        writeLog(0, "Loaded " + std::to_string(new_keywords.size()) + " UA blocker keywords from '" + loaded_from + "'.", LOG_LEVEL_INFO);
    }
    ua_block_keywords_loaded_from = loaded_from;
    ua_block_keywords.swap(new_keywords);
}

bool WebServer::is_user_agent_blocked(const std::string &user_agent, std::string *matched_keyword)
{
    if (!ua_block_enabled || user_agent.empty())
    {
        return false;
    }

    auto user_agent_lower = toLower(user_agent);
    std::lock_guard<std::mutex> lock(ua_block_keywords_mutex);
    reload_ua_block_keywords_if_needed_locked();
    for (const auto &keyword : ua_block_keywords)
    {
        if (user_agent_lower.find(keyword) == std::string::npos)
        {
            continue;
        }
        if (matched_keyword != nullptr)
        {
            *matched_keyword = keyword;
        }
        return true;
    }
    return false;
}
