#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/test_xiaozhi_face_power.py
python3 tests/check_digit_font.py
python3 tests/check_title_glyphs.py
python3 tests/check_launcher_apps.py
test_dir="$(mktemp -d /tmp/passport-host.XXXXXX)"
trap 'rm -rf -- "$test_dir"' EXIT
if [[ "$(uname -s)" == Darwin ]]; then
    section_gc_flag="-Wl,-dead_strip"
else
    section_gc_flag="-Wl,--gc-sections"
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Imain tests/test_xiaozhi_level.cc -o "$test_dir/xiaozhi-level"
"$test_dir/xiaozhi-level"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-unused-variable \
    -fsanitize=address,undefined -g -Itests/registry_mocks -Imain \
    tests/test_app_registry.cc -o "$test_dir/registry"
"$test_dir/registry"
python3 tools/generate_ui_sfx.py --check
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itests/game_audio_mocks -Imain tests/test_ui_sfx.c -lm -o "$test_dir/ui-sfx"
"$test_dir/ui-sfx"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/test_terminal.cc -o "$test_dir/terminal"
"$test_dir/terminal"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/audio_mocks -Icomponents/bsp/src \
    tests/test_audio_volume.c components/bsp/src/bsp_audio_volume.c -lm -o "$test_dir/audio-volume"
"$test_dir/audio-volume"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_stopwatch.c main/apps/stopwatch/stopwatch_core.c -o "$test_dir/stopwatch"
"$test_dir/stopwatch"
python3 tests/test_wifi_configure.py
python3 tests/test_wifi_internet.py
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain \
    tests/test_battery_guard.c -o "$test_dir/battery-guard"
"$test_dir/battery-guard"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain \
    tests/test_pomodoro.c main/apps/pomodoro/pomodoro_core.c -o "$test_dir/pomodoro"
"$test_dir/pomodoro"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain \
    -c main/apps/pomodoro/pomodoro_core.c -o "$test_dir/pomodoro-core.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain \
    -c main/apps/stopwatch/stopwatch_core.c -o "$test_dir/stopwatch-core.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain \
    tests/test_timer_resume.cc "$test_dir/stopwatch-core.o" "$test_dir/pomodoro-core.o" -o "$test_dir/timer-resume"
"$test_dir/timer-resume"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-unused-variable -Wno-misleading-indentation \
    -fsanitize=address,undefined -g -Itests/pomodoro_mocks -Imain -Imain/fonts \
    tests/test_pomodoro_app.cc "$test_dir/pomodoro-core.o" -o "$test_dir/pomodoro-app"
"$test_dir/pomodoro-app"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/test_wifi_keyboard.cc -o "$test_dir/wifi-keyboard"
"$test_dir/wifi-keyboard"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare \
    -Wno-unused-parameter -Wno-misleading-indentation -fsanitize=address,undefined -g \
    -Itests/wifi_mocks -Icomponents/esp-wifi-connect/include \
    tests/test_wifi_station.cc -o "$test_dir/wifi-station"
"$test_dir/wifi-station"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Imain -Icomponents/esp-wifi-connect/include \
    tests/test_captive_setup.cc -o "$test_dir/captive-setup"
"$test_dir/captive-setup"
node tests/test_profile_page.js
node tests/test_portrait_pixels.js
python3 tests/test_card_gallery.py
python3 tests/test_avatar_upload.py
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-unused-variable \
    -fsanitize=address,undefined -g -ffunction-sections -fdata-sections \
    -Itests/home_mocks -Itests/radio_mocks -Imain \
    tests/test_home_storage.c "$section_gc_flag" -lm -o "$test_dir/home-storage"
"$test_dir/home-storage"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -fsanitize=address,undefined -g \
    -c tests/vendor/cjson/cJSON.c -o "$test_dir/cjson.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Itests/vendor/cjson -Icomponents/esp-wifi-connect/include \
    tests/test_phone_time.cc "$test_dir/cjson.o" -o "$test_dir/phone-time"
"$test_dir/phone-time"
python3 tests/test_city_catalog.py
python3 tests/test_home_weather.py
python3 tests/test_home_weather_schedule.py
python3 tests/test_home_daily_quote.py
python3 tests/test_home_element_colors.py
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Imain tests/test_home_almanac.c main/apps/home/home_almanac.c \
    -o "$test_dir/home-almanac"
"$test_dir/home-almanac"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Imain tests/test_bazi_core.c main/apps/bazi/bazi_core.c \
    main/apps/bazi/bazi_almanac.c -lm -o "$test_dir/bazi-core"
"$test_dir/bazi-core"
python3 tests/test_bazi_ui.py
python3 tests/test_radio_catalog.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Imain tests/test_radio_hls.cc -o "$test_dir/radio-hls"
"$test_dir/radio-hls"
python3 tests/test_radio_source.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -g -Imain tests/test_radio_stream_buffer.cc -o "$test_dir/radio-buffer"
"$test_dir/radio-buffer"
python3 tests/test_xiaozhi.py
python3 tests/test_reminders.py
python3 tests/test_device_control.py
python3 tests/test_lists.py
python3 tests/test_input_serial_dispatch.py
python3 tests/test_button_poll.py
python3 tests/test_bsp_button.py
python3 tests/test_bsp_audio_profiles.py
python3 tests/test_memory_telemetry.py
python3 tests/test_radio_lifecycle.py
python3 tests/test_verify_firmware.py
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/battery_mocks -Icomponents/bsp/include \
    tests/test_battery_driver.c components/bsp/src/bsp_battery.c -o "$test_dir/battery"
"$test_dir/battery"
python3 tests/test_netease_parse.py
python3 tests/test_media_startup.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itests/registry_mocks -Imain tests/test_netease_controls.cc -o "$test_dir/netease-controls"
"$test_dir/netease-controls"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/registry_mocks -Imain \
    tests/test_netease_intent.cc main/apps/netease/netease_json.cc -o "$test_dir/netease-intent"
"$test_dir/netease-intent"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
    tests/test_aihot.cc main/apps/netease/netease_json.cc -o "$test_dir/aihot"
"$test_dir/aihot"
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain     tests/test_netease_dsp.cc -o "$test_dir/netease-dsp"
"$test_dir/netease-dsp"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
    -Itests/registry_mocks tests/test_netease_performance.cc main/apps/netease/netease_json.cc -o "$test_dir/netease-performance"
"$test_dir/netease-performance"
echo "Host regression tests: PASS"
