#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace namegen {

constexpr int kMaxCount = 5000;

// Maximum number of unique full-name combinations available.
// If you request more than this, uniqueness is impossible.
int max_unique_count();

// Stable "universe" of possible full names.
// These are used by the server-side global history store.
size_t universe_size();
const std::string& universe_name_at(size_t idx);
uint64_t universe_fingerprint();

// Generates `count` full names ("First Last").
// Guarantees: within a single call, names are unique (no duplicates),
// as long as `count <= max_unique_count()` and `count <= kMaxCount`.
//
// Throws no exceptions; if count is invalid, returns an empty list.
std::vector<std::string> generate_names(int count);

}  // namespace namegen

