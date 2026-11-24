#include "platform.h"

#include "../main/apps/reminders/reminder_core.cc"
#include "../main/apps/lists/list_service.cc"
#include "../main/apps/lists/list_mcp.cc"

struct LegacyList {
    uint32_t magic;
    uint32_t id;
    uint32_t revision;
    uint32_t next_item;
    uint32_t count;
    char title[48];
    lists::Item items[12];
};
static_assert(sizeof(LegacyList) == 1316, "legacy fixture layout changed");

static LegacyList legacy(uint32_t id, const char *title) {
    LegacyList value{};
    value.magic = 0x3154534c;
    value.id = id;
    value.revision = 3;
    value.next_item = 1;
    std::snprintf(value.title, sizeof(value.title), "%s", title);
    return value;
}

static void add_legacy(LegacyList &list, const char *text, bool done) {
    auto &item = list.items[list.count++];
    item = {};
    item.id = list.next_item++;
    item.done = done;
    std::snprintf(item.text, sizeof(item.text), "%s", text);
}

static void store_legacy(unsigned slot, const LegacyList &list) {
    auto *raw = reinterpret_cast<const unsigned char *>(&list);
    disk["list" + std::to_string(slot)] = {raw, raw + sizeof(list)};
}

static std::string invoke(const char *name, const char *args) {
    auto *json = cJSON_Parse(args);
    auto result = lists::call(name, json);
    cJSON_Delete(json);
    return result;
}

static std::string answer(const char *name, const char *args) {
    auto outer_text = invoke(name, args);
    cJSON *outer = cJSON_Parse(outer_text.c_str());
    cJSON *content = cJSON_GetObjectItemCaseSensitive(outer, "content");
    cJSON *item = cJSON_GetArrayItem(content, 0);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
    std::string result =
        cJSON_IsString(text) ? text->valuestring : std::string();
    cJSON_Delete(outer);
    return result;
}

int main() {
    using namespace lists;
    auto first = legacy(1, "旧购物清单");
    add_legacy(first, "牛奶", true);
    add_legacy(first, "鸡蛋", false);
    auto second = legacy(4, "旧出门清单");
    add_legacy(second, "牛奶", false);
    add_legacy(second, "雨伞", true);
    store_legacy(0, first);
    store_legacy(1, second);
    auto empty = legacy(3, "空清单");
    store_legacy(2, empty);

    initialize();
    Checklist checklist{};
    const char *error = nullptr;
    assert(snapshot(&checklist, &error));
    assert(checklist.revision == 1 && checklist.count == 3);
    assert(completed_count(checklist) == 1);
    assert(!strcmp(checklist.items[0].text, "牛奶") &&
           checklist.items[0].done == 0);  // pending copy wins
    assert(!strcmp(checklist.items[1].text, "鸡蛋"));
    assert(!strcmp(checklist.items[2].text, "雨伞"));
    assert(!disk.count("list0") && !disk.count("list1") && !disk.count("list2"));
    assert(disk.count("items") && disk.count("schema"));

    const auto before = disk;
    fail_write = true;
    const char *failed[] = {"不得保存"};
    assert(!modify(checklist.revision, Action::Add, 0, nullptr, -1,
                   failed, 1, &error));
    fail_write = false;
    assert(disk == before);

    const char *milk = "牛奶";
    assert(!modify(checklist.revision - 1, Action::Add, 0, nullptr, -1,
                   &milk, 1, &error));
    assert(modify(checklist.revision, Action::Add, 0, nullptr, -1,
                  &milk, 1, &error));
    assert(snapshot(&checklist, &error) && checklist.count == 3);  // duplicate

    const auto milk_id = checklist.items[0].id;
    assert(modify(checklist.revision, Action::Set, milk_id, nullptr, 1,
                  nullptr, 0, &error));
    assert(snapshot(&checklist, &error) && checklist.items[0].done == 1);
    const auto egg_id = checklist.items[1].id;
    assert(modify(checklist.revision, Action::Remove, egg_id, nullptr, -1,
                  nullptr, 0, &error));
    assert(snapshot(&checklist, &error) && checklist.count == 2);

    assert(modify(checklist.revision, Action::ClearDone, 0, nullptr, -1,
                  nullptr, 0, &error));
    assert(snapshot(&checklist, &error));
    assert(checklist.count == 0 && completed_count(checklist) == 0);
    assert(!modify(checklist.revision, Action::ClearDone, 0, nullptr, -1,
                   nullptr, 0, &error));

    const char *incoming[] = {"检查夜间主题", "更新截图", "复查布局"};
    assert(modify(checklist.revision, Action::Add, 0, nullptr, -1,
                  incoming, 3, &error));
    assert(snapshot(&checklist, &error) && checklist.count == 3);
    const auto check_id = checklist.items[0].id;
    assert(modify(checklist.revision, Action::Set, check_id, nullptr, 1,
                  nullptr, 0, &error));

    // Simulated reboot reloads the same singleton checklist.
    mutex = nullptr;
    initialize();
    assert(snapshot(&checklist, &error) && checklist.count == 3);
    assert(completed_count(checklist) == 1);

    auto read = answer("self.list.read", "{}");
    assert(read.find("\"total\":3") != std::string::npos);
    assert(read.find("\"list_id\"") == std::string::npos);
    assert(read.find("\"title\"") == std::string::npos);
    auto add = answer("self.list.add",
        ("{\"revision\":" + std::to_string(checklist.revision) +
         ",\"items\":[\"检查夜间主题\",\"补充真机记录\"]}").c_str());
    assert(add.find("\"saved\":true") != std::string::npos);
    assert(snapshot(&checklist, &error) && checklist.count == 4);
    auto past_end = answer("self.list.read", R"({"offset":60})");
    assert(past_end.find("\"items\":[]") != std::string::npos);

    for (auto args : {
             R"({"revision":1,"action":"set","item_id":1,"done":"yes"})",
             R"({"revision":1,"action":"remove","item_id":1,"text":"x"})",
             R"({"revision":1,"action":"clear_done","item_id":1})",
             R"({"list_id":1,"revision":1,"action":"remove","item_id":1})",
         }) {
        assert(invoke("self.list.update", args).find("\"isError\":true") !=
               std::string::npos);
    }

    disk["items"][0] ^= 1;
    assert(!snapshot(&checklist, &error));
    puts("Checklist: singleton migration, add/toggle/remove/clear, revision, "
         "reboot, voice tools and corruption recovery PASS");
}
