#include "apps/remote/hid_reports.h"

extern "C" {
#include "esp_hid_common.h"
}

#include <cassert>

int main() {
    using namespace remote;

    esp_hid_report_map_t *keyboard = esp_hid_parse_report_map(
        kKeyboardReportMap, sizeof(kKeyboardReportMap));
    assert(keyboard != nullptr);
    assert(keyboard->usage == ESP_HID_USAGE_KEYBOARD);
    assert(keyboard->reports_len == 2);
    assert(keyboard->reports[0].report_id == kKeyboardReportId);
    assert(keyboard->reports[0].report_type == ESP_HID_REPORT_TYPE_INPUT);
    assert(keyboard->reports[0].value_len == sizeof(KeyboardReport));
    esp_hid_free_report_map(keyboard);

    esp_hid_report_map_t *consumer = esp_hid_parse_report_map(
        kConsumerReportMap, sizeof(kConsumerReportMap));
    assert(consumer != nullptr);
    assert(consumer->reports_len == 1);
    assert(consumer->reports[0].report_id == kConsumerReportId);
    assert(consumer->reports[0].report_type == ESP_HID_REPORT_TYPE_INPUT);
    assert(consumer->reports[0].value_len == sizeof(ConsumerReport));
    esp_hid_free_report_map(consumer);
}
