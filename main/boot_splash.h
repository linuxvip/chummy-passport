// 开机动画:命海日出 —— 金色太阳、几何海浪、星点与 14 格启动节奏。
// 全部由 LVGL 基础对象绘制,不依赖位图;原生色板不随主题变化。
// 时长约 1s;自带节拍,调用方不得持 LVGL 锁。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 显示开机动画并阻塞约 0.9s。内部自己拿/放 LVGL 锁。
void boot_splash_run(void);

// 首页加载完成后调用:删除开机屏对象(动画屏此后不再可见)。
void boot_splash_end(void);

#ifdef __cplusplus
}
#endif
