#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
//#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>

#ifndef NO_WEBGET
#include <curl/curl.h>
#include "version.h"
#endif

#include "handler/settings.h"
#include "server/socket.h"
#include "utils/base64/base64.h"
#include "utils/defer.h"
#include "utils/file_extra.h"
#include "utils/lock.h"
#include "utils/logger.h"
#include "utils/network.h"
#include "utils/urlencode.h"
#include "webget.h"

#ifdef _WIN32
#ifndef _stat
#define _stat stat
#endif // _stat
#endif // _WIN32

/*
using guarded_mutex = std::lock_guard<std::mutex>;
std::mutex cache_rw_lock;
*/

RWLock cache_rw_lock;

static bool is_supported_remote_url(const std::string &url)
{
    return startsWith(url, "http://") || startsWith(url, "https://");
}

static bool extract_url_host(const std::string &url, std::string &host)
{
    const std::string::size_type scheme_pos = url.find("://");
    if(scheme_pos == std::string::npos)
        return false;
    const std::string::size_type authority_start = scheme_pos + 3;
    const std::string::size_type authority_end = url.find_first_of("/?#", authority_start);
    std::string authority = authority_end == std::string::npos ? url.substr(authority_start) : url.substr(authority_start, authority_end - authority_start);
    if(authority.empty())
        return false;
    const std::string::size_type at_pos = authority.rfind('@');
    if(at_pos != std::string::npos)
        authority.erase(0, at_pos + 1);
    if(authority.empty())
        return false;
    if(authority.front() == '[')
    {
        const std::string::size_type close_pos = authority.find(']');
        if(close_pos == std::string::npos)
            return false;
        host = authority.substr(1, close_pos - 1);
        return !host.empty();
    }
    const std::string::size_type colon_pos = authority.rfind(':');
    if(colon_pos != std::string::npos && authority.find(':') == colon_pos)
        host = authority.substr(0, colon_pos);
    else
        host = authority;
    return !host.empty();
}

static bool is_private_or_loopback_ipv4(const in_addr &addr)
{
    const uint32_t value = ntohl(addr.s_addr);
    if((value & 0xFF000000u) == 0x0A000000u) // 10.0.0.0/8
        return true;
    if((value & 0xFF000000u) == 0x7F000000u) // 127.0.0.0/8
        return true;
    if((value & 0xFFF00000u) == 0xAC100000u) // 172.16.0.0/12
        return true;
    if((value & 0xFFFF0000u) == 0xC0A80000u) // 192.168.0.0/16
        return true;
    if((value & 0xFFFF0000u) == 0xA9FE0000u) // 169.254.0.0/16
        return true;
    if((value & 0xFFC00000u) == 0x64400000u) // 100.64.0.0/10
        return true;
    if((value & 0xFF000000u) == 0x00000000u) // 0.0.0.0/8
        return true;
    return false;
}

static bool is_private_or_loopback_ipv6(const in6_addr &addr)
{
    static const in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
    static const in6_addr any = IN6ADDR_ANY_INIT;
    if(std::memcmp(&addr, &loopback, sizeof(in6_addr)) == 0)
        return true;
    if(std::memcmp(&addr, &any, sizeof(in6_addr)) == 0)
        return true;
    if((addr.s6_addr[0] & 0xFEu) == 0xFCu) // fc00::/7
        return true;
    if(addr.s6_addr[0] == 0xFEu && (addr.s6_addr[1] & 0xC0u) == 0x80u) // fe80::/10
        return true;
#ifdef IN6_IS_ADDR_V4MAPPED
    if(IN6_IS_ADDR_V4MAPPED(&addr))
    {
        in_addr mapped {};
        std::memcpy(&mapped, addr.s6_addr + 12, sizeof(mapped));
        return is_private_or_loopback_ipv4(mapped);
    }
#endif
    return false;
}

