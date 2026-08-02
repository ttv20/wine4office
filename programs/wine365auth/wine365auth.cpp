/*
 * Wine 365 organizational OAuth helper.
 *
 * Hosts Microsoft authentication in an owner-window MSHTML control inside the
 * Wine prefix, exchanges the public-client PKCE code, and stores the resulting
 * WAM state with DPAPI.  It never opens the host browser and never logs tokens.
 */
#include <windows.h>
#include <bcrypt.h>
#include <cryptuiapi.h>
#include <exdisp.h>
#include <mshtml.h>
#include <ole2.h>
#include <oleidl.h>
#include <ocidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" HINTERNET WINAPI InternetOpenW(const WCHAR *, DWORD, const WCHAR *, const WCHAR *, DWORD);
extern "C" HINTERNET WINAPI InternetConnectW(HINTERNET, const WCHAR *, INTERNET_PORT,
                                               const WCHAR *, const WCHAR *, DWORD, DWORD, DWORD_PTR);
extern "C" HINTERNET WINAPI HttpOpenRequestW(HINTERNET, const WCHAR *, const WCHAR *, const WCHAR *,
                                               const WCHAR *, const WCHAR *const *, DWORD, DWORD_PTR);
extern "C" BOOL WINAPI HttpSendRequestW(HINTERNET, const WCHAR *, DWORD, void *, DWORD);
extern "C" BOOL WINAPI HttpQueryInfoW(HINTERNET, DWORD, void *, DWORD *, DWORD *);
extern "C" BOOL WINAPI InternetCloseHandle(HINTERNET);

#define INTERNET_OPEN_TYPE_PRECONFIG 0
#define INTERNET_SERVICE_HTTP 3
#define INTERNET_FLAG_SECURE 0x00800000
#define INTERNET_FLAG_NO_AUTO_REDIRECT 0x00200000
#define INTERNET_FLAG_RELOAD 0x80000000
#define INTERNET_FLAG_NO_CACHE_WRITE 0x04000000
#define HTTP_QUERY_STATUS_CODE 19
#define HTTP_QUERY_LOCATION 33
#define HTTP_QUERY_RAW_HEADERS_CRLF 22
#define HTTP_QUERY_FLAG_NUMBER 0x20000000

static const char client_id[] = "d3590ed6-52b3-4102-aeff-aad2292ab01c";
static const char teams_client_id[] = "1fec8e78-bce4-4aaf-ab1b-5451cc387264";
static const char office_scope[] = "https://officeapps.live.com/.default offline_access openid profile";
static const char licensing_scope[] = "https://licensing.m365.svc.cloud.microsoft/.default";
static const char redirect_uri[] =
    "ms-appx-web://Microsoft.AAD.BrokerPlugin/d3590ed6-52b3-4102-aeff-aad2292ab01c";
static const WCHAR redirect_prefix[] = L"ms-appx-web://Microsoft.AAD.BrokerPlugin/";

static IOleObject *ole_object;
static IOleInPlaceObject *inplace_object;
static IWebBrowser2 *browser;
static IConnectionPoint *browser_connection;
static DWORD browser_connection_cookie;
static HWND host_window, owner_window;
static std::string oauth_state, oauth_code, oauth_tenant = "organizations";
static std::vector<BYTE> pending_auth_post;
static std::wstring pending_auth_path;
static bool oauth_error, oauth_cancelled;

static std::string wide_to_utf8(const WCHAR *value)
{
    int size;
    std::string result;
    if (!value) return result;
    size = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (size <= 1) return result;
    result.resize(size);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], size, NULL, NULL);
    result.resize(size - 1);
    return result;
}

static std::wstring utf8_to_wide(const std::string &value)
{
    int size;
    std::wstring result;
    if (value.empty()) return result;
    size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), value.size(), NULL, 0);
    if (!size) return result;
    result.resize(size);
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), value.size(), &result[0], size);
    return result;
}

static std::string url_encode(const std::string &value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char ch : value)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') result += ch;
        else
        {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 15];
        }
    }
    return result;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static std::string url_decode(const std::string &value)
{
    std::string result;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size())
        {
            int high = hex_value(value[i + 1]), low = hex_value(value[i + 2]);
            if (high >= 0 && low >= 0)
            {
                result += (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        result += value[i] == '+' ? ' ' : value[i];
    }
    return result;
}

static std::string query_value(const std::string &url, const char *name)
{
    size_t start = url.find('?');
    if (start == std::string::npos) return {};
    ++start;
    while (start < url.size())
    {
        size_t end = url.find('&', start), equal;
        if (end == std::string::npos) end = url.size();
        equal = url.find('=', start);
        if (equal != std::string::npos && equal < end &&
            url.compare(start, equal - start, name) == 0)
            return url_decode(url.substr(equal + 1, end - equal - 1));
        start = end + 1;
    }
    return {};
}

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64url_encode(const BYTE *bytes, size_t size)
{
    std::string result;
    unsigned int value = 0, bits = 0;
    for (size_t i = 0; i < size; ++i)
    {
        value = (value << 8) | bytes[i];
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            result += base64_chars[(value >> bits) & 63];
        }
    }
    if (bits) result += base64_chars[(value << (6 - bits)) & 63];
    return result;
}

static std::vector<BYTE> base64url_decode(const std::string &text)
{
    std::vector<BYTE> result;
    unsigned int value = 0, bits = 0;
    for (char ch : text)
    {
        const char *found = strchr(base64_chars, ch);
        if (!found) continue;
        value = (value << 6) | (unsigned int)(found - base64_chars);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            result.push_back((BYTE)((value >> bits) & 255));
        }
    }
    return result;
}

