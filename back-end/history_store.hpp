#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Compressed + base64-encoded "used name" store for global uniqueness across requests.
//
// Note: this is NOT encryption. Anyone with access to the backing store can decode it.
// The goal is reducing size (compression) + storing as text (base64).
//
// Durable persistence options:
// - If `HISTORY_GIST_ID` + `HISTORY_GITHUB_TOKEN` are set: stores a compressed blob in a GitHub Gist (durable).
// - Otherwise: stores a compressed file at `HISTORY_FILE` (ephemeral on many hosts).
class HistoryStore {
public:
    explicit HistoryStore(std::string file_path);

    // Returns empty string on success; otherwise an error message.
    std::string init();

    int remaining_unique() const;
    int total_unique() const;

    // Generates `count` unique names (globally unique across all prior calls),
    // persists history, and returns empty string on success; otherwise an error.
    std::string generate_and_mark(int count, std::vector<std::string>& out_names);

private:
    enum class Backend {
        File,
        GitHubGist,
    };

    std::string file_path_;
    bool ready_ = false;

    std::vector<uint8_t> used_bits_; // bitset over namegen universe indices
    size_t used_count_ = 0;

    Backend backend_ = Backend::File;
    std::string gist_id_;
    std::string gist_filename_;
    std::string github_token_;

    std::string load_or_init_empty();
    std::string persist();

    // Common helpers for encoding/compression
    std::string encode_to_blob(std::vector<uint8_t>& out_blob) const;
    std::string decode_from_blob(const std::vector<uint8_t>& blob);

    // GitHub Gist helpers
    std::string gist_init();
    std::string gist_read_content(std::string& out_content_b64);
    std::string gist_write_content(const std::string& content_b64);
};

