#include "history_store.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <zlib.h>

#include "namegen.hpp"

using std::string;
using std::vector;

static bool file_exists(const string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

static string read_all_bytes(const string& path, vector<uint8_t>& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return "could not open history file for reading";
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0) return "could not read history file size";
    if (size > 100 * 1024 * 1024) return "history file too large";
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!out.empty() && !in.read(reinterpret_cast<char*>(out.data()), size)) {
        return "could not read history file";
    }
    return "";
}

static string write_all_bytes_atomic(const string& path, const vector<uint8_t>& bytes) {
    const string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return "could not open temp history file for writing";
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out) return "failed while writing temp history file";
        }
        out.flush();
        if (!out) return "failed while flushing temp history file";
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        return string("rename() failed: ") + std::strerror(errno);
    }
    return "";
}

static void mkdirs_for_path(const string& path) {
    auto slash = path.find_last_of('/');
    if (slash == string::npos) return;
    string dir = path.substr(0, slash);
    if (dir.empty()) return;

    size_t pos = 0;
    while (true) {
        size_t next = dir.find('/', pos);
        string part = (next == string::npos) ? dir : dir.substr(0, next);
        if (!part.empty()) (void)::mkdir(part.c_str(), 0755);
        if (next == string::npos) break;
        pos = next + 1;
    }
}

static bool get_bit(const vector<uint8_t>& bits, size_t i) {
    return (bits[i / 8] >> (i % 8)) & 1u;
}

static void set_bit(vector<uint8_t>& bits, size_t i) {
    bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
}

static std::mt19937 seeded_rng() {
    std::random_device rd;
    auto now = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::seed_seq seq{rd(), rd(), rd(), now, now ^ 0x9e3779b9U};
    return std::mt19937(seq);
}

// Minimal base64 (standard alphabet, padding '=')
static const char* B64_ALPH = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static string base64_encode_bytes(const vector<uint8_t>& in) {
    string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < in.size()) {
        uint32_t a = in[i++];
        uint32_t b = (i < in.size()) ? in[i++] : 0;
        uint32_t c = (i < in.size()) ? in[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(B64_ALPH[(triple >> 18) & 0x3F]);
        out.push_back(B64_ALPH[(triple >> 12) & 0x3F]);
        out.push_back((i - 1 <= in.size()) ? B64_ALPH[(triple >> 6) & 0x3F] : '=');
        out.push_back((i <= in.size()) ? B64_ALPH[triple & 0x3F] : '=');
    }
    // Fix padding when input length isn't multiple of 3
    size_t mod = in.size() % 3;
    if (mod == 1) {
        out[out.size() - 2] = '=';
        out[out.size() - 1] = '=';
    } else if (mod == 2) {
        out[out.size() - 1] = '=';
    }
    return out;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static string base64_decode_bytes(const string& b64, vector<uint8_t>& out) {
    out.clear();
    string s;
    s.reserve(b64.size());
    for (unsigned char c : b64) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        s.push_back(static_cast<char>(c));
    }
    if (s.size() % 4 != 0) return "invalid base64 length";

    size_t pad = 0;
    if (!s.empty() && s[s.size() - 1] == '=') pad++;
    if (s.size() >= 2 && s[s.size() - 2] == '=') pad++;

    out.reserve((s.size() / 4) * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        int v0 = b64_val(s[i]);
        int v1 = b64_val(s[i + 1]);
        int v2 = (s[i + 2] == '=') ? 0 : b64_val(s[i + 2]);
        int v3 = (s[i + 3] == '=') ? 0 : b64_val(s[i + 3]);
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return "invalid base64 character";
        uint32_t triple = (static_cast<uint32_t>(v0) << 18) |
                          (static_cast<uint32_t>(v1) << 12) |
                          (static_cast<uint32_t>(v2) << 6) |
                          static_cast<uint32_t>(v3);
        out.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
        if (s[i + 2] != '=') out.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
        if (s[i + 3] != '=') out.push_back(static_cast<uint8_t>(triple & 0xFF));
    }
    return "";
}