static bool random_bytes(BYTE *bytes, size_t size)
{
    return BCryptGenRandom(NULL, bytes, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

static bool sha256(const std::string &value, BYTE hash[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE handle = NULL;
    DWORD object_size = 0, returned;
    std::vector<BYTE> object;
    bool success = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (BYTE *)&object_size,
                          sizeof(object_size), &returned, 0) < 0) goto done;
    object.resize(object_size);
    if (BCryptCreateHash(algorithm, &handle, object.data(), object.size(), NULL, 0, 0) < 0) goto done;
    if (BCryptHashData(handle, (BYTE *)value.data(), value.size(), 0) < 0) goto done;
    if (BCryptFinishHash(handle, hash, 32, 0) < 0) goto done;
    success = true;
done:
    if (handle) BCryptDestroyHash(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

static ULONGLONG unix_time(void)
{
    FILETIME time;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&time);
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

static bool json_string(const std::string &json, const char *name, std::string &value)
{
    std::string needle = std::string("\"") + name + "\"";
    size_t pos = json.find(needle), end;
    value.clear();
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    for (end = pos; end < json.size(); ++end)
    {
        char ch = json[end];
        if (ch == '"') return true;
        if (ch != '\\') { value += ch; continue; }
        if (++end >= json.size()) return false;
        ch = json[end];
        switch (ch)
        {
        case '"': case '\\': case '/': value += ch; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        case 'u':
            if (end + 4 < json.size())
            {
                int code = 0;
                for (int i = 1; i <= 4; ++i)
                {
                    int digit = hex_value(json[end + i]);
                    if (digit < 0) return false;
                    code = (code << 4) | digit;
                }
                WCHAR wide[2] = {(WCHAR)code, 0};
                value += wide_to_utf8(wide);
                end += 4;
            }
            break;
        default: return false;
        }
    }
    return false;
}

static bool json_number(const std::string &json, const char *name, ULONGLONG &value)
{
    std::string needle = std::string("\"") + name + "\"";
    size_t pos = json.find(needle), end;
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    while (++pos < json.size() && std::isspace((unsigned char)json[pos])) {}
    end = pos;
    while (end < json.size() && std::isdigit((unsigned char)json[end])) ++end;
    if (end == pos) return false;
    value = _strtoui64(json.substr(pos, end - pos).c_str(), NULL, 10);
    return true;
}

static bool jwt_payload(const std::string &token, std::string &payload)
{
    size_t first = token.find('.'), second;
    if (first == std::string::npos) return false;
    second = token.find('.', first + 1);
    if (second == std::string::npos) return false;
    std::vector<BYTE> decoded = base64url_decode(token.substr(first + 1, second - first - 1));
    if (decoded.empty()) return false;
    payload.assign((const char *)decoded.data(), decoded.size());
    return true;
}

static std::wstring cache_directory(void)
{
    WCHAR path[MAX_PATH];
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", path, ARRAYSIZE(path));
    if (!size || size >= ARRAYSIZE(path))
    {
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                    NULL, SHGFP_TYPE_CURRENT, path))) return {};
    }
    std::wstring directory = path;
    directory += L"\\Wine365";
    CreateDirectoryW(directory.c_str(), NULL);
    directory += L"\\WAM";
    CreateDirectoryW(directory.c_str(), NULL);
    return directory;
}

static std::wstring cache_file(const WCHAR *name)
{
    std::wstring path = cache_directory();
    if (!path.empty()) { path += L'\\'; path += name; }
    return path;
}

static bool protected_write(const WCHAR *name, const std::string &value)
{
    DATA_BLOB input, output = {};
    std::wstring path = cache_file(name), temporary;
    HANDLE file;
    DWORD written;
    bool success = false;
    if (path.empty()) return false;
    input.cbData = value.size();
    input.pbData = (BYTE *)value.data();
    if (!CryptProtectData(&input, L"Wine 365 WAM", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    temporary = path + L".tmp";
    file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        success = WriteFile(file, output.pbData, output.cbData, &written, NULL) &&
                  written == output.cbData && FlushFileBuffers(file);
        CloseHandle(file);
        if (success) success = MoveFileExW(temporary.c_str(), path.c_str(),
                                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!success) DeleteFileW(temporary.c_str());
    }
    LocalFree(output.pbData);
    return success;
}

static bool protected_read(const WCHAR *name, std::string &value)
{
    DATA_BLOB input = {}, output = {};
    std::wstring path = cache_file(name);
    LARGE_INTEGER size;
    HANDLE file;
    DWORD read;
    std::vector<BYTE> bytes;
    bool success = false;
    value.clear();
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024)
    { CloseHandle(file); return false; }
    bytes.resize((size_t)size.QuadPart);
    if (!ReadFile(file, bytes.data(), bytes.size(), &read, NULL) || read != bytes.size())
    { CloseHandle(file); return false; }
    CloseHandle(file);
    input.cbData = bytes.size();
    input.pbData = bytes.data();
    if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL,
                           CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        value.assign((const char *)output.pbData, output.cbData);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        success = true;
    }
    SecureZeroMemory(bytes.data(), bytes.size());
    return success;
}