static bool should_block_private_address_request(const std::string &url)
{
    if(!global.blockPrivateAddressRequests || !is_supported_remote_url(url))
        return false;
    std::string host;
    if(!extract_url_host(url, host))
        return true;
    const std::string host_lower = toLower(host);
    if(host_lower == "localhost" || endsWith(host_lower, ".localhost"))
        return true;
    if(isIPv4(host))
    {
        in_addr addr {};
        if(inet_pton(AF_INET, host.c_str(), &addr) == 1)
            return is_private_or_loopback_ipv4(addr);
        return true;
    }
    if(isIPv6(host))
    {
        in6_addr addr6 {};
        if(inet_pton(AF_INET6, host.c_str(), &addr6) == 1)
            return is_private_or_loopback_ipv6(addr6);
        return true;
    }
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *resolved = nullptr;
    if(getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || resolved == nullptr)
        return false;
    defer(freeaddrinfo(resolved);)
    for(addrinfo *entry = resolved; entry != nullptr; entry = entry->ai_next)
    {
        if(entry->ai_family == AF_INET)
        {
            const auto *sock = reinterpret_cast<const sockaddr_in *>(entry->ai_addr);
            if(sock && is_private_or_loopback_ipv4(sock->sin_addr))
                return true;
        }
        else if(entry->ai_family == AF_INET6)
        {
            const auto *sock6 = reinterpret_cast<const sockaddr_in6 *>(entry->ai_addr);
            if(sock6 && is_private_or_loopback_ipv6(sock6->sin6_addr))
                return true;
        }
    }
    return false;
}

#ifndef NO_WEBGET
//std::string user_agent_str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/74.0.3729.169 Safari/537.36";
static auto user_agent_str = "subconverter/" VERSION " cURL/" LIBCURL_VERSION;

struct curl_progress_data
{
    long size_limit = 0L;
};

static inline void curl_init()
{
    static bool init = false;
    if(!init)
    {
        curl_global_init(CURL_GLOBAL_ALL);
        init = true;
    }
}

static int writer(char *data, size_t size, size_t nmemb, std::string *writerData)
{
    if(writerData == nullptr)
        return 0;

    writerData->append(data, size*nmemb);

    return static_cast<int>(size * nmemb);
}

static int dummy_writer(char *, size_t size, size_t nmemb, void *)
{
    /// dummy writer, do not save anything
    return static_cast<int>(size * nmemb);
}

//static int size_checker(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
static int size_checker(void *clientp, curl_off_t, curl_off_t dlnow, curl_off_t, curl_off_t)
{
    if(clientp)
    {
        auto *data = reinterpret_cast<curl_progress_data*>(clientp);
        if(data->size_limit)
        {
            if(dlnow > data->size_limit)
                return 1;
        }
    }
    return 0;
}

static int logger(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
    (void)handle;
    (void)userptr;
    std::string prefix;
    switch(type)
    {
    case CURLINFO_TEXT:
        prefix = "CURL_INFO: ";
        break;
    case CURLINFO_HEADER_IN:
        prefix = "CURL_HEADER: < ";
        break;
    case CURLINFO_HEADER_OUT:
        prefix = "CURL_HEADER: > ";
        break;
    case CURLINFO_DATA_IN:
    case CURLINFO_DATA_OUT:
    default:
        return 0;
    }
    std::string content(data, size);
    if(content.find("\r\n") != std::string::npos)
    {
        string_array lines = split(content, "\r\n");
        for(auto &x : lines)
        {
            std::string log_content = prefix;
            log_content += x;
            writeLog(0, log_content, LOG_LEVEL_VERBOSE);
        }
    }
    else
    {
        std::string log_content = prefix;
        log_content += trimWhitespace(content);
        writeLog(0, log_content, LOG_LEVEL_VERBOSE);
    }
    return 0;
}

static inline void curl_set_common_options(CURL *curl_handle, const char *url, curl_progress_data *data)
{
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, global.logLevel == LOG_LEVEL_VERBOSE ? 1L : 0L);
    curl_easy_setopt(curl_handle, CURLOPT_DEBUGFUNCTION, logger);
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 20L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, global.verifyOutboundTls ? 1L : 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, global.verifyOutboundTls ? 2L : 0L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    if(data)
    {
        if(data->size_limit)
            curl_easy_setopt(curl_handle, CURLOPT_MAXFILESIZE, data->size_limit);
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, size_checker);
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, data);
    }
}

