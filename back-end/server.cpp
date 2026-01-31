#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "history_store.hpp"
#include "namegen.hpp"

using namespace std;

static std::unique_ptr<HistoryStore> g_history;
static std::string g_history_init_error;

static bool file_exists(const string& path) {
    ifstream in(path, ios::binary);
    return static_cast<bool>(in);
}

static string read_file(const string& path) {
    ifstream in(path, ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static string detect_frontend_root() {
    // Support running from repo root OR from back-end/ directory.
    // Prefer repo-root layout.
    if (file_exists("front-end/index.html")) return "front-end";
    if (file_exists("../front-end/index.html")) return "../front-end";
    return "front-end"; // fallback
}

static string http_date_now() {
    // Minimal; browsers don't require a correct Date header for local dev.
    return "Sat, 01 Jan 2000 00:00:00 GMT";
}

static string content_type_for_path(const string& path) {
    auto dot = path.find_last_of('.');
    string ext = (dot == string::npos) ? "" : path.substr(dot + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    return "text/plain; charset=utf-8";
}

static string json_escape(const string& s) {
    string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    // Control chars -> \u00XX
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

static string normalize_method(const string& m) {
    string out;
    out.reserve(m.size());
    for (unsigned char c : m) {
        if (std::isalpha(c)) out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

static unordered_map<string, string> parse_query(const string& query) {
    unordered_map<string, string> out;
    size_t i = 0;
    while (i < query.size()) {
        size_t amp = query.find('&', i);
        if (amp == string::npos) amp = query.size();
        string part = query.substr(i, amp - i);
        size_t eq = part.find('=');
        if (eq == string::npos) {
            out[part] = "";
        } else {
            out[part.substr(0, eq)] = part.substr(eq + 1);
        }
        i = amp + 1;
    }
    return out;
}

// -----------------------------
// HTTP handling
// -----------------------------
struct HttpResponse {
    int status = 200;
    string content_type = "text/plain; charset=utf-8";
    string body;
    unordered_map<string, string> headers;
};

static string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

static HttpResponse handle_request(const string& method, const string& target) {
    HttpResponse res;
    res.headers["Cache-Control"] = "no-store";
    res.headers["Access-Control-Allow-Origin"] = "*";
    res.headers["Access-Control-Allow-Methods"] = "GET, HEAD";

    const string m = normalize_method(method);
    const bool is_get = (m == "GET");
    const bool is_head = (m == "HEAD");
    if (!is_get && !is_head) {
        res.status = 405;
        res.body = "Method Not Allowed\n";
        return res;
    }

    string path = target;
    string query;
    if (auto q = target.find('?'); q != string::npos) {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }

    if (path == "/api/generate") {
        auto params = parse_query(query);
        int count = 0;
        try {
            count = stoi(params["count"]);
        } catch (...) {
            count = 0;
        }

        if (!g_history || !g_history_init_error.empty()) {
            res.status = 500;
            res.content_type = "application/json; charset=utf-8";
            std::ostringstream err;
            err << "{\"error\":\"history store unavailable: " << json_escape(g_history_init_error) << "\"}";
            res.body = err.str();
            return res;
        }

        const int remaining = g_history->remaining_unique();
        if (count <= 0 || count > remaining) {
            res.status = 400;
            res.content_type = "application/json; charset=utf-8";
            std::ostringstream err;
            err << "{\"error\":\"count must be an integer between 1 and " << remaining << "\"}";
            res.body = err.str();
            return res;
        }

        std::vector<std::string> names;
        auto gen_err = g_history->generate_and_mark(count, names);
        if (!gen_err.empty()) {
            res.status = 500;
            res.content_type = "application/json; charset=utf-8";
            std::ostringstream err;
            err << "{\"error\":\"" << json_escape(gen_err) << "\"}";
            res.body = err.str();
            return res;
        }

        ostringstream ss;
        ss << "{\"names\":[";
        for (size_t i = 0; i < names.size(); i++) {
            if (i) ss << ",";
            ss << "\"" << json_escape(names[i]) << "\"";
        }
        ss << "]}";

        res.content_type = "application/json; charset=utf-8";
        res.body = is_head ? "" : ss.str();
        return res;
    }

    // Static files
    const string frontend_root = detect_frontend_root();

    string rel = path;
    if (rel == "/" || rel == "") rel = "/index.html";
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1); // strip leading '/'

    string file_path = frontend_root + "/" + rel;

    // Basic path traversal guard
    if (file_path.find("..") != string::npos) {
        res.status = 400;
        res.body = "Bad Request\n";
        return res;
    }

    string body = read_file(file_path);
    if (body.empty()) {
        res.status = 404;
        res.body = "Not Found\n";
        return res;
    }

    res.content_type = content_type_for_path(file_path);
    res.body = is_head ? "" : body;
    return res;
}

static string build_http_response(const HttpResponse& r) {
    ostringstream ss;
    ss << "HTTP/1.1 " << r.status << " " << status_text(r.status) << "\r\n";
    ss << "Date: " << http_date_now() << "\r\n";
    ss << "Connection: close\r\n";
    ss << "Content-Type: " << r.content_type << "\r\n";
    ss << "Content-Length: " << r.body.size() << "\r\n";
    for (const auto& [k, v] : r.headers) ss << k << ": " << v << "\r\n";
    ss << "\r\n";
    ss << r.body;
    return ss.str();
}

static bool read_until_headers_end(int fd, string& out) {
    out.clear();
    char buf[4096];
    while (out.find("\r\n\r\n") == string::npos) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        out.append(buf, buf + n);
        if (out.size() > 64 * 1024) return false;  // prevent abuse
    }
    return true;
}

int main(int argc, char** argv) {
    int port = 8080;
    if (const char* env_port = getenv("PORT"); env_port && *env_port) {
        port = atoi(env_port);
    }
    if (argc >= 2) port = atoi(argv[1]);
    if (port <= 0) port = 8080;

    // Global history store (encrypted on disk).
    {
        const char* env_file = getenv("HISTORY_FILE");
        std::string file_path = env_file && *env_file ? std::string(env_file) : std::string("data/history.bin");
        g_history = std::make_unique<HistoryStore>(file_path);
        g_history_init_error = g_history->init();
        if (!g_history_init_error.empty()) {
            cerr << "History store init failed: " << g_history_init_error << "\n";
        } else {
            cerr << "History store ready. Total unique: " << g_history->total_unique()
                 << ", remaining: " << g_history->remaining_unique()
                 << ", file: " << file_path << "\n";
        }
    }

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        cerr << "bind() failed: " << strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    if (::listen(server_fd, 16) != 0) {
        cerr << "listen() failed: " << strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    cout << "C++ server running on http://127.0.0.1:" << port << "\n";
    cout << "API: GET /api/generate?count=10\n";

    while (true) {
        int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        string raw;
        if (!read_until_headers_end(client_fd, raw)) {
            ::close(client_fd);
            continue;
        }

        // Request line: METHOD SP TARGET SP HTTP/1.1
        size_t line_end = raw.find("\r\n");
        string req_line = (line_end == string::npos) ? raw : raw.substr(0, line_end);
        istringstream rl(req_line);
        string method, target, version;
        rl >> method >> target >> version;

        HttpResponse res;
        if (method.empty() || target.empty()) {
            res.status = 400;
            res.body = "Bad Request\n";
        } else {
            try {
                res = handle_request(method, target);
            } catch (...) {
                res.status = 500;
                res.body = "Internal Server Error\n";
            }
        }

        string response = build_http_response(res);
        (void)::send(client_fd, response.data(), response.size(), 0);
        ::close(client_fd);
    }
}