static bool http_post(const WCHAR *host, const WCHAR *path, const std::string &body,
                      std::string &response)
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD status = 0, status_size = sizeof(status), available, read;
    bool success = false;
    response.clear();
    session = WinHttpOpen(L"Wine365-WAM/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"Wine365-WAM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto done;
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    if (!(connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0))) goto done;
    request = WinHttpOpenRequest(connection, L"POST", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) goto done;
    if (!WinHttpAddRequestHeaders(request, L"Content-Type: application/x-www-form-urlencoded\r\n",
                                  -1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) goto done;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (void *)body.data(), body.size(), body.size(), 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto done;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) goto done;
    do
    {
        if (!WinHttpQueryDataAvailable(request, &available)) goto done;
        if (available)
        {
            size_t old_size = response.size();
            response.resize(old_size + available);
            if (!WinHttpReadData(request, &response[old_size], available, &read)) goto done;
            response.resize(old_size + read);
        }
    } while (available);
    success = true;
done:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return success;
}

static bool internet_post_redirect(const WCHAR *host, const WCHAR *path,
                                   const std::vector<BYTE> &body, std::wstring &redirect)
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    const WCHAR *accept[] = {L"text/html", L"application/xhtml+xml", NULL};
    DWORD status = 0, status_size = sizeof(status), index = 0;
    bool success = false;
    session = InternetOpenW(L"Wine365-WAM/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                            NULL, NULL, 0);
    if (!session || !(connection = InternetConnectW(session, host, INTERNET_DEFAULT_HTTPS_PORT,
                                                     NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0)))
        goto done;
    request = HttpOpenRequestW(connection, L"POST", path, NULL, NULL, accept,
                               INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_AUTO_REDIRECT |
                               INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!request || !HttpSendRequestW(request,
            L"Content-Type: application/x-www-form-urlencoded\r\n", -1,
            (void *)body.data(), body.size()) ||
        !HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                        &status, &status_size, &index)) goto done;
    if (status >= 300 && status < 400)
    {
        std::vector<WCHAR> location(32768);
        DWORD location_size = location.size() * sizeof(WCHAR);
        index = 0;
        if (HttpQueryInfoW(request, HTTP_QUERY_LOCATION, location.data(),
                           &location_size, &index))
        {
            if (!_wcsnicmp(location.data(), L"https://", 8) ||
                !_wcsnicmp(location.data(), redirect_prefix, ARRAYSIZE(redirect_prefix) - 1))
                redirect = location.data();
            else if (location[0] == '/') redirect = L"https://" + std::wstring(host) + location.data();
            success = !redirect.empty();
        }
        if (!success)
        {
            std::vector<WCHAR> raw(32768);
            DWORD raw_size = raw.size() * sizeof(WCHAR);
            index = 0;
            if (HttpQueryInfoW(request, HTTP_QUERY_RAW_HEADERS_CRLF, raw.data(), &raw_size, &index))
            {
                const WCHAR *line = raw.data();
                while (*line)
                {
                    const WCHAR *end = wcsstr(line, L"\r\n");
                    if (!end) end = line + wcslen(line);
                    if (end - line >= 9 && !_wcsnicmp(line, L"Location:", 9))
                    {
                        const WCHAR *value = line + 9;
                        while (value < end && (*value == ' ' || *value == '\t')) ++value;
                        std::wstring value_string(value, end);
                        if (!_wcsnicmp(value_string.c_str(), L"https://", 8) ||
                            !_wcsnicmp(value_string.c_str(), redirect_prefix,
                                       ARRAYSIZE(redirect_prefix) - 1)) redirect = value_string;
                        else if (!value_string.empty() && value_string[0] == '/')
                            redirect = L"https://" + std::wstring(host) + value_string;
                        success = !redirect.empty();
                        break;
                    }
                    line = *end ? end + 2 : end;
                }
            }
        }
    }
done:
    if (request) InternetCloseHandle(request);
    if (connection) InternetCloseHandle(connection);
    if (session) InternetCloseHandle(session);
    return success;
}

static bool http_get(const WCHAR *host, const WCHAR *path, std::string &response)
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD status = 0, status_size = sizeof(status), available, read;
    bool success = false;
    response.clear();
    session = WinHttpOpen(L"Wine365-WAM/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"Wine365-WAM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto done;
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    if (!(connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0))) goto done;
    request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto done;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) goto done;
    do
    {
        if (!WinHttpQueryDataAvailable(request, &available) ||
            response.size() + available > 1024 * 1024) goto done;
        if (available)
        {
            size_t old_size = response.size();
            response.resize(old_size + available);
            if (!WinHttpReadData(request, &response[old_size], available, &read)) goto done;
            response.resize(old_size + read);
        }
    } while (available);
    success = true;
done:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return success;
}

static bool discover_tenant(const std::string &domain, std::string &tenant)
{
    std::string document, issuer;
    std::wstring path = utf8_to_wide("/" + domain + "/v2.0/.well-known/openid-configuration");
    static const std::string prefix = "https://login.microsoftonline.com/";
    if (!http_get(L"login.microsoftonline.com", path.c_str(), document) ||
        !json_string(document, "issuer", issuer) || issuer.compare(0, prefix.size(), prefix))
        return false;
    size_t end = issuer.find('/', prefix.size());
    if (end == std::string::npos || end == prefix.size()) return false;
    tenant = issuer.substr(prefix.size(), end - prefix.size());
    return std::all_of(tenant.begin(), tenant.end(), [](unsigned char ch)
        { return std::isalnum(ch) || ch == '-'; });
}

static bool token_request(const std::string &body, std::string &response,
                          const std::string &tenant = "organizations")
{
    std::wstring path = utf8_to_wide("/" + tenant + "/oauth2/v2.0/token");
    return http_post(L"login.microsoftonline.com", path.c_str(), body, response);
}