//static std::string curlGet(const std::string &url, const std::string &proxy, std::string &response_headers, CURLcode &return_code, const string_map &request_headers)
static int curlGet(const FetchArgument &argument, FetchResult &result)
{
    CURL *curl_handle;
    std::string *data = result.content, new_url = argument.url;
    curl_slist *header_list = nullptr;
    defer(curl_slist_free_all(header_list);)
    long retVal;

    curl_init();

    curl_handle = curl_easy_init();
    if(!argument.proxy.empty())
    {
        if(startsWith(argument.proxy, "cors:"))
        {
            header_list = curl_slist_append(header_list, "X-Requested-With: subconverter " VERSION);
            new_url = argument.proxy.substr(5) + argument.url;
        }
        else
            curl_easy_setopt(curl_handle, CURLOPT_PROXY, argument.proxy.data());
    }
    curl_progress_data limit;
    limit.size_limit = global.maxAllowedDownloadSize;
    curl_set_common_options(curl_handle, new_url.data(), &limit);
    header_list = curl_slist_append(header_list, "Content-Type: application/json;charset=utf-8");
    if(argument.request_headers)
    {
        for(auto &x : *argument.request_headers)
        {
            auto header = x.first + ": " + x.second;
            header_list = curl_slist_append(header_list, header.data());
        }
        if(!argument.request_headers->contains("User-Agent"))
            curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, user_agent_str);
    }
    if(!argument.omit_subconverter_headers)
    {
        header_list = curl_slist_append(header_list, "SubConverter-Request: 1");
        header_list = curl_slist_append(header_list, "SubConverter-Version: " VERSION);
    }
    if(header_list)
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, header_list);

    if(result.content)
    {
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writer);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, result.content);
    }
    else
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, dummy_writer);
    if(result.response_headers)
    {
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, writer);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, result.response_headers);
    }
    else
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, dummy_writer);

    if(argument.cookies)
    {
        string_array cookies = split(*argument.cookies, "\r\n");
        for(auto &x : cookies)
            curl_easy_setopt(curl_handle, CURLOPT_COOKIELIST, x.c_str());
    }

    switch(argument.method)
    {
    case HTTP_POST:
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
        if(argument.post_data)
        {
            curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, argument.post_data->data());
            curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, argument.post_data->size());
        }
        break;
    case HTTP_PATCH:
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PATCH");
        if(argument.post_data)
        {
            curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, argument.post_data->data());
            curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, argument.post_data->size());
        }
        break;
    case HTTP_HEAD:
        curl_easy_setopt(curl_handle, CURLOPT_NOBODY, 1L);
        break;
    case HTTP_GET:
        break;
    }

    unsigned int fail_count = 0, max_fails = 1;
    while(true)
    {
        retVal = curl_easy_perform(curl_handle);
        if(retVal == CURLE_OK || max_fails <= fail_count || global.APIMode)
            break;
        else
            fail_count++;
    }

    long code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_HTTP_CODE, &code);
    *result.status_code = code;

    if(result.cookies)
    {
        curl_slist *cookies = nullptr;
        curl_easy_getinfo(curl_handle, CURLINFO_COOKIELIST, &cookies);
        if(cookies)
        {
            auto each = cookies;
            while(each)
            {
                result.cookies->append(each->data);
                *result.cookies += "\r\n";
                each = each->next;
            }
        }
        curl_slist_free_all(cookies);
    }

    curl_easy_cleanup(curl_handle);

    if(data && !argument.keep_resp_on_fail)
    {
        if(retVal != CURLE_OK || *result.status_code != 200)
            data->clear();
        data->shrink_to_fit();
    }

    return *result.status_code;
}
#else
static int curlGet(const FetchArgument &argument, FetchResult &result)
{
    (void)argument;
    if(result.status_code)
        *result.status_code = 0;
    if(result.content)
        result.content->clear();
    if(result.response_headers)
        result.response_headers->clear();
    if(result.cookies)
        result.cookies->clear();
    return result.status_code ? *result.status_code : 0;
}
#endif // NO_WEBGET

// data:[<mediatype>][;base64],<data>
static std::string dataGet(const std::string &url)
{
    if (!startsWith(url, "data:"))
        return "";
    std::string::size_type comma = url.find(',');
    if (comma == std::string::npos || comma == url.size() - 1)
        return "";

    std::string data = urlDecode(url.substr(comma + 1));
    if (endsWith(url.substr(0, comma), ";base64")) {
        return urlSafeBase64Decode(data);
    } else {
        return data;
    }
}

std::string buildSocks5ProxyString(const std::string &addr, int port, const std::string &username, const std::string &password)
{
    std::string authstr = username.size() && password.size() ? username + ":" + password + "@" : "";
    std::string proxystr = "socks5://" + authstr + addr + ":" + std::to_string(port);
    return proxystr;
}