static std::string trim_ascii_whitespace(std::string s) {
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static size_t min_history_blob_size() {
    // magic(5) + ver(1) + u32 size + u64 fp + u32 raw_len + u32 comp_len + comp bytes
    return 5 + 1 + 4 + 8 + 4 + 4;
}

// -------------------------
// Minimal GitHub API helpers (libcurl)
// -------------------------
struct CurlBuf {
    std::string body;
    std::string etag;
    long status = 0;
};

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* b = reinterpret_cast<CurlBuf*>(userdata);
    b->body.append(ptr, size * nmemb);
    return size * nmemb;
}

static size_t curl_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* b = reinterpret_cast<CurlBuf*>(userdata);
    std::string line(buffer, size * nitems);
    const std::string k = "ETag:";
    if (line.size() >= k.size() && std::equal(k.begin(), k.end(), line.begin(),
                                             [](char a, char c) { return std::tolower(a) == std::tolower(c); })) {
        auto v = line.substr(k.size());
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
        b->etag = v;
    }
    return size * nitems;
}

static string json_escape(const string& s) {
    std::string out;
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

static string json_unescape_string(const string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c != '\\' || i + 1 >= s.size()) {
            out.push_back(c);
            continue;
        }
        char n = s[++i];
        switch (n) {
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(n); break;
        }
    }
    return out;
}

static string gist_extract_file_content(const string& json, const string& filename, string& out_content) {
    out_content.clear();
    const string key = "\"" + filename + "\"";
    size_t pos = json.find(key);
    if (pos == string::npos) return ""; // missing file -> treat as empty
    size_t cpos = json.find("\"content\"", pos);
    if (cpos == string::npos) return "gist JSON missing content field";
    size_t colon = json.find(':', cpos);
    if (colon == string::npos) return "gist JSON malformed near content";
    size_t q1 = json.find('\"', colon);
    if (q1 == string::npos) return "gist JSON malformed (content not string)";
    q1++;
    size_t q2 = q1;
    while (true) {
        q2 = json.find('\"', q2);
        if (q2 == string::npos) return "gist JSON malformed (unterminated content string)";
        size_t bs = 0;
        for (size_t k = q2; k > q1 && json[k - 1] == '\\'; k--) bs++;
        if ((bs % 2) == 0) break;
        q2++;
    }
    out_content = json_unescape_string(json.substr(q1, q2 - q1));
    return "";
}

static string http_request(
    const string& method,
    const string& url,
    const string& token,
    const string& body,
    const string& if_match_etag,
    CurlBuf& out) {

    out = {};
    CURL* c = curl_easy_init();
    if (!c) return "curl init failed";

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "RandomNameGenerator/1.0");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!token.empty()) {
        // GitHub accepts:
        // - Classic PATs: "Authorization: token <TOKEN>"
        // - Fine-grained PATs: "Authorization: Bearer <TOKEN>"
        // We'll choose based on the token prefix (best-effort).
        const bool looks_like_classic =
            (token.rfind("ghp_", 0) == 0) || (token.rfind("gho_", 0) == 0) ||
            (token.rfind("ghu_", 0) == 0) || (token.rfind("ghs_", 0) == 0) ||
            (token.rfind("ghr_", 0) == 0);
        const string auth = looks_like_classic ? ("Authorization: token " + token)
                                               : ("Authorization: Bearer " + token);
        headers = curl_slist_append(headers, auth.c_str());
    }
    if (!if_match_etag.empty()) headers = curl_slist_append(headers, ("If-Match: " + if_match_etag).c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);

    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        string err = curl_easy_strerror(rc);
        curl_slist_free_all(headers);
        curl_easy_cleanup(c);
        return "curl request failed: " + err;
    }

    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return "";
}