struct token_set
{
    std::string access_token, refresh_token, id_token;
    ULONGLONG expires_in = 0;
};

static void secure_clear(std::string &value)
{
    if (!value.empty()) SecureZeroMemory(&value[0], value.size());
    value.clear();
}

static void secure_clear(token_set &tokens)
{
    secure_clear(tokens.access_token);
    secure_clear(tokens.refresh_token);
    secure_clear(tokens.id_token);
    tokens.expires_in = 0;
}

static bool parse_token_set(const std::string &response, token_set &tokens,
                            const token_set *previous = NULL)
{
    if (!json_string(response, "access_token", tokens.access_token)) return false;
    json_string(response, "refresh_token", tokens.refresh_token);
    json_string(response, "id_token", tokens.id_token);
    json_number(response, "expires_in", tokens.expires_in);
    if (previous)
    {
        if (tokens.refresh_token.empty()) tokens.refresh_token = previous->refresh_token;
        if (tokens.id_token.empty()) tokens.id_token = previous->id_token;
    }
    return !tokens.refresh_token.empty() && !tokens.id_token.empty();
}

static bool refresh_scope(const std::string &refresh_token, const char *scope,
                          token_set &tokens, const token_set *previous = NULL,
                          const char *requested_client_id = client_id)
{
    std::string response;
    std::string body = "client_id=" + std::string(requested_client_id) +
        "&grant_type=refresh_token&refresh_token=" + url_encode(refresh_token) +
        "&scope=" + url_encode(scope);
    bool success = token_request(body, response) && parse_token_set(response, tokens, previous);
    secure_clear(body);
    secure_clear(response);
    return success;
}

static bool save_tokens(const token_set &office, const token_set &licensing)
{
    std::string payload, oid, tid, username, first_name, last_name, display_name, exp;
    std::string account_id, authority, client_info_json, client_info;
    ULONGLONG expires;
    if (!jwt_payload(office.id_token, payload) ||
        !json_string(payload, "oid", oid) || !json_string(payload, "tid", tid) ||
        (!json_string(payload, "preferred_username", username) &&
         !json_string(payload, "upn", username))) return false;
    json_string(payload, "given_name", first_name);
    json_string(payload, "family_name", last_name);
    json_string(payload, "name", display_name);
    if (!json_number(payload, "exp", expires)) expires = unix_time() + office.expires_in;
    exp = std::to_string(expires);
    account_id = oid + "." + tid;
    authority = "https://login.microsoftonline.com/" + tid + "/";
    client_info_json = "{\"uid\":\"" + oid + "\",\"utid\":\"" + tid + "\"}";
    client_info = base64url_encode((const BYTE *)client_info_json.data(), client_info_json.size());

    bool success =
        protected_write(L"wam-access-token.dat", office.access_token) &&
        protected_write(L"wam-id-token.dat", office.id_token) &&
        protected_write(L"wam-refresh-token.dat", office.refresh_token) &&
        protected_write(L"wam-licensing-token.dat", licensing.access_token) &&
        protected_write(L"wam-token-expires-on.dat", exp) &&
        protected_write(L"wam-account-username.dat", username) &&
        protected_write(L"wam-account-id.dat", account_id) &&
        protected_write(L"wam-account-oid.dat", oid) &&
        protected_write(L"wam-account-tenant-id.dat", tid) &&
        protected_write(L"wam-account-authority.dat", authority) &&
        protected_write(L"wam-client-info.dat", client_info);
    if (success && !first_name.empty()) success = protected_write(L"wam-account-first-name.dat", first_name);
    if (success && !last_name.empty()) success = protected_write(L"wam-account-last-name.dat", last_name);
    if (success && !display_name.empty()) success = protected_write(L"wam-account-display-name.dat", display_name);
    return success;
}

static bool exchange_and_save(const std::string &code, const std::string &verifier)
{
    token_set office, licensing;
    std::string response;
    std::string body = "client_id=" + std::string(client_id) +
        "&grant_type=authorization_code&code=" + url_encode(code) +
        "&redirect_uri=" + url_encode(redirect_uri) +
        "&code_verifier=" + url_encode(verifier) +
        "&scope=" + url_encode(office_scope);
    bool success = token_request(body, response, oauth_tenant) && parse_token_set(response, office);
    if (success) success = refresh_scope(office.refresh_token, licensing_scope, licensing, &office);
    if (success && !licensing.refresh_token.empty()) office.refresh_token = licensing.refresh_token;
    if (success) success = save_tokens(office, licensing);
    secure_clear(body);
    secure_clear(response);
    secure_clear(office);
    secure_clear(licensing);
    return success;
}

static bool refresh_and_save(void)
{
    token_set previous, office, licensing;
    bool success = protected_read(L"wam-refresh-token.dat", previous.refresh_token) &&
                   protected_read(L"wam-id-token.dat", previous.id_token);
    if (success) success = refresh_scope(previous.refresh_token, office_scope, office, &previous);
    if (success) success = refresh_scope(office.refresh_token, licensing_scope, licensing, &office);
    if (success && !licensing.refresh_token.empty()) office.refresh_token = licensing.refresh_token;
    if (success) success = save_tokens(office, licensing);
    secure_clear(previous);
    secure_clear(office);
    secure_clear(licensing);
    return success;
}