std::string webGet(const std::string &url, const std::string &proxy, unsigned int cache_ttl,
                   std::string *response_headers, string_icase_map *request_headers,
                   bool omit_subconverter_headers)
{
    int return_code = 0;
    std::string content;

    FetchArgument argument {HTTP_GET, url, proxy, nullptr, request_headers, nullptr, cache_ttl, false,
                            omit_subconverter_headers};
    FetchResult fetch_res {&return_code, &content, response_headers, nullptr};

    if (startsWith(url, "data:"))
        return dataGet(url);
    if(!is_supported_remote_url(url))
    {
        writeLog(0, "Unsupported URL scheme: '" + url + "'.", LOG_LEVEL_WARNING);
        return "";
    }
    // cache system
    if(cache_ttl > 0)
    {
        md("cache");
        const std::string url_md5 = getMD5(url);
        const std::string path = "cache/" + url_md5, path_header = path + "_header";
        struct stat result {};
        if(stat(path.data(), &result) == 0) // cache exist
        {
            time_t mtime = result.st_mtime, now = time(nullptr); // get cache modified time and current time
            if(difftime(now, mtime) <= cache_ttl) // within TTL
            {
                writeLog(0, "CACHE HIT: '" + url + "', using local cache.");
                //guarded_mutex guard(cache_rw_lock);
                cache_rw_lock.readLock();
                defer(cache_rw_lock.readUnlock();)
                if(response_headers)
                    *response_headers = fileGet(path_header, true);
                return fileGet(path, true);
            }
            writeLog(0, "CACHE MISS: '" + url + "', TTL timeout, creating new cache."); // out of TTL
        }
        else
            writeLog(0, "CACHE NOT EXIST: '" + url + "', creating new cache.");
        //content = curlGet(url, proxy, response_headers, return_code); // try to fetch data
        curlGet(argument, fetch_res);
        if(return_code == 200) // success, save new cache
        {
            //guarded_mutex guard(cache_rw_lock);
            cache_rw_lock.writeLock();
            defer(cache_rw_lock.writeUnlock();)
            fileWrite(path, content, true);
            if(response_headers)
                fileWrite(path_header, *response_headers, true);
        }
        else
        {
            if(fileExist(path) && global.serveCacheOnFetchFail) // failed, check if cache exist
            {
                writeLog(0, "Fetch failed. Serving cached content."); // cache exist, serving cache
                //guarded_mutex guard(cache_rw_lock);
                cache_rw_lock.readLock();
                defer(cache_rw_lock.readUnlock();)
                content = fileGet(path, true);
                if(response_headers)
                    *response_headers = fileGet(path_header, true);
            }
            else
                writeLog(0, "Fetch failed. No local cache available."); // cache not exist or not allow to serve cache, serving nothing
        }
        return content;
    }
    //return curlGet(url, proxy, response_headers, return_code);
    curlGet(argument, fetch_res);
    return content;
}

void flushCache()
{
    //guarded_mutex guard(cache_rw_lock);
    cache_rw_lock.writeLock();
    defer(cache_rw_lock.writeUnlock();)
    operateFiles("cache", [](const std::string &file){ remove(("cache/" + file).data()); return 0; });
}

int webPost(const std::string &url, const std::string &data, const std::string &proxy, const string_icase_map &request_headers, std::string *retData)
{
    //return curlPost(url, data, proxy, request_headers, retData);
    int return_code = 0;
    FetchArgument argument {HTTP_POST, url, proxy, &data, &request_headers, nullptr, 0, true};
    FetchResult fetch_res {&return_code, retData, nullptr, nullptr};
    return webGet(argument, fetch_res);
}

int webPatch(const std::string &url, const std::string &data, const std::string &proxy, const string_icase_map &request_headers, std::string *retData)
{
    //return curlPatch(url, data, proxy, request_headers, retData);
    int return_code = 0;
    FetchArgument argument {HTTP_PATCH, url, proxy, &data, &request_headers, nullptr, 0, true};
    FetchResult fetch_res {&return_code, retData, nullptr, nullptr};
    return webGet(argument, fetch_res);
}

int webHead(const std::string &url, const std::string &proxy, const string_icase_map &request_headers, std::string &response_headers)
{
    //return curlHead(url, proxy, request_headers, response_headers);
    int return_code = 0;
    FetchArgument argument {HTTP_HEAD, url, proxy, nullptr, &request_headers, nullptr, 0};
    FetchResult fetch_res {&return_code, nullptr, &response_headers, nullptr};
    return webGet(argument, fetch_res);
}

string_array headers_map_to_array(const string_map &headers)
{
    string_array result;
    for(auto &kv : headers)
        result.push_back(kv.first + ": " + kv.second);
    return result;
}

int webGet(const FetchArgument& argument, FetchResult &result)
{
    if(should_block_private_address_request(argument.url))
    {
        writeLog(0, "Blocked outbound request to private/loopback target: '" + argument.url + "'.", LOG_LEVEL_WARNING);
        if(result.status_code)
            *result.status_code = 403;
        if(result.content)
            result.content->clear();
        if(result.response_headers)
            result.response_headers->clear();
        if(result.cookies)
            result.cookies->clear();
        return result.status_code ? *result.status_code : 403;
    }
    return curlGet(argument, result);
}