// -------------------------
// HistoryStore
// -------------------------
HistoryStore::HistoryStore(std::string file_path) : file_path_(std::move(file_path)) {}

std::string HistoryStore::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char* gist = std::getenv("HISTORY_GIST_ID");
    const char* tok = std::getenv("HISTORY_GITHUB_TOKEN");
    if (gist && *gist && tok && *tok) {
        backend_ = Backend::GitHubGist;
        gist_id_ = gist;
        github_token_ = tok;
        const char* fn = std::getenv("HISTORY_GIST_FILENAME");
        gist_filename_ = (fn && *fn) ? std::string(fn) : std::string("history.bin.b64");
        auto gerr = gist_init();
        if (!gerr.empty()) return gerr;
    } else {
        backend_ = Backend::File;
    }

    auto err = load_or_init_empty();
    if (!err.empty()) return err;

    ready_ = true;
    return "";
}

int HistoryStore::total_unique() const {
    const auto n = namegen::universe_size();
    if (n > static_cast<size_t>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(n);
}

int HistoryStore::remaining_unique() const {
    if (!ready_) return 0;
    const size_t n = namegen::universe_size();
    const size_t remaining = (used_count_ > n) ? 0 : (n - used_count_);
    const size_t cap = static_cast<size_t>(namegen::kMaxCount);
    const size_t r = remaining < cap ? remaining : cap;
    if (r > static_cast<size_t>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(r);
}

std::string HistoryStore::load_or_init_empty() {
    const size_t n = namegen::universe_size();
    used_bits_.assign((n + 7) / 8, 0);
    used_count_ = 0;

    vector<uint8_t> blob;
    if (backend_ == Backend::File) {
        if (!file_exists(file_path_)) return persist();
        auto rerr = read_all_bytes(file_path_, blob);
        if (!rerr.empty()) return rerr;
    } else {
        std::string content_b64;
        auto rerr = gist_read_content(content_b64);
        if (!rerr.empty()) return rerr;
        content_b64 = trim_ascii_whitespace(content_b64);
        if (content_b64.empty() || content_b64 == "init") return persist();
        auto derr = base64_decode_bytes(content_b64, blob);
        if (!derr.empty()) return derr;
        // If the gist contained something like "init" (which is valid-ish base64) or otherwise
        // tiny junk, treat it as "uninitialized" and overwrite with a real encrypted blob.
        if (blob.size() < min_history_blob_size()) return persist();
    }
    return decode_from_blob(blob);
}

std::string HistoryStore::persist() {
    vector<uint8_t> blob;
    auto eerr = encode_to_blob(blob);
    if (!eerr.empty()) return eerr;

    if (backend_ == Backend::File) {
        mkdirs_for_path(file_path_);
        return write_all_bytes_atomic(file_path_, blob);
    }
    return gist_write_content(base64_encode_bytes(blob));
}

std::string HistoryStore::generate_and_mark(int count, std::vector<std::string>& out_names) {
    out_names.clear();
    if (!ready_) return "history store not initialized";
    if (count <= 0) return "count must be >= 1";
    if (count > namegen::kMaxCount) return "count too large";

    for (int attempt = 0; attempt < 3; attempt++) {
        if (backend_ == Backend::GitHubGist) {
            std::string content_b64;
            auto rerr = gist_read_content(content_b64);
            if (!rerr.empty()) return rerr;
            content_b64 = trim_ascii_whitespace(content_b64);
            if (content_b64.empty() || content_b64 == "init") {
                // Treat as brand-new history
                used_bits_.assign((namegen::universe_size() + 7) / 8, 0);
                used_count_ = 0;
            } else {
                vector<uint8_t> blob;
                auto derr = base64_decode_bytes(content_b64, blob);
                if (!derr.empty()) return derr;
                if (blob.size() < min_history_blob_size()) {
                    used_bits_.assign((namegen::universe_size() + 7) / 8, 0);
                    used_count_ = 0;
                } else {
                auto uerr = decode_from_blob(blob);
                if (!uerr.empty()) return uerr;
                }
            }
        }

        const size_t n = namegen::universe_size();
        const size_t remaining = (used_count_ > n) ? 0 : (n - used_count_);
        if (static_cast<size_t>(count) > remaining) {
            std::ostringstream ss;
            ss << "not enough unused names remaining (" << remaining << " left)";
            return ss.str();
        }

        std::vector<size_t> unused;
        unused.reserve(remaining);
        for (size_t i = 0; i < n; i++) if (!get_bit(used_bits_, i)) unused.push_back(i);
        auto rng = seeded_rng();
        std::shuffle(unused.begin(), unused.end(), rng);

        out_names.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
            size_t idx = unused[static_cast<size_t>(i)];
            out_names.push_back(namegen::universe_name_at(idx));
            set_bit(used_bits_, idx);
        }
        used_count_ += static_cast<size_t>(count);

        auto perr = persist();
        if (perr.empty()) return "";

        if (perr.find("precondition failed") != std::string::npos || perr.find("412") != std::string::npos) {
            out_names.clear();
            continue;
        }
        return perr;
    }

    return "could not persist history (concurrent updates); please retry";
}

// -------------------------
// Crypto blob format
// -------------------------
std::string HistoryStore::decode_from_blob(const std::vector<uint8_t>& blob) {
    // magic "RNGZ1"
    const uint8_t MAGIC[5] = {'R', 'N', 'G', 'Z', '1'};
    const size_t MIN = min_history_blob_size();
    if (blob.size() < MIN) return "history blob is corrupted (too small)";
    if (std::memcmp(blob.data(), MAGIC, 5) != 0) return "history blob has wrong magic/version";

    size_t off = 5;
    uint8_t ver = blob[off++];
    if (ver != 1) return "history blob version unsupported";

    auto read_u32 = [&](uint32_t& out) {
        out = 0;
        out |= static_cast<uint32_t>(blob[off + 0]);
        out |= static_cast<uint32_t>(blob[off + 1]) << 8;
        out |= static_cast<uint32_t>(blob[off + 2]) << 16;
        out |= static_cast<uint32_t>(blob[off + 3]) << 24;
        off += 4;
    };
    auto read_u64 = [&](uint64_t& out) {
        out = 0;
        for (int i = 0; i < 8; i++) out |= static_cast<uint64_t>(blob[off++]) << (8 * i);
    };

    uint32_t stored_n = 0;
    uint64_t stored_fp = 0;
    uint32_t raw_len = 0;
    uint32_t comp_len = 0;
    read_u32(stored_n);
    read_u64(stored_fp);
    read_u32(raw_len);
    read_u32(comp_len);

    const size_t n = namegen::universe_size();
    const size_t expected_bytes = (n + 7) / 8;
    if (stored_n != static_cast<uint32_t>(n)) return "history universe size mismatch (names list changed?)";
    if (stored_fp != namegen::universe_fingerprint()) return "history universe fingerprint mismatch (names list changed?)";
    if (raw_len != static_cast<uint32_t>(expected_bytes)) return "history raw length mismatch";
    if (off + comp_len != blob.size()) return "history compressed length mismatch";

    vector<uint8_t> raw(expected_bytes);
    uLongf dest_len = static_cast<uLongf>(raw.size());
    int zrc = ::uncompress(raw.data(), &dest_len, blob.data() + off, comp_len);
    if (zrc != Z_OK || dest_len != raw.size()) return "history decompress failed";

    used_bits_ = std::move(raw);
    used_count_ = 0;
    for (size_t i = 0; i < n; i++) if (get_bit(used_bits_, i)) used_count_++;
    return "";
}

std::string HistoryStore::encode_to_blob(std::vector<uint8_t>& out_blob) const {
    const size_t n = namegen::universe_size();
    const size_t expected_bytes = (n + 7) / 8;
    if (used_bits_.size() != expected_bytes) return "internal error: bitset size mismatch";

    // Compress bitset
    uLongf bound = ::compressBound(static_cast<uLong>(used_bits_.size()));
    vector<uint8_t> comp(bound);
    int level = 6;
    if (const char* lvl = std::getenv("HISTORY_ZLIB_LEVEL"); lvl && *lvl) {
        int v = std::atoi(lvl);
        if (v >= 1 && v <= 9) level = v;
    }
    uLongf comp_len = bound;
    int zrc = ::compress2(comp.data(), &comp_len, used_bits_.data(),
                          static_cast<uLong>(used_bits_.size()), level);
    if (zrc != Z_OK) return "history compress failed";
    comp.resize(static_cast<size_t>(comp_len));

    // Format:
    // magic(5) "RNGZ1"
    // ver(1) = 1
    // universe_size u32
    // universe_fingerprint u64
    // raw_len u32
    // comp_len u32
    // comp bytes
    out_blob.clear();
    out_blob.reserve(min_history_blob_size() + comp.size());
    const uint8_t MAGIC[5] = {'R', 'N', 'G', 'Z', '1'};
    out_blob.insert(out_blob.end(), MAGIC, MAGIC + 5);
    out_blob.push_back(1);

    auto push_u32 = [&](uint32_t v) {
        out_blob.push_back(static_cast<uint8_t>(v & 0xFF));
        out_blob.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out_blob.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out_blob.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    auto push_u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; i++) out_blob.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    };

    push_u32(static_cast<uint32_t>(n));
    push_u64(namegen::universe_fingerprint());
    push_u32(static_cast<uint32_t>(used_bits_.size()));
    push_u32(static_cast<uint32_t>(comp.size()));
    out_blob.insert(out_blob.end(), comp.begin(), comp.end());
    return "";
}