static std::string normalize_resource_scope(const std::string &scope)
{
    static const std::string prefix = "service::";
    if (!scope.compare(0, prefix.size(), prefix))
    {
        size_t end = scope.find("::", prefix.size());
        if (end == std::string::npos || end == prefix.size()) return {};
        return "https://" + scope.substr(prefix.size(), end - prefix.size()) + "/.default";
    }

    /* WAM callers use both delegated scopes and v1-style resource URLs.  The
     * v2 token endpoint accepts the former unchanged, but a bare resource URL
     * must be converted to its /.default scope. */
    size_t scheme = scope.find("://");
    if (scheme != std::string::npos)
    {
        size_t path = scope.find('/', scheme + 3);
        if (path == std::string::npos) return scope + "/.default";
        if (path == scope.size() - 1) return scope + ".default";
    }
    return scope;
}

static bool resource_cache_names(const std::string &scope, const std::string &requested_client_id,
                                 std::wstring &token_name,
                                 std::wstring &expires_name)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    BYTE hash[32];
    WCHAR suffix[17];

    if (!sha256(scope + "\n" + requested_client_id, hash)) return false;
    for (unsigned int i = 0; i < 8; ++i)
    {
        suffix[2 * i] = hex[hash[i] >> 4];
        suffix[2 * i + 1] = hex[hash[i] & 0xf];
    }
    suffix[16] = 0;
    token_name = L"wam-resource-" + std::wstring(suffix) + L"-token.dat";
    expires_name = L"wam-resource-" + std::wstring(suffix) + L"-expires-on.dat";
    SecureZeroMemory(hash, sizeof(hash));
    return true;
}

static bool refresh_resource_and_save(const std::string &requested_scope,
                                      const std::string &requested_client_id)
{
    token_set previous, resource;
    std::string scope = normalize_resource_scope(requested_scope);
    std::string cached_token, cached_expires;
    std::wstring token_name, expires_name;
    bool have_cache_names = resource_cache_names(scope, requested_client_id, token_name, expires_name);
    ULONGLONG expiry = 0;

    if (have_cache_names && protected_read(token_name.c_str(), cached_token) &&
        protected_read(expires_name.c_str(), cached_expires))
        expiry = _strtoui64(cached_expires.c_str(), NULL, 10);
    if (!cached_token.empty() && expiry > unix_time() + 300)
    {
        bool success = protected_write(L"wam-access-token.dat", cached_token) &&
                       protected_write(L"wam-token-expires-on.dat", cached_expires);
        secure_clear(cached_token);
        secure_clear(cached_expires);
        secure_clear(scope);
        return success;
    }
    secure_clear(cached_token);
    secure_clear(cached_expires);

    bool success = !scope.empty() &&
        (requested_client_id == client_id || requested_client_id == teams_client_id) &&
        protected_read(L"wam-refresh-token.dat", previous.refresh_token) &&
        protected_read(L"wam-id-token.dat", previous.id_token);
    if (success) success = refresh_scope(previous.refresh_token, scope.c_str(), resource, &previous,
                                         requested_client_id.c_str());
    if (success)
    {
        std::string expires = std::to_string(unix_time() + resource.expires_in);
        success = protected_write(L"wam-access-token.dat", resource.access_token) &&
                  protected_write(L"wam-refresh-token.dat", resource.refresh_token) &&
                  protected_write(L"wam-token-expires-on.dat", expires) &&
                  (!have_cache_names ||
                   (protected_write(token_name.c_str(), resource.access_token) &&
                    protected_write(expires_name.c_str(), expires)));
    }
    secure_clear(scope);
    secure_clear(previous);
    secure_clear(resource);
    return success;
}

static bool handle_redirect(const WCHAR *location)
{
    if (!location || _wcsnicmp(location, redirect_prefix, ARRAYSIZE(redirect_prefix) - 1))
        return false;
    std::string url = wide_to_utf8(location);
    std::string state = query_value(url, "state");
    oauth_code = query_value(url, "code");
    oauth_error = state != oauth_state || oauth_code.empty();
    PostMessageW(host_window, WM_CLOSE, 1, 0);
    SecureZeroMemory((void *)url.data(), url.size());
    return true;
}

static SAFEARRAY *variant_byte_array(VARIANTARG *arg)
{
    for (unsigned int i = 0; i < 3 && arg && arg->vt == (VT_BYREF | VT_VARIANT); ++i)
        arg = arg->pvarVal;
    return arg && arg->vt == (VT_ARRAY | VT_UI1) ? arg->parray : NULL;
}

