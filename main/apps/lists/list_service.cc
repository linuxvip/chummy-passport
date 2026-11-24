#include "list_service.h"

#include "apps/reminders/reminder_core.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace lists {
namespace {

constexpr uint32_t kMagic = 0x3254534c;  // LST2
constexpr uint32_t kLegacyMagic = 0x3154534c;  // LST1
constexpr uint32_t kSchemaVersion = 2;
constexpr unsigned kLegacyLists = 5;
constexpr unsigned kLegacyItems = 12;
constexpr unsigned kLegacyTitle = 48;
constexpr char kNvsNamespace[] = "checklist_v2";
constexpr char kLegacyNamespace[] = "text_lists_v1";
constexpr char kItemsKey[] = "items";
constexpr char kSchemaKey[] = "schema";

struct LegacyList {
    uint32_t magic;
    uint32_t id;
    uint32_t revision;
    uint32_t next_item;
    uint32_t count;
    char title[kLegacyTitle];
    Item items[kLegacyItems];
};

static_assert(sizeof(LegacyList) == 1316, "legacy list layout changed");

StaticSemaphore_t mutex_storage;
SemaphoreHandle_t mutex;
std::atomic<uint32_t> rev{0};
bool storage_error;

struct Guard {
    Guard() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(mutex); }
};

