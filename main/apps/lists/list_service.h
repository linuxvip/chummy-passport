#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct cJSON;

namespace lists {

constexpr unsigned kItems = 60;
constexpr unsigned kBatch = 3;
constexpr unsigned kText = 96;

struct Item {
    uint32_t id;
    uint32_t done;
    char text[kText];
};

struct Checklist {
    uint32_t magic;
    uint32_t revision;
    uint32_t next_item;
    uint32_t count;
    Item items[kItems];
};

enum class Action { Add, Set, Remove, ClearDone };

void initialize();
uint32_t revision();

// Storage is one checklist. An empty checklist is a valid first-class state.
bool snapshot(Checklist *out, const char **error);
size_t completed_count(const Checklist &checklist);

// All edits use the revision returned by snapshot().
bool modify(uint32_t expected_revision, Action action, uint32_t item_id,
            const char *text, int done, const char *const *items, size_t count,
            const char **error);

size_t tool_count();
const char *tool_schema(size_t index);
std::string call(const char *name, const cJSON *args);

}  // namespace lists