static bool queue_auth_handoff(const WCHAR *location, VARIANTARG *post_arg)
{
    static const WCHAR login_srf[] = L"https://login.microsoftonline.com/login.srf";
    static const WCHAR process_auth[] = L"https://login.microsoftonline.com/common/SAS/ProcessAuth";
    static const char saml[] = "SAMLResponse=", relay[] = "RelayState=";
    SAFEARRAY *array = variant_byte_array(post_arg);
    LONG lower, upper;
    BYTE *data = NULL;
    bool is_saml, is_process_auth, has_saml = false, has_relay = false;

    if (!location) return false;
    is_saml = !_wcsicmp(location, login_srf);
    is_process_auth = !_wcsicmp(location, process_auth);
    if ((!is_saml && !is_process_auth) || !array ||
        FAILED(SafeArrayGetLBound(array, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(array, 1, &upper)) || upper < lower ||
        FAILED(SafeArrayAccessData(array, (void **)&data))) return false;
    size_t size = upper - lower + 1;
    if (size && !data[size - 1]) --size;
    for (size_t i = 0; is_saml && i < size; ++i)
    {
        if (i + sizeof(saml) - 1 <= size && !memcmp(data + i, saml, sizeof(saml) - 1))
            has_saml = true;
        if (i + sizeof(relay) - 1 <= size && !memcmp(data + i, relay, sizeof(relay) - 1))
            has_relay = true;
    }
    if ((is_saml && has_saml && has_relay) || (is_process_auth && size))
    {
        pending_auth_post.assign(data, data + size);
        pending_auth_path = is_saml ? L"/login.srf" : L"/common/SAS/ProcessAuth";
    }
    SafeArrayUnaccessData(array);
    if (pending_auth_post.empty()) return false;
    PostMessageW(host_window, WM_APP + 2, 0, 0);
    return true;
}

class CallbackProtocol : public IInternetProtocol
{
    LONG ref = 1;
public:
    virtual ~CallbackProtocol() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_IInternetProtocol && iid != IID_IInternetProtocolRoot)
            return E_NOINTERFACE;
        *out = static_cast<IInternetProtocol *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    { ULONG value = InterlockedDecrement(&ref); if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE Start(LPCWSTR url, IInternetProtocolSink *sink, IInternetBindInfo *,
                                    DWORD, HANDLE_PTR) override
    {
        HRESULT hr = handle_redirect(url) ? E_ABORT : INET_E_INVALID_URL;
        if (sink) sink->ReportResult(hr, 0, NULL);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Continue(PROTOCOLDATA *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Abort(HRESULT, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Terminate(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Suspend() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Resume() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Read(void *, ULONG, ULONG *read) override
    { if (read) *read = 0; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRequest(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnlockRequest() override { return S_OK; }
};

class CallbackProtocolFactory : public IClassFactory
{
    LONG ref = 1;
public:
    virtual ~CallbackProtocolFactory() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    { ULONG value = InterlockedDecrement(&ref); if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override
    {
        CallbackProtocol *protocol;
        HRESULT hr;
        if (outer) return CLASS_E_NOAGGREGATION;
        protocol = new CallbackProtocol();
        hr = protocol->QueryInterface(iid, out);
        protocol->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

class BrowserEvents : public IDispatch
{
    LONG ref = 1;
public:
    virtual ~BrowserEvents() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_IDispatch) return E_NOINTERFACE;
        *out = static_cast<IDispatch *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    { ULONG value = InterlockedDecrement(&ref); if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override
    { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override
    { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS *params,
                                     VARIANT *, EXCEPINFO *, UINT *) override
    {
        /* DWebBrowserEvents2::BeforeNavigate2 arguments are reversed. Capture
         * the BrokerPlugin URI before MSHTML replaces the unknown protocol
         * with its HTTP error page.  Federated SAML and native MFA responses
         * are submitted with WinINet because MSHTML cannot send these form
         * streams intact. */
        if (id == 250 && params && params->cArgs >= 7)
        {
            VARIANTARG *arg = &params->rgvarg[5];
            BSTR location = NULL;
            if (arg->vt == (VT_BYREF | VT_VARIANT) && arg->pvarVal)
                arg = arg->pvarVal;
            if (arg->vt == VT_BSTR) location = arg->bstrVal;
            else if (arg->vt == (VT_BYREF | VT_BSTR) && arg->pbstrVal) location = *arg->pbstrVal;
            bool handled = handle_redirect(location);
            if (!handled) handled = queue_auth_handoff(location, &params->rgvarg[2]);
            if (handled)
            {
                VARIANTARG *cancel = &params->rgvarg[0];
                if (cancel->vt == (VT_BYREF | VT_BOOL) && cancel->pboolVal)
                    *cancel->pboolVal = VARIANT_TRUE;
            }
        }
        return S_OK;
    }
};

class BrowserSite : public IOleClientSite, public IOleInPlaceSite, public IOleInPlaceFrame
{
    LONG ref = 1;
    HWND hwnd;
public:
    virtual ~BrowserSite() = default;
    explicit BrowserSite(HWND window) : hwnd(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid == IID_IUnknown || iid == IID_IOleClientSite)
            *out = static_cast<IOleClientSite *>(this);
        else if (iid == IID_IOleWindow || iid == IID_IOleInPlaceSite)
            *out = static_cast<IOleInPlaceSite *>(this);
        else if (iid == IID_IOleInPlaceUIWindow || iid == IID_IOleInPlaceFrame)
            *out = static_cast<IOleInPlaceFrame *>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    { ULONG value = InterlockedDecrement(&ref); if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE SaveObject() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer **value) override
    { if (value) *value = NULL; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE ShowObject() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND *value) override
    { if (!value) return E_POINTER; *value = hwnd; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CanInPlaceActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUIActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame **frame, IOleInPlaceUIWindow **doc,
                                               RECT *pos, RECT *clip, OLEINPLACEFRAMEINFO *info) override
    {
        if (frame) { *frame = static_cast<IOleInPlaceFrame *>(this); AddRef(); }
        if (doc) *doc = NULL;
        if (pos) GetClientRect(hwnd, pos);
        if (clip) GetClientRect(hwnd, clip);
        if (info)
        {
            info->cb = sizeof(*info); info->fMDIApp = FALSE; info->hwndFrame = hwnd;
            info->haccel = NULL; info->cAccelEntries = 0;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Scroll(SIZE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DiscardUndoState() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeactivateAndUndo() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnPosRectChange(const RECT *rect) override
    { return inplace_object && rect ? inplace_object->SetObjectRects(rect, rect) : S_OK; }
    HRESULT STDMETHODCALLTYPE GetBorder(RECT *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject *, LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMenu(HMENU, HOLEMENU, HWND) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, WORD) override { return E_NOTIMPL; }
};

static void resize_browser(HWND hwnd)
{
    RECT rect;
    if (inplace_object && GetClientRect(hwnd, &rect)) inplace_object->SetObjectRects(&rect, &rect);
}

static void close_browser(void)
{
    if (browser_connection)
    {
        browser_connection->Unadvise(browser_connection_cookie);
        browser_connection->Release();
        browser_connection = NULL;
        browser_connection_cookie = 0;
    }
    if (browser) { browser->Stop(); browser->Quit(); browser->Release(); browser = NULL; }
    if (inplace_object) { inplace_object->Release(); inplace_object = NULL; }
    if (ole_object)
    {
        ole_object->Close(OLECLOSE_NOSAVE);
        ole_object->SetClientSite(NULL);
        ole_object->Release();
        ole_object = NULL;
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_SIZE:
        resize_browser(hwnd);
        return 0;
    case WM_APP + 2:
        if (browser && !pending_auth_post.empty() && !pending_auth_path.empty())
        {
            std::wstring redirect;
            if (internet_post_redirect(L"login.microsoftonline.com", pending_auth_path.c_str(),
                                       pending_auth_post, redirect))
            {
                if (!handle_redirect(redirect.c_str()))
                {
                    VARIANT empty;
                    BSTR target = SysAllocString(redirect.c_str());
                    VariantInit(&empty);
                    browser->Navigate(target, &empty, &empty, &empty, &empty);
                    SysFreeString(target);
                }
            }
            else
            {
                oauth_error = true;
                PostMessageW(hwnd, WM_CLOSE, 1, 0);
            }
            SecureZeroMemory(pending_auth_post.data(), pending_auth_post.size());
            pending_auth_post.clear();
            pending_auth_path.clear();
        }
        return 0;
    case WM_TIMER:
        if (browser)
        {
            BSTR location = NULL;
            if (SUCCEEDED(browser->get_LocationURL(&location)) && location)
            {
                bool accepted = handle_redirect(location);
                SysFreeString(location);
                if (accepted) return 0;
            }
        }
        return 0;
    case WM_CLOSE:
        if (!wparam && oauth_code.empty()) oauth_cancelled = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        close_browser();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void center_window(HWND window, HWND owner)
{
    RECT target, rect;
    if (owner && GetWindowRect(owner, &target) && GetWindowRect(window, &rect))
    {
        int width = rect.right - rect.left, height = rect.bottom - rect.top;
        SetWindowPos(window, NULL, target.left + ((target.right - target.left) - width) / 2,
                     target.top + ((target.bottom - target.top) - height) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static bool run_owned_oauth(HINSTANCE instance, HWND owner, const std::string &login_hint,
                            std::string &verifier)
{
    static const WCHAR class_name[] = L"Wine365OAuthBroker";
    BrowserSite *site;
    WNDCLASSW window_class = {};
    BYTE random[64], challenge_hash[32];
    std::string challenge, authorize_url;
    std::wstring authorize_url_w;
    RECT rect;
    VARIANT empty;
    BSTR url;
    IInternetSession *internet_session = NULL;
    CallbackProtocolFactory *protocol_factory = NULL;
    MSG message;
    HRESULT hr;

    if (!random_bytes(random, sizeof(random))) return false;
    verifier = base64url_encode(random, sizeof(random));
    if (!sha256(verifier, challenge_hash)) return false;
    challenge = base64url_encode(challenge_hash, sizeof(challenge_hash));
    if (!random_bytes(random, 32)) return false;
    oauth_state = base64url_encode(random, 32);
    SecureZeroMemory(random, sizeof(random));

    oauth_tenant = "organizations";
    {
        size_t at = login_hint.rfind('@');
        if (at != std::string::npos && at + 1 < login_hint.size())
        {
            std::string domain = login_hint.substr(at + 1);
            if (!std::all_of(domain.begin(), domain.end(), [](unsigned char ch)
                { return std::isalnum(ch) || ch == '.' || ch == '-'; }) ||
                !discover_tenant(domain, oauth_tenant))
                return false;
        }
    }
    authorize_url = "https://login.microsoftonline.com/" + oauth_tenant +
        "/oauth2/v2.0/authorize?client_id=" + std::string(client_id) +
        "&response_type=code&response_mode=query&redirect_uri=" +
        url_encode(redirect_uri) + "&scope=" + url_encode(office_scope) +
        "&code_challenge=" + challenge + "&code_challenge_method=S256&state=" + oauth_state;
    if (!login_hint.empty()) authorize_url += "&login_hint=" + url_encode(login_hint);
    authorize_url_w = utf8_to_wide(authorize_url);

    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = class_name;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    host_window = CreateWindowExW(WS_EX_DLGMODALFRAME, class_name, L"Sign in to Microsoft 365",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 520, 700,
                                  owner, NULL, instance, NULL);
    if (!host_window) return false;
    center_window(host_window, owner);
    if (owner) EnableWindow(owner, FALSE);

    site = new BrowserSite(host_window);
    hr = CoCreateInstance(CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER,
                          IID_IOleObject, (void **)&ole_object);
    if (FAILED(hr)) { site->Release(); DestroyWindow(host_window); return false; }
    ole_object->SetClientSite(site);
    OleSetContainedObject(ole_object, TRUE);
    GetClientRect(host_window, &rect);
    hr = ole_object->DoVerb(OLEIVERB_SHOW, NULL, site, 0, host_window, &rect);
    site->Release();
    if (FAILED(hr)) { DestroyWindow(host_window); return false; }
    if (FAILED(ole_object->QueryInterface(IID_IOleInPlaceObject, (void **)&inplace_object)) ||
        FAILED(ole_object->QueryInterface(IID_IWebBrowser2, (void **)&browser)))
    { DestroyWindow(host_window); return false; }
    resize_browser(host_window);
    {
        IConnectionPointContainer *container = NULL;
        BrowserEvents *events = new BrowserEvents();
        if (SUCCEEDED(browser->QueryInterface(IID_IConnectionPointContainer, (void **)&container)) &&
            SUCCEEDED(container->FindConnectionPoint(DIID_DWebBrowserEvents2, &browser_connection)) &&
            FAILED(browser_connection->Advise(events, &browser_connection_cookie)))
        {
            browser_connection->Release();
            browser_connection = NULL;
        }
        if (container) container->Release();
        events->Release();
    }

    protocol_factory = new CallbackProtocolFactory();
    hr = CoInternetGetSession(0, &internet_session, 0);
    if (SUCCEEDED(hr))
        hr = internet_session->RegisterNameSpace(protocol_factory, CLSID_NULL,
                                                  L"ms-appx-web", 0, NULL, 0);
    if (FAILED(hr))
    {
        if (internet_session) internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        return false;
    }

    {
        static const char user_agent[] =
            "Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; rv:11.0) like Gecko";
        UrlMkSetSessionOption(URLMON_OPTION_USERAGENT, (void *)user_agent,
                              sizeof(user_agent) - 1, 0);
    }
    VariantInit(&empty);
    url = SysAllocString(authorize_url_w.c_str());
    hr = browser->Navigate(url, &empty, &empty, &empty, &empty);
    SysFreeString(url);
    if (FAILED(hr))
    {
        internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
        internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        return false;
    }
    ShowWindow(host_window, SW_SHOW);
    UpdateWindow(host_window);
    SetForegroundWindow(host_window);
    SetTimer(host_window, 1, 250, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
    internet_session->Release();
    protocol_factory->Release();
    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return !oauth_cancelled && !oauth_error && !oauth_code.empty();
}

static std::string read_login_hint_file(const WCHAR *path)
{
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read;
    std::string value;
    if (!path || !*path) return value;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return value;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 4096)
    {
        value.resize((size_t)size.QuadPart);
        if (!ReadFile(file, &value[0], value.size(), &read, NULL)) value.clear();
        else value.resize(read);
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
    }
    CloseHandle(file);
    DeleteFileW(path);
    return value;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command_line, int show)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string verifier, login_hint, cached_refresh;
    bool success;
    (void)previous; (void)command_line; (void)show;
    if (!argv) return 3;
    if (argc >= 2 && !wcscmp(argv[1], L"--self-test-cache"))
    {
        BYTE bytes[32];
        std::string expected, actual;
        success = random_bytes(bytes, sizeof(bytes));
        if (success) expected = base64url_encode(bytes, sizeof(bytes));
        if (success) success = protected_write(L"self-test.dat", expected) &&
                               protected_read(L"self-test.dat", actual) && actual == expected;
        DeleteFileW(cache_file(L"self-test.dat").c_str());
        SecureZeroMemory(bytes, sizeof(bytes));
        LocalFree(argv);
        return success ? 0 : 3;
    }
    if (argc >= 2 && !wcscmp(argv[1], L"--refresh"))
    {
        success = refresh_and_save();
        LocalFree(argv);
        return success ? 0 : 3;
    }
    if (argc >= 4 && !wcscmp(argv[1], L"--refresh-resource"))
    {
        std::string scope = wide_to_utf8(argv[2]);
        std::string requested_client_id = wide_to_utf8(argv[3]);
        success = refresh_resource_and_save(scope, requested_client_id);
        secure_clear(scope);
        secure_clear(requested_client_id);
        LocalFree(argv);
        return success ? 0 : 3;
    }
    for (int i = 1; i < argc; ++i)
    {
        if (!wcscmp(argv[i], L"--owner") && i + 1 < argc)
            owner_window = (HWND)(ULONG_PTR)_wcstoui64(argv[++i], NULL, 0);
        else if (!wcscmp(argv[i], L"--login-hint-file") && i + 1 < argc)
            login_hint = read_login_hint_file(argv[++i]);
    }
    LocalFree(argv);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 3;
    OleInitialize(NULL);
    if (protected_read(L"wam-refresh-token.dat", cached_refresh) && !cached_refresh.empty())
    {
        success = refresh_and_save();
        SecureZeroMemory((void *)cached_refresh.data(), cached_refresh.size());
        if (success)
        {
            OleUninitialize();
            CoUninitialize();
            return 0;
        }
    }
    success = run_owned_oauth(instance, owner_window, login_hint, verifier);
    if (success) success = exchange_and_save(oauth_code, verifier);
    SecureZeroMemory((void *)oauth_code.data(), oauth_code.size());
    SecureZeroMemory((void *)verifier.data(), verifier.size());
    OleUninitialize();
    CoUninitialize();
    if (!success && !oauth_cancelled)
        MessageBoxW(owner_window, L"Microsoft 365 sign-in could not be completed. Please try again.",
                    L"Microsoft 365", MB_OK | MB_ICONERROR);
    return success ? 0 : oauth_cancelled ? 2 : 3;
}