struct Storage {
    nvs_handle_t handle{};
    bool ok;
    Storage() : ok(nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {}
    ~Storage() {
        if (ok) nvs_close(handle);
    }
};

struct LegacyStorage {
    nvs_handle_t handle{};
    bool ok;
    LegacyStorage()
        : ok(nvs_open(kLegacyNamespace, NVS_READWRITE, &handle) == ESP_OK) {}
    ~LegacyStorage() {
        if (ok) nvs_close(handle);
    }
};

bool fail(const char **error, const char *text) {
    *error = text;
    return false;
}

bool text_ok(const char *text, size_t capacity) {
    return text && std::strlen(text) < capacity && reminders::valid_title(text);
}

void set_empty(Checklist *checklist) {
    *checklist = {};
    checklist->magic = kMagic;
    checklist->revision = 1;
    checklist->next_item = 1;
}

bool valid(const Checklist &checklist) {
    if (checklist.magic != kMagic || !checklist.revision || !checklist.next_item ||
        checklist.count > kItems) {
        return false;
    }
    for (unsigned i = 0; i < checklist.count; ++i) {
        const Item &item = checklist.items[i];
        if (!item.id || item.id >= checklist.next_item || item.done > 1 ||
            !std::memchr(item.text, 0, sizeof(item.text)) ||
            !text_ok(item.text, sizeof(item.text))) {
            return false;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (item.id == checklist.items[j].id) return false;
        }
    }
    return true;
}

bool valid_legacy(const LegacyList &legacy) {
    if (legacy.magic != kLegacyMagic || !legacy.id || !legacy.revision ||
        !legacy.next_item || legacy.count > kLegacyItems ||
        !std::memchr(legacy.title, 0, sizeof(legacy.title)) ||
        !text_ok(legacy.title, sizeof(legacy.title))) {
        return false;
    }
    for (unsigned i = 0; i < legacy.count; ++i) {
        const Item &item = legacy.items[i];
        if (!item.id || item.id >= legacy.next_item || item.done > 1 ||
            !std::memchr(item.text, 0, sizeof(item.text)) ||
            !text_ok(item.text, sizeof(item.text))) {
            return false;
        }
    }
    return true;
}

void legacy_key(unsigned slot, char (&key)[8]) {
    std::snprintf(key, sizeof(key), "list%u", slot);
}

bool load(Storage &storage, Checklist *out, const char **error) {
    set_empty(out);
    size_t length = sizeof(*out);
    const esp_err_t result =
        nvs_get_blob(storage.handle, kItemsKey, out, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        set_empty(out);
        return true;
    }
    if (result != ESP_OK || length != sizeof(*out) || !valid(*out)) {
        set_empty(out);
        return fail(error, "事项清单存储读取失败，原数据未被覆盖");
    }
    return true;
}

bool save(Storage &storage, Checklist *checklist, const char **error) {
    if (checklist->revision == UINT32_MAX) {
        return fail(error, "事项清单版本已用尽");
    }
    ++checklist->revision;
    esp_err_t result =
        nvs_set_blob(storage.handle, kItemsKey, checklist, sizeof(*checklist));
    if (result == ESP_OK) result = nvs_commit(storage.handle);
    if (result != ESP_OK) {
        --checklist->revision;
        return fail(error, "事项清单保存失败，请稍后重试");
    }
    ++rev;
    return true;
}

bool append(Checklist *checklist, const char *const *items, size_t count,
            bool *changed, const char **error) {
    *changed = false;
    if (!count || count > kBatch || !items) {
        return fail(error, "每次请添加一到三条事项，可分批添加");
    }
    for (size_t i = 0; i < count; ++i) {
        if (!text_ok(items[i], kText)) {
            return fail(error, "事项过长或无效，请缩短到三十个汉字左右");
        }
        bool duplicate = false;
        for (unsigned j = 0; j < checklist->count; ++j) {
            duplicate = duplicate ||
                !std::strcmp(checklist->items[j].text, items[i]);
        }
        if (duplicate) continue;
        if (checklist->count == kItems) {
            return fail(error, "事项清单最多保存六十条，请先删除不需要的事项");
        }
        if (checklist->next_item == UINT32_MAX) {
            return fail(error, "事项编号已用尽");
        }
        Item &item = checklist->items[checklist->count++];
        item = {};
        item.id = checklist->next_item++;
        std::strcpy(item.text, items[i]);
        *changed = true;
    }
    return true;
}

bool migrate_legacy(Storage &storage, const char **error) {
    uint32_t schema = 0;
    if (nvs_get_u32(storage.handle, kSchemaKey, &schema) == ESP_OK &&
        schema == kSchemaVersion) {
        return true;
    }

    LegacyStorage legacy_storage;
    if (!legacy_storage.ok) {
        return fail(error, "旧清单存储不可用，尚未执行迁移");
    }
    std::unique_ptr<Checklist> migrated(new (std::nothrow) Checklist);
    if (!migrated) return fail(error, "事项清单迁移内存不足，旧数据未覆盖");
    set_empty(migrated.get());
    for (unsigned slot = 0; slot < kLegacyLists; ++slot) {
        char key[8];
        legacy_key(slot, key);
        std::unique_ptr<LegacyList> legacy(new (std::nothrow) LegacyList{});
        if (!legacy) return fail(error, "事项清单迁移内存不足，旧数据未覆盖");
        size_t length = sizeof(*legacy);
        const esp_err_t result =
            nvs_get_blob(legacy_storage.handle, key, legacy.get(), &length);
        if (result == ESP_ERR_NVS_NOT_FOUND) continue;
        if (result != ESP_OK || length != sizeof(*legacy) || !valid_legacy(*legacy)) {
            return fail(error, "旧清单迁移校验失败，旧数据未覆盖");
        }
        for (unsigned i = 0; i < legacy->count; ++i) {
            Item *existing = nullptr;
            for (unsigned j = 0; j < migrated->count; ++j) {
                if (!std::strcmp(migrated->items[j].text, legacy->items[i].text)) {
                    existing = &migrated->items[j];
                    break;
                }
            }
            if (existing) {
                // A duplicated item remains pending if any old copy was pending.
                existing->done =
                    existing->done && legacy->items[i].done ? 1 : 0;
                continue;
            }
            if (migrated->count == kItems || migrated->next_item == UINT32_MAX) {
                return fail(error, "旧清单事项超过容量，旧数据未覆盖");
            }
            Item &item = migrated->items[migrated->count++];
            item = {};
            item.id = migrated->next_item++;
            item.done = legacy->items[i].done;
            std::strcpy(item.text, legacy->items[i].text);
        }
    }

    esp_err_t result =
        nvs_set_blob(storage.handle, kItemsKey, migrated.get(), sizeof(*migrated));
    if (result == ESP_OK) result = nvs_set_u32(storage.handle, kSchemaKey, kSchemaVersion);
    if (result == ESP_OK) result = nvs_commit(storage.handle);
    if (result != ESP_OK) {
        return fail(error, "旧清单迁移保存失败，旧数据未覆盖");
    }
    ++rev;

    // Old values are harmless after schema=2. Keep the migration successful
    // even if cleanup cannot erase every historical slot.
    for (unsigned slot = 0; slot < kLegacyLists; ++slot) {
        char key[8];
        legacy_key(slot, key);
        nvs_erase_key(legacy_storage.handle, key);
    }
    nvs_erase_key(legacy_storage.handle, "next");
    nvs_commit(legacy_storage.handle);
    return true;
}

}  // namespace