// -------------------------
// GitHub Gist backend
// -------------------------
std::string HistoryStore::gist_init() {
    if (gist_id_.empty()) return "HISTORY_GIST_ID is empty";
    if (github_token_.empty()) return "HISTORY_GITHUB_TOKEN is empty";
    return "";
}

std::string HistoryStore::gist_read_content(std::string& out_content_b64) {
    out_content_b64.clear();
    CurlBuf buf;
    const string url = "https://api.github.com/gists/" + gist_id_;
    auto err = http_request("GET", url, github_token_, "", "", buf);
    if (!err.empty()) return err;
    if (buf.status == 404) return "gist not found (check HISTORY_GIST_ID)";
    if (buf.status < 200 || buf.status >= 300) {
        std::ostringstream ss;
        ss << "gist GET failed (HTTP " << buf.status << ")";
        return ss.str();
    }
    string content;
    auto perr = gist_extract_file_content(buf.body, gist_filename_, content);
    if (!perr.empty()) return perr;
    out_content_b64 = content;
    return "";
}

std::string HistoryStore::gist_write_content(const std::string& content_b64) {
    const string url = "https://api.github.com/gists/" + gist_id_;
    std::ostringstream body;
    body << "{\"files\":{\"" << json_escape(gist_filename_) << "\":{\"content\":\""
         << json_escape(content_b64) << "\"}}}";

    CurlBuf patchbuf;
    // NOTE: GitHub gists do not allow conditional headers (like If-Match) on PATCH.
    // We'll do a simple PATCH; this is durable but not strongly concurrency-safe.
    auto perr = http_request("PATCH", url, github_token_, body.str(), "", patchbuf);
    if (!perr.empty()) return perr;
    if (patchbuf.status < 200 || patchbuf.status >= 300) {
        std::ostringstream ss;
        ss << "gist PATCH failed (HTTP " << patchbuf.status << ")";
        if (!patchbuf.body.empty()) {
            // Include some response body for debugging (it's GitHub error JSON).
            string b = patchbuf.body;
            if (b.size() > 500) b.resize(500);
            ss << ": " << b;
        }
        return ss.str();
    }
    return "";
}

