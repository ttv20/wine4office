/*
 * Wine4Office organizational OAuth helper.
 *
 * Hosts Microsoft authentication in an owner-window MSHTML control inside the
 * Wine prefix, exchanges the public-client PKCE code, and stores each account's
 * versioned WAM bundle with DPAPI.  It never opens the host browser and never
 * logs tokens.
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
static const char teams_nested_client_id[] = "f4060917-6abe-40d7-baa6-f634c0eda4ac";
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
    directory += L"\\Wine4Office";
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

static bool cache_transaction_pending(void)
{
    std::wstring path = cache_file(L"wam-transaction.pending");
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

class cache_lock
{
    HANDLE mutex = NULL;
    bool locked = false;

public:
    cache_lock()
    {
        DWORD wait;
        mutex = CreateMutexW(NULL, FALSE, L"Local\\Wine4OfficeWamCache");
        if (!mutex) return;
        wait = WaitForSingleObject(mutex, INFINITE);
        locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }

    ~cache_lock()
    {
        if (mutex)
        {
            if (locked) ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
    }

    bool valid() const { return locked; }
};

class cache_transaction
{
    cache_lock lock;
    std::wstring marker, target, backup, temporary;
    bool active = false, replaced = false;
    bool write_marker(const char *state = "prepared")
    {
        HANDLE file;
        DWORD written;
        std::string name;
        std::wstring marker_temporary;

        if (target.empty()) return false;
        const WCHAR *filename = wcsrchr(target.c_str(), L'\\');
        name = wide_to_utf8(filename ? filename + 1 : target.c_str());
        marker = cache_file(L"wam-transaction.pending");
        if (marker.empty()) return false;
        marker_temporary = marker + L".new";
        file = CreateFileW(marker_temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, NULL);
        if (file == INVALID_HANDLE_VALUE) return false;
        std::string content = "Wine4OfficeWamBundle=1\n" + name + "\n" + state + "\n";
        bool success = WriteFile(file, content.data(), content.size(), &written, NULL) &&
                       written == content.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (success) success = MoveFileExW(marker_temporary.c_str(), marker.c_str(),
                                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!success) DeleteFileW(marker_temporary.c_str());
        return success;
    }

public:
    bool valid() const { return lock.valid() && active; }

    bool begin(const std::wstring &bundle)
    {
        if (!lock.valid() || cache_transaction_pending()) return false;
        target = bundle;
        backup = target + L".bak";
        temporary = target + L".new";
        DeleteFileW(backup.c_str());
        DeleteFileW(temporary.c_str());
        active = write_marker();
        return active;
    }

    bool commit()
    {
        if (!valid() || !DeleteFileW(marker.c_str())) return false;
        DeleteFileW(backup.c_str());
        DeleteFileW(temporary.c_str());
        active = false;
        return true;
    }

    bool rollback()
    {
        bool success = true;
        if (!active) return false;
        DeleteFileW(temporary.c_str());
        if (replaced)
        {
            if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                success = DeleteFileW(target.c_str()) &&
                          MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
            }
            else success = DeleteFileW(target.c_str()) ||
                           GetLastError() == ERROR_FILE_NOT_FOUND;
        }
        if (success && !DeleteFileW(marker.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            success = false;
        if (success)
        {
            DeleteFileW(backup.c_str());
            active = false;
        }
        return success;
    }

    bool replace_bundle(const std::string &value);
};

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
    if (!CryptProtectData(&input, L"Wine4Office WAM", NULL, NULL, NULL,
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

static bool delete_cache_file(const WCHAR *name)
{
    std::wstring path = cache_file(name);
    if (path.empty()) return false;
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static bool protected_read_path(const std::wstring &path, std::string &value, bool check_pending)
{
    DATA_BLOB input = {}, output = {};
    LARGE_INTEGER size;
    HANDLE file;
    DWORD read;
    std::vector<BYTE> bytes;
    bool success = false;

    value.clear();
    if (path.empty() || (check_pending && cache_transaction_pending())) return false;
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

static bool protected_read(const WCHAR *name, std::string &value)
{
    return protected_read_path(cache_file(name), value, true);
}

bool cache_transaction::replace_bundle(const std::string &value)
{
    DATA_BLOB input, output = {};
    HANDLE file;
    DWORD written;
    bool success = false;

    if (!valid()) return false;
    input.cbData = value.size();
    input.pbData = (BYTE *)value.data();
    if (!CryptProtectData(&input, L"Wine4Office WAM bundle", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        success = WriteFile(file, output.pbData, output.cbData, &written, NULL) &&
                  written == output.cbData && FlushFileBuffers(file);
        CloseHandle(file);
        if (success)
        {
            if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
                success = ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(),
                                       REPLACEFILE_WRITE_THROUGH, NULL, NULL);
            else success = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        }
        if (!success) DeleteFileW(temporary.c_str());
    }
    replaced = success;
    if (success && !write_marker("replaced")) success = false;
    return success;
}


static bool cached_account_matches(const std::string &login_hint);

static bool http_post(const WCHAR *host, const WCHAR *path, const std::string &body,
                      std::string &response)
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD status = 0, status_size = sizeof(status), available, read;
    bool success = false;
    response.clear();
    session = WinHttpOpen(L"Wine4Office-WAM/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"Wine4Office-WAM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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
    session = InternetOpenW(L"Wine4Office-WAM/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
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
    session = WinHttpOpen(L"Wine4Office-WAM/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"Wine4Office-WAM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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

struct cache_record
{
    token_set office, licensing;
    std::string username, oid, tid, first_name, last_name, display_name;
    std::string account_id, authority, client_info;
    ULONGLONG expires = 0;
};

static void secure_clear(cache_record &record)
{
    secure_clear(record.office);
    secure_clear(record.licensing);
    secure_clear(record.username);
    secure_clear(record.oid);
    secure_clear(record.tid);
    secure_clear(record.first_name);
    secure_clear(record.last_name);
    secure_clear(record.display_name);
    secure_clear(record.account_id);
    secure_clear(record.authority);
    secure_clear(record.client_info);
    record.expires = 0;
}
static bool publish_projection(const cache_record &record);
static bool clear_projection(void);

static bool same_account_string(const std::string &left, const std::string &right)
{
    std::wstring left_w = utf8_to_wide(left), right_w = utf8_to_wide(right);
    return !left_w.empty() && !right_w.empty() &&
           CompareStringOrdinal(left_w.c_str(), left_w.size(), right_w.c_str(), right_w.size(),
                                TRUE) == CSTR_EQUAL;
}

static std::wstring cache_bundle_name(const std::string &username)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    BYTE hash[32];
    WCHAR suffix[33];
    std::string normalized = username;

    if (normalized.empty()) return {};
    for (char &ch : normalized)
        if ((unsigned char)ch < 128) ch = (char)std::tolower((unsigned char)ch);
    if (!sha256(normalized, hash)) return {};
    for (unsigned int i = 0; i < 16; ++i)
    {
        suffix[2 * i] = hex[hash[i] >> 4];
        suffix[2 * i + 1] = hex[hash[i] & 0xf];
    }
    suffix[32] = 0;
    SecureZeroMemory(hash, sizeof(hash));
    return L"wam-bundle-" + std::wstring(suffix) + L".dat";
}

static std::string json_escape(const std::string &value)
{
    std::string result;
    static const char hex[] = "0123456789abcdef";
    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 15];
            }
            else result += ch;
        }
    }
    return result;
}

static std::string cache_record_json(const cache_record &record)
{
    std::string json = "{\"version\":1";
    auto add_string = [&](const char *name, const std::string &value)
    {
        json += ",\""; json += name; json += "\":\""; json += json_escape(value); json += '"';
    };
    auto add_number = [&](const char *name, ULONGLONG value)
    {
        json += ",\""; json += name; json += "\":"; json += std::to_string(value);
    };

    add_string("username", record.username);
    add_string("oid", record.oid);
    add_string("tid", record.tid);
    add_string("account_id", record.account_id);
    add_string("authority", record.authority);
    add_string("client_info", record.client_info);
    add_string("first_name", record.first_name);
    add_string("last_name", record.last_name);
    add_string("display_name", record.display_name);
    add_string("access_token", record.office.access_token);
    add_string("refresh_token", record.office.refresh_token);
    add_string("id_token", record.office.id_token);
    add_number("expires", record.expires);
    add_string("licensing_access_token", record.licensing.access_token);
    add_string("licensing_refresh_token", record.licensing.refresh_token);
    add_string("licensing_id_token", record.licensing.id_token);
    add_number("licensing_expires_in", record.licensing.expires_in);
    json += '}';
    return json;
}

static bool cache_record_from_json(const std::string &json, cache_record &record)
{
    ULONGLONG version;
    record = {};
    if (json.empty() || json.back() != '}' ||
        !json_number(json, "version", version) || version != 1 ||
        !json_string(json, "username", record.username) ||
        !json_string(json, "oid", record.oid) || !json_string(json, "tid", record.tid) ||
        !json_string(json, "account_id", record.account_id) ||
        !json_string(json, "authority", record.authority) ||
        !json_string(json, "client_info", record.client_info) ||
        !json_string(json, "access_token", record.office.access_token) ||
        !json_string(json, "refresh_token", record.office.refresh_token) ||
        !json_string(json, "id_token", record.office.id_token) ||
        !json_number(json, "expires", record.expires) ||
        !json_string(json, "licensing_access_token", record.licensing.access_token) ||
        !json_string(json, "licensing_refresh_token", record.licensing.refresh_token) ||
        !json_string(json, "licensing_id_token", record.licensing.id_token) ||
        !json_number(json, "licensing_expires_in", record.licensing.expires_in))
        return false;
    json_string(json, "first_name", record.first_name);
    json_string(json, "last_name", record.last_name);
    json_string(json, "display_name", record.display_name);
    return !record.username.empty() && !record.oid.empty() && !record.tid.empty();
}
static bool cache_record_identity_valid(const cache_record &record, const std::string &login_hint)
{
    std::string payload, oid, tid, username, client_info_json, expected_client_info;
    bool success = jwt_payload(record.office.id_token, payload) &&
                   json_string(payload, "oid", oid) && json_string(payload, "tid", tid) &&
                   (json_string(payload, "preferred_username", username) ||
                    json_string(payload, "upn", username));
    client_info_json = "{\"uid\":\"" + record.oid + "\",\"utid\":\"" + record.tid + "\"}";
    expected_client_info = base64url_encode((const BYTE *)client_info_json.data(),
                                            client_info_json.size());
    if (success)
        success = same_account_string(record.oid, oid) && same_account_string(record.tid, tid) &&
                  same_account_string(record.username, username) &&
                  record.account_id == record.oid + "." + record.tid &&
                  record.authority == "https://login.microsoftonline.com/" + record.tid + "/" &&
                  record.client_info == expected_client_info &&
                  (login_hint.empty() || same_account_string(record.username, login_hint));
    secure_clear(payload);
    secure_clear(oid);
    secure_clear(tid);
    secure_clear(username);
    secure_clear(client_info_json);
    secure_clear(expected_client_info);
    return success;
}

static bool recover_cache_transaction_locked(void)
{
    std::wstring marker = cache_file(L"wam-transaction.pending");
    LARGE_INTEGER size;
    HANDLE file;
    DWORD read;
    std::string content, name;
    std::wstring target, backup, temporary;
    bool success = true, transaction_replaced = false;

    if (marker.empty() || GetFileAttributesW(marker.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    file = CreateFileW(marker.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) &&
        size.QuadPart > 0 && size.QuadPart <= 512)
    {
        content.resize((size_t)size.QuadPart);
        if (!ReadFile(file, &content[0], content.size(), &read, NULL) || read != content.size())
            content.clear();
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (content.compare(0, strlen("Wine4OfficeWamBundle=1\n"), "Wine4OfficeWamBundle=1\n"))
    {
        DeleteFileW(marker.c_str());
        return true;
    }
    {
        size_t prefix_len = strlen("Wine4OfficeWamBundle=1\n"), name_end;
        name_end = content.find('\n', prefix_len);
        if (name_end == std::string::npos) return false;
        name = content.substr(prefix_len, name_end - prefix_len);
        transaction_replaced = content.compare(name_end + 1, strlen("replaced"), "replaced") == 0;
        if (name.compare(0, 11, "wam-bundle-") || name.size() != 11 + 32 + 4 ||
            name.compare(name.size() - 4, 4, ".dat"))
            return false;
        for (size_t i = 11; i < 11 + 32; ++i)
            if (!std::isxdigit((unsigned char)name[i])) return false;
        target = cache_file(utf8_to_wide(name).c_str());
        backup = target + L".bak";
        temporary = target + L".new";
        if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(target.c_str());
            success = MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        }
        else if (transaction_replaced) DeleteFileW(target.c_str());
        DeleteFileW(temporary.c_str());
    }
    DeleteFileW(temporary.c_str());
    if (success)
    {
        std::string json;
        cache_record record;
        if (protected_read_path(target, json, false) &&
            cache_record_from_json(json, record) &&
            cache_record_identity_valid(record, {}))
            success = publish_projection(record);
        else success = clear_projection();
        secure_clear(json);
        secure_clear(record);
    }
    if (success) success = DeleteFileW(marker.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    return success;
}

static bool recover_cache_transaction(void)
{
    cache_lock lock;
    return lock.valid() && recover_cache_transaction_locked();
}

static bool cache_record_load_locked(const std::string &login_hint, cache_record &record)
{
    std::string username = login_hint, active_username, json;
    std::wstring bundle;
    record = {};
    if (username.empty())
    {
        if (!protected_read_path(cache_file(L"wam-active-account.dat"), active_username, false))
            return false;
        username = active_username;
    }
    bundle = cache_file(cache_bundle_name(username).c_str());
    if (bundle.empty() || !protected_read_path(bundle, json, false) ||
        !cache_record_from_json(json, record) ||
        !cache_record_identity_valid(record, login_hint))
    {
        secure_clear(json);
        secure_clear(record);
        return false;
    }
    secure_clear(json);
    secure_clear(username);
    return true;
}

static bool cache_record_load(const std::string &login_hint, cache_record &record)
{
    cache_lock lock;
    if (!lock.valid() || !recover_cache_transaction_locked()) return false;
    return cache_record_load_locked(login_hint, record);
}

static bool publish_projection(const cache_record &record)
{
    std::string expires = std::to_string(record.expires);
    bool success =
        protected_write(L"wam-access-token.dat", record.office.access_token) &&
        protected_write(L"wam-id-token.dat", record.office.id_token) &&
        protected_write(L"wam-refresh-token.dat", record.office.refresh_token) &&
        protected_write(L"wam-licensing-token.dat", record.licensing.access_token) &&
        protected_write(L"wam-token-expires-on.dat", expires) &&
        protected_write(L"wam-account-username.dat", record.username) &&
        protected_write(L"wam-account-id.dat", record.account_id) &&
        protected_write(L"wam-account-oid.dat", record.oid) &&
        protected_write(L"wam-account-tenant-id.dat", record.tid) &&
        protected_write(L"wam-account-authority.dat", record.authority) &&
        protected_write(L"wam-client-info.dat", record.client_info);
    if (success) success = record.first_name.empty() ? delete_cache_file(L"wam-account-first-name.dat") :
                                                       protected_write(L"wam-account-first-name.dat", record.first_name);
    if (success) success = record.last_name.empty() ? delete_cache_file(L"wam-account-last-name.dat") :
                                                      protected_write(L"wam-account-last-name.dat", record.last_name);
    if (success) success = record.display_name.empty() ? delete_cache_file(L"wam-account-display-name.dat") :
                                                         protected_write(L"wam-account-display-name.dat", record.display_name);
    return success && protected_write(L"wam-active-account.dat", record.username);
}

static bool clear_projection(void)
{
    bool success = true;
    static const WCHAR *const names[] =
    {
        L"wam-access-token.dat", L"wam-id-token.dat", L"wam-refresh-token.dat",
        L"wam-licensing-token.dat", L"wam-token-expires-on.dat",
        L"wam-account-username.dat", L"wam-account-id.dat", L"wam-account-oid.dat",
        L"wam-account-tenant-id.dat", L"wam-account-authority.dat", L"wam-client-info.dat",
        L"wam-account-first-name.dat", L"wam-account-last-name.dat",
        L"wam-account-display-name.dat", L"wam-active-account.dat"
    };
    for (const WCHAR *name : names)
        if (!delete_cache_file(name)) success = false;
    return success;
}

static bool cache_record_save(const cache_record &record)
{
    std::wstring bundle_name = cache_bundle_name(record.username);
    std::wstring bundle = cache_file(bundle_name.c_str());
    cache_transaction transaction;
    std::string bundle_json;
    bool success;

    if (bundle_name.empty() || bundle.empty() ||
        !cache_record_identity_valid(record, {}) || !transaction.begin(bundle))
        return false;
    bundle_json = cache_record_json(record);
    success = transaction.replace_bundle(bundle_json);
    secure_clear(bundle_json);
    if (!success)
    {
        transaction.rollback();
        return false;
    }
    if (!publish_projection(record)) return false;
    if (!transaction.commit())
    {
        transaction.rollback();
        return false;
    }
    return true;
}

static bool cached_account_matches(const std::string &login_hint)
{
    cache_record record;
    bool success = login_hint.empty() || cache_record_load(login_hint, record);
    secure_clear(record);
    return success;
}

static bool save_tokens(const token_set &office, const token_set &licensing,
                        const std::string &login_hint = {})
{
    cache_record record;
    std::string payload, oid, tid, username, client_info_json;
    ULONGLONG expires;
    bool success;

    if (!jwt_payload(office.id_token, payload) ||
        !json_string(payload, "oid", oid) || !json_string(payload, "tid", tid) ||
        (!json_string(payload, "preferred_username", username) &&
         !json_string(payload, "upn", username)) ||
        (!login_hint.empty() && !same_account_string(login_hint, username)))
    {
        secure_clear(payload);
        secure_clear(oid);
        secure_clear(tid);
        secure_clear(username);
        return false;
    }
    json_string(payload, "given_name", record.first_name);
    json_string(payload, "family_name", record.last_name);
    json_string(payload, "name", record.display_name);
    if (!json_number(payload, "exp", expires)) expires = unix_time() + office.expires_in;
    record.office = office;
    record.licensing = licensing;
    record.username = username;
    record.oid = oid;
    record.tid = tid;
    record.expires = expires;
    record.account_id = oid + "." + tid;
    record.authority = "https://login.microsoftonline.com/" + tid + "/";
    client_info_json = "{\"uid\":\"" + oid + "\",\"utid\":\"" + tid + "\"}";
    record.client_info = base64url_encode((const BYTE *)client_info_json.data(), client_info_json.size());
    success = cache_record_save(record);
    secure_clear(payload);
    secure_clear(oid);
    secure_clear(tid);
    secure_clear(username);
    secure_clear(client_info_json);
    secure_clear(record.office);
    secure_clear(record.licensing);
    secure_clear(record.username);
    secure_clear(record.oid);
    secure_clear(record.tid);
    secure_clear(record.account_id);
    secure_clear(record.authority);
    secure_clear(record.client_info);
    secure_clear(record.first_name);
    secure_clear(record.last_name);
    secure_clear(record.display_name);
    return success;
}

static bool exchange_and_save(const std::string &code, const std::string &verifier,
                              const std::string &login_hint)
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
    if (success) success = save_tokens(office, licensing, login_hint);
    secure_clear(body);
    secure_clear(response);
    secure_clear(office);
    secure_clear(licensing);
    return success;
}

static bool refresh_and_save(const std::string &login_hint)
{
    cache_record previous_record;
    token_set office, licensing;
    bool success = cache_record_load(login_hint, previous_record);

    if (success)
    {
        office.refresh_token = previous_record.office.refresh_token;
        office.id_token = previous_record.office.id_token;
        success = refresh_scope(office.refresh_token, office_scope, office, &office);
    }
    if (success) success = refresh_scope(office.refresh_token, licensing_scope, licensing, &office);
    if (success && !licensing.refresh_token.empty()) office.refresh_token = licensing.refresh_token;
    if (success) success = save_tokens(office, licensing, login_hint.empty() ?
                                       previous_record.username : login_hint);
    secure_clear(previous_record);
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

static bool refresh_resource_and_save(const std::string &requested_scope,
                                      const std::string &requested_client_id,
                                      const std::string &login_hint)
{
    cache_record account;
    token_set resource;
    std::string scope = normalize_resource_scope(requested_scope);
    bool success = !scope.empty() &&
        (requested_client_id == client_id || requested_client_id == teams_client_id ||
         requested_client_id == teams_nested_client_id) &&
        cache_record_load(login_hint, account);

    if (success)
    {
        std::string oidc_scope = scope + " offline_access openid profile";
        success = refresh_scope(account.office.refresh_token, oidc_scope.c_str(), resource, NULL,
                                requested_client_id.c_str());
        secure_clear(oidc_scope);
    }
    if (success)
    {
        std::string payload, audience;
        success = jwt_payload(resource.id_token, payload) &&
                  json_string(payload, "aud", audience) && audience == requested_client_id &&
                  cache_record_identity_valid(account, login_hint);
        secure_clear(payload);
        secure_clear(audience);
    }
    if (success)
    {
        account.office.access_token = resource.access_token;
        account.office.id_token = resource.id_token;
        if (!resource.refresh_token.empty()) account.office.refresh_token = resource.refresh_token;
        account.expires = unix_time() + resource.expires_in;
        success = cache_record_save(account);
    }
    secure_clear(scope);
    secure_clear(account);
    secure_clear(resource);
    return success;
}

static bool exchange_resource_and_save(const std::string &code, const std::string &verifier,
                                       const std::string &requested_scope,
                                       const std::string &requested_client_id,
                                       const std::string &requested_redirect_uri,
                                       const std::string &login_hint)
{
    token_set resource;
    std::string scope = normalize_resource_scope(requested_scope);
    std::string oidc_scope = scope + " offline_access openid profile";
    std::string response;
    std::string body = "client_id=" + requested_client_id +
        "&grant_type=authorization_code&code=" + url_encode(code) +
        "&redirect_uri=" + url_encode(requested_redirect_uri) +
        "&code_verifier=" + url_encode(verifier) + "&scope=" + url_encode(oidc_scope);
    bool success = !scope.empty() && requested_client_id == teams_nested_client_id &&
                   token_request(body, response, oauth_tenant) && parse_token_set(response, resource);

    if (success)
    {
        std::string payload, audience;
        success = jwt_payload(resource.id_token, payload) &&
                  json_string(payload, "aud", audience) && audience == requested_client_id;
        secure_clear(payload);
        secure_clear(audience);
    }
    if (success) success = save_tokens(resource, resource, login_hint);
    secure_clear(scope);
    secure_clear(oidc_scope);
    secure_clear(body);
    secure_clear(response);
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

static void restore_owner_window(HWND owner)
{
    if (!owner) return;
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

class owner_window_restore
{
    HWND owner;
    bool armed = false;

public:
    explicit owner_window_restore(HWND window) : owner(window) {}
    void disable()
    {
        if (owner)
        {
            EnableWindow(owner, FALSE);
            armed = true;
        }
    }
    ~owner_window_restore()
    {
        if (armed) restore_owner_window(owner);
    }
};

static bool run_owned_oauth(HINSTANCE instance, HWND owner, const std::string &login_hint,
                            const std::string &requested_client_id,
                            const std::string &requested_scope,
                            const std::string &requested_redirect_uri, std::string &verifier)
{
    owner_window_restore owner_state(owner);
    static const WCHAR class_name[] = L"Wine4OfficeOAuthBroker";
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
        "/oauth2/v2.0/authorize?client_id=" + requested_client_id +
        "&response_type=code&response_mode=query&redirect_uri=" +
        url_encode(requested_redirect_uri) + "&scope=" + url_encode(requested_scope) +
        "&code_challenge=" + challenge + "&code_challenge_method=S256&state=" + oauth_state;
    if (!login_hint.empty()) authorize_url += "&login_hint=" + url_encode(login_hint);

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
    owner_state.disable();

    site = new BrowserSite(host_window);
    hr = CoCreateInstance(CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER,
                          IID_IOleObject, (void **)&ole_object);
    if (FAILED(hr)) { site->Release(); DestroyWindow(host_window); restore_owner_window(owner); return false; }
    hr = ole_object->SetClientSite(site);
    if (FAILED(hr)) { site->Release(); DestroyWindow(host_window); restore_owner_window(owner); return false; }
    hr = OleSetContainedObject(ole_object, TRUE);
    if (FAILED(hr)) { site->Release(); DestroyWindow(host_window); restore_owner_window(owner); return false; }
    if (!GetClientRect(host_window, &rect))
    { site->Release(); DestroyWindow(host_window); restore_owner_window(owner); return false; }
    hr = ole_object->DoVerb(OLEIVERB_SHOW, NULL, site, 0, host_window, &rect);
    site->Release();
    if (FAILED(hr)) { DestroyWindow(host_window); restore_owner_window(owner); return false; }
    if (FAILED(ole_object->QueryInterface(IID_IOleInPlaceObject, (void **)&inplace_object)) ||
        FAILED(ole_object->QueryInterface(IID_IWebBrowser2, (void **)&browser)))
    { DestroyWindow(host_window); restore_owner_window(owner); return false; }
    resize_browser(host_window);
    {
        IConnectionPointContainer *container = NULL;
        BrowserEvents *events = new BrowserEvents();
        hr = browser->QueryInterface(IID_IConnectionPointContainer, (void **)&container);
        if (SUCCEEDED(hr)) hr = container->FindConnectionPoint(DIID_DWebBrowserEvents2,
                                                                 &browser_connection);
        if (SUCCEEDED(hr)) hr = browser_connection->Advise(events, &browser_connection_cookie);
        if (container) container->Release();
        events->Release();
        if (FAILED(hr))
        {
            if (browser_connection)
            {
                browser_connection->Release();
                browser_connection = NULL;
            }
            DestroyWindow(host_window);
            restore_owner_window(owner);
            return false;
        }
    }
    protocol_factory = new CallbackProtocolFactory();
    if (!protocol_factory)
    {
        DestroyWindow(host_window);
        restore_owner_window(owner);
        return false;
    }
    hr = CoInternetGetSession(0, &internet_session, 0);
    if (SUCCEEDED(hr))
        hr = internet_session->RegisterNameSpace(protocol_factory, CLSID_NULL,
                                                  L"ms-appx-web", 0, NULL, 0);
    if (FAILED(hr))
    {
        if (internet_session) internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        restore_owner_window(owner);
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
    if (!url)
    {
        internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
        internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        restore_owner_window(owner);
        return false;
    }
    hr = browser->Navigate(url, &empty, &empty, &empty, &empty);
    SysFreeString(url);
    if (FAILED(hr))
    {
        internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
        internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        restore_owner_window(owner);
        return false;
    }
    ShowWindow(host_window, SW_SHOW);
    UpdateWindow(host_window);
    SetForegroundWindow(host_window);
    if (!SetTimer(host_window, 1, 250, NULL))
    {
        internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
        internet_session->Release();
        protocol_factory->Release();
        DestroyWindow(host_window);
        restore_owner_window(owner);
        return false;
    }
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    internet_session->UnregisterNameSpace(protocol_factory, L"ms-appx-web");
    internet_session->Release();
    protocol_factory->Release();
    restore_owner_window(owner);
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

static std::string self_test_id_token(const std::string &username, const std::string &oid,
                                      const std::string &tid)
{
    std::string header = "{}", payload = "{\"oid\":\"" + oid + "\",\"tid\":\"" + tid +
                         "\",\"preferred_username\":\"" + username + "\"}";
    return base64url_encode((const BYTE *)header.data(), header.size()) + "." +
           base64url_encode((const BYTE *)payload.data(), payload.size()) + ".test";
}

static void self_test_record(cache_record &record, const char *username, const char *oid,
                             const char *tid, const char *suffix)
{
    std::string client_info_json;
    record = {};
    record.username = username;
    record.oid = oid;
    record.tid = tid;
    record.account_id = record.oid + "." + record.tid;
    record.authority = "https://login.microsoftonline.com/" + record.tid + "/";
    client_info_json = "{\"uid\":\"" + record.oid + "\",\"utid\":\"" + record.tid + "\"}";
    record.client_info = base64url_encode((const BYTE *)client_info_json.data(),
                                          client_info_json.size());
    secure_clear(client_info_json);
    record.office.access_token = std::string("access-") + suffix;
    record.office.refresh_token = std::string("refresh-") + suffix;
    record.office.id_token = self_test_id_token(record.username, record.oid, record.tid);
    record.licensing.access_token = std::string("license-") + suffix;
    record.licensing.refresh_token = record.office.refresh_token;
    record.licensing.id_token = record.office.id_token;
    record.expires = unix_time() + 3600;
}

struct cache_lock_probe
{
    HANDLE started;
    bool blocked;
};

static DWORD WINAPI cache_lock_probe_thread(void *param)
{
    cache_lock_probe *probe = (cache_lock_probe *)param;
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\Wine4OfficeWamCache");
    DWORD wait;

    if (!mutex)
    {
        probe->blocked = false;
        SetEvent(probe->started);
        return 0;
    }
    SetEvent(probe->started);
    wait = WaitForSingleObject(mutex, 0);
    probe->blocked = wait == WAIT_TIMEOUT;
    if (wait == WAIT_OBJECT_0) ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command_line, int show)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string verifier, login_hint, resource_scope, resource_client_id, resource_redirect_uri;
    bool success;
    (void)previous; (void)command_line; (void)show;
    if (!argv) return 3;
    if (argc >= 2 && !wcscmp(argv[1], L"--self-test-cache"))
    {
        bool recovered = recover_cache_transaction();
        cache_record first, second, loaded;
        std::wstring first_bundle, second_bundle;
        std::string mismatch;

        self_test_record(first, "first@example.invalid", "11111111-1111-1111-1111-111111111111",
                         "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", "first");
        self_test_record(second, "second@example.invalid", "22222222-2222-2222-2222-222222222222",
                         "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", "second");
        first_bundle = cache_bundle_name(first.username);
        second_bundle = cache_bundle_name(second.username);
        DeleteFileW(cache_file(first_bundle.c_str()).c_str());
        DeleteFileW(cache_file(second_bundle.c_str()).c_str());
        success = recovered && cache_record_save(first) && cache_record_save(second);
        if (success) success = cache_record_load(first.username, loaded) &&
                               loaded.office.refresh_token == first.office.refresh_token;
        if (success) success = cache_record_load(second.username, loaded) &&
                               loaded.office.refresh_token == second.office.refresh_token;
        mismatch = "third@example.invalid";
        if (success) success = !cache_record_load(mismatch, loaded);
        if (success)
        {
            cache_lock held;
            cache_lock_probe probe = {};
            HANDLE thread, started = CreateEventW(NULL, TRUE, FALSE, NULL);
            probe.started = started;
            thread = started ? CreateThread(NULL, 0, cache_lock_probe_thread, &probe, 0, NULL) : NULL;
            if (thread)
            {
                WaitForSingleObject(started, INFINITE);
                WaitForSingleObject(thread, INFINITE);
                CloseHandle(thread);
            }
            if (started) CloseHandle(started);
            success = held.valid() && thread != NULL && probe.blocked;
        }
        if (success)
        {
            protected_write(first_bundle.c_str(), "corrupt bundle");
            success = !cache_record_load(first.username, loaded) && cache_record_save(first);
        }
        if (success)
        {
            {
                cache_transaction interrupted;
                success = interrupted.begin(cache_file(first_bundle.c_str())) &&
                          interrupted.replace_bundle("partial bundle");
            }
            if (success) success = cache_record_load(first.username, loaded) &&
                                   loaded.office.refresh_token == first.office.refresh_token;
        }
        DeleteFileW(cache_file(first_bundle.c_str()).c_str());
        DeleteFileW(cache_file(second_bundle.c_str()).c_str());
        success = clear_projection() && success;
        DeleteFileW(cache_file(L"wam-transaction.pending").c_str());
        SecureZeroMemory((void *)mismatch.data(), mismatch.size());
        secure_clear(first);
        secure_clear(second);
        secure_clear(loaded);
        LocalFree(argv);
        return success ? 0 : 3;
    }
    if (argc >= 2 && !wcscmp(argv[1], L"--refresh"))
    {
        success = refresh_and_save({});
        LocalFree(argv);
        return success ? 0 : 3;
    }
    if (argc >= 4 && !wcscmp(argv[1], L"--refresh-resource"))
    {
        std::string scope = wide_to_utf8(argv[2]);
        std::string requested_client_id = wide_to_utf8(argv[3]);
        success = refresh_resource_and_save(scope, requested_client_id, {});
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
        else if (!wcscmp(argv[i], L"--resource-scope") && i + 1 < argc)
            resource_scope = wide_to_utf8(argv[++i]);
        else if (!wcscmp(argv[i], L"--resource-client-id") && i + 1 < argc)
            resource_client_id = wide_to_utf8(argv[++i]);
        else if (!wcscmp(argv[i], L"--resource-redirect-uri") && i + 1 < argc)
            resource_redirect_uri = wide_to_utf8(argv[++i]);
    }
    LocalFree(argv);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 3;
    OleInitialize(NULL);
    bool resource_mode = !resource_scope.empty() && !resource_client_id.empty() &&
                         !resource_redirect_uri.empty();
    if (cached_account_matches(login_hint))
    {
        success = resource_mode ? refresh_resource_and_save(resource_scope, resource_client_id, login_hint) :
                                  refresh_and_save(login_hint);
        if (success)
        {
            OleUninitialize();
            CoUninitialize();
            return 0;
        }
    }
    success = run_owned_oauth(instance, owner_window, login_hint,
                              resource_mode ? resource_client_id : client_id,
                              resource_mode ? resource_scope + " offline_access openid profile" : office_scope,
                              resource_mode ? resource_redirect_uri : redirect_uri, verifier);
    if (success)
        success = resource_mode ? exchange_resource_and_save(oauth_code, verifier, resource_scope,
                                                              resource_client_id, resource_redirect_uri,
                                                              login_hint) :
                                  exchange_and_save(oauth_code, verifier, login_hint);
    SecureZeroMemory((void *)oauth_code.data(), oauth_code.size());
    SecureZeroMemory((void *)verifier.data(), verifier.size());
    secure_clear(resource_scope);
    secure_clear(resource_client_id);
    secure_clear(resource_redirect_uri);
    OleUninitialize();
    CoUninitialize();
    if (!success && !oauth_cancelled)
        MessageBoxW(owner_window, L"Microsoft 365 sign-in could not be completed. Please try again.",
                    L"Microsoft 365", MB_OK | MB_ICONERROR);
    return success ? 0 : oauth_cancelled ? 2 : 3;
}