void initialize() {
    if (mutex) return;
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    const char *error = nullptr;
    Storage storage;
    storage_error = !storage.ok || !migrate_legacy(storage, &error);
}

uint32_t revision() {
    return rev.load(std::memory_order_acquire);
}

size_t completed_count(const Checklist &checklist) {
    size_t completed = 0;
    for (unsigned i = 0; i < checklist.count; ++i) {
        completed += checklist.items[i].done != 0;
    }
    return completed;
}

bool snapshot(Checklist *out, const char **error) {
    *error = nullptr;
    if (!out || !mutex) return fail(error, "事项清单尚未就绪");
    if (storage_error) return fail(error, "旧事项清单迁移失败，原数据未被覆盖");
    Guard guard;
    Storage storage;
    if (!storage.ok) return fail(error, "事项清单存储不可用");
    return load(storage, out, error);
}

bool modify(uint32_t expected_revision, Action action, uint32_t item_id,
            const char *text, int done, const char *const *items, size_t count,
            const char **error) {
    *error = nullptr;
    if (!mutex) return fail(error, "事项清单尚未就绪");
    if (storage_error) return fail(error, "旧事项清单迁移失败，原数据未被覆盖");
    Guard guard;
    Storage storage;
    if (!storage.ok) return fail(error, "事项清单存储不可用");

    std::unique_ptr<Checklist> checklist(new (std::nothrow) Checklist);
    if (!checklist) return fail(error, "事项清单内存不足");
    if (!load(storage, checklist.get(), error)) return false;
    if (!expected_revision || checklist->revision != expected_revision) {
        return fail(error, "事项清单已改变，请重新读取后再操作");
    }

    bool changed = true;
    if (action == Action::Add) {
        if (!append(checklist.get(), items, count, &changed, error)) return false;
        if (!changed) return true;
    } else if (action == Action::Set) {
        unsigned index = 0;
        while (index < checklist->count && checklist->items[index].id != item_id) {
            ++index;
        }
        if (index == checklist->count) {
            return fail(error, "这条事项已不存在，请重新读取");
        }
        if (!text && done < 0) {
            return fail(error, "请指定新文字或完成状态");
        }
        if (text) {
            if (!text_ok(text, kText)) {
                return fail(error, "事项过长或无效，请缩短到三十个汉字左右");
            }
            std::strcpy(checklist->items[index].text, text);
        }
        if (done >= 0) {
            if (done > 1) return fail(error, "完成状态无效");
            checklist->items[index].done = static_cast<uint32_t>(done);
        }
    } else if (action == Action::Remove) {
        unsigned index = 0;
        while (index < checklist->count && checklist->items[index].id != item_id) {
            ++index;
        }
        if (index == checklist->count) {
            return fail(error, "这条事项已不存在，请重新读取");
        }
        for (unsigned i = index; i + 1 < checklist->count; ++i) {
            checklist->items[i] = checklist->items[i + 1];
        }
        checklist->items[--checklist->count] = {};
    } else if (action == Action::ClearDone) {
        unsigned write = 0;
        for (unsigned read = 0; read < checklist->count; ++read) {
            if (checklist->items[read].done) continue;
            if (write != read) checklist->items[write] = checklist->items[read];
            ++write;
        }
        if (write == checklist->count) {
            return fail(error, "当前没有已完成事项");
        }
        const unsigned kept = write;
        while (write < checklist->count) checklist->items[write++] = {};
        checklist->count = kept;
    } else {
        return fail(error, "不支持的事项操作");
    }
    return save(storage, checklist.get(), error);
}

}  // namespace lists
