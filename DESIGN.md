---
name: Passport OS shared UI
description: Passport OS 当前 240×320 LVGL 整机视觉规范
colors:
  travel-bg: "#181B1C"
  travel-surface: "#202526"
  travel-surface-alt: "#303738"
  travel-border: "#59605D"
  travel-text: "#EEE7D6"
  travel-text-dim: "#B3B2A5"
  travel-accent: "#DDB36C"
  travel-danger: "#F09B87"
  travel-on-accent: "#181B1C"
  travel-success: "#9DB89A"
  classic-bg: "#03120B"
  classic-surface: "#091E14"
  classic-surface-alt: "#122E20"
  classic-border: "#37694B"
  classic-text: "#A8EDBB"
  classic-text-dim: "#80AF90"
  classic-accent: "#8CF5A6"
  classic-danger: "#FF9585"
  classic-on-accent: "#03120B"
  classic-success: "#8CF5A6"
  classic-warning: "#F2CA6D"
  travel-warning: "#DDB36C"
  playful-bg: "#F5F7F6"
  playful-surface: "#E8EFEC"
  playful-surface-alt: "#D7E9E1"
  playful-border: "#7B8885"
  playful-text: "#252D30"
  playful-text-dim: "#56635F"
  playful-accent: "#A5315B"
  playful-danger: "#AE302E"
  playful-on-accent: "#FFFFFF"
  playful-success: "#286B59"
  playful-warning: "#805710"
  playful-secondary: "#28648B"
  playful-tertiary: "#A64B29"
  neon-bg: "#0A0B10"
  neon-surface: "#17151F"
  neon-surface-alt: "#252B32"
  neon-border: "#596373"
  neon-text: "#F4F4F6"
  neon-text-dim: "#A4AFC0"
  neon-accent: "#38E8E1"
  neon-danger: "#FF747D"
  neon-on-accent: "#10131A"
  neon-success: "#AAEE6D"
  neon-warning: "#FFD45A"
  neon-secondary: "#FF78CA"
  neon-tertiary: "#FFB75E"
typography:
  body:
    fontFamily: "Source Han Sans SC"
    fontSize: "16px"
    lineHeight: "20px"
    letterSpacing: "0px"
  title:
    fontFamily: "Source Han Sans SC"
    fontSize: "20px"
    lineHeight: "24px"
  numeric-label:
    fontFamily: "Montserrat"
    fontSize: "14px"
  numeric-display:
    fontFamily: "Montserrat"
    fontSize: "32px"
rounded:
  pixel: "0px"
spacing:
  panel-inset: "7px"
components:
  travel-top-bar:
    backgroundColor: "{colors.travel-surface}"
    rounded: "{rounded.pixel}"
    width: "240px"
    height: "26px"
  travel-panel:
    backgroundColor: "{colors.travel-surface}"
    rounded: "{rounded.pixel}"
    padding: "{spacing.panel-inset}"
---

# Design System: Passport OS shared UI

## Overview

共享视觉以实现中的实体按键、真实状态、声音和应用逻辑为边界；历史版本记录不进入本规范。

六套主题共享布局、字体、图像和交互：经典绿、旅行仪器、像素白、霓虹黑、宣纸朱砂、青金守夜。运行时以 `UI_*` 语义角色、`ui_theme_color()`、`ui_theme_color_on_dark()` 和 `ui_theme_element_color()` 为实现来源；上方数值记录现有 palette，不能用于绕开主题 API。

**Key Characteristics:**
- 固定小屏，文字、状态与实体按键说明优先。
- 方角几何与平面色块；共享构件不依赖阴影表达层级。
- 应用可有局部场景色；它们不自动成为全机配色。

## Colors

### Primary

`travel-accent` / `classic-accent` 对应 `UI_ACCENT`，用于强调状态、标题或动作；其反色文字使用 `UI_ON_ACCENT`。`UI_SUCCESS` 和 `UI_DANGER` 是已有状态角色，经典主题中成功与强调共用色值。

### Neutral

所有主题分别保留背景、表面、选中表面、边界、主文字和次文字角色。默认主题是经典绿。等待状态使用 `UI_WARNING` 并同时显示文字。值取自 `ui_pixel.c::s_palettes`，角色取自 `ui_palette.h`。

### 像素白

中性浅色画布配淡灰绿表面，辅助轨道使用浅薄荷色。莓红用于选中动作和主强调，湖蓝用于语音类图标，橙色用于收音机与音乐，绿色用于成功及相应应用图标。主次文字使用中性深灰，应用选择页的电量框和联网信号也使用正文色。色彩主要落在内容与状态，分隔线和顶栏降低饱和度。

首页的 A8 人像与夜景素材把亮部记录为不透明像素，需要深色底保留原有明暗关系。多彩首页使用 `#181B1C` 深底、`#F1EEE6` 人像、`#8DDFC5` 底图、`#86C9FF` 时钟、`#FFAD78` 温度和 `#F58FAA` 天气短语。顶栏与页面深底相同，正文为 `#E9EDEB`、次文字为 `#AFBDB7`、分隔线为 `#53605D`；信号和电池框使用浅色，电池填充依照深色场景的成功、警告和危险色变化。深色角色集中维护在 `ui_pixel.c::s_playful_dark`，由 `ui_theme_color_on_dark()` 读取，原主题在该 API 下仍返回原 palette。

`UI_SECONDARY` / `UI_TERTIARY` 为两种补充强调色，经典绿与旅行主题均映射回原强调色。浅色主题中的 `UI_INK` 是深墨色，与选中项上的白字 `UI_ON_ACCENT` 分开。状态色仍保留成功、警示、危险的含义。深色游戏场景继续使用场景自己的浅色说明与旗杆，不能直接继承浅色主题的深色正文。

设置页按经典绿 → 旅行仪器 → 像素白 → 霓虹黑循环；像素白内部仍为 `UI_THEME_PLAYFUL`，NVS 值 0、1、2 保持原义，霓虹黑为 3，恢复默认仍使用经典绿。

### 霓虹黑

近黑画布贯穿首页与顶栏，浅色人像保持原始亮部蒙版；时钟为电青色、夜景为荧光粉、温度为琥珀色、天气短语为酸橙色。应用图标使用功能色与双色细节，全机正文使用中性白与冷灰。

共享顶栏和底部分隔线统一为 1px 连续冷灰线（`UI_BORDER`）。顶栏分隔线横贯屏宽，底部分隔线沿用左右各 12px 的内容内边距。应用图标外侧保留错开的短笔画，设置选中条在左上、右下各切去 6px 阶梯角。图标与选中条复用现有对象的 draw 回调，不添加常驻动画、帧缓冲、图片或字体。`ui_theme_is_multicolor()` 仅用于彩色主题的辅助色与应用图标；像素白专属的深色场景和深墨色映射保持独立。

## Typography

中文正文使用 `buddy_font_16`，共享标题使用 `ui_font_20`，首页和八字应用的四柱值使用只含十天干、十二地支的 `ganzhi_font_20`；三者均为 Source Han Sans SC 的 LVGL 字体子集，行高来自实际字体结构。它们不是浏览器可直接加载的 webfont。正文默认字距取 `UI_LS_BODY`；共享 title plate 单独使用 `UI_LS_TITLE`（1px）。

六爻排盘使用 `liuyao_font_12` 显示双卦表中的六神、六亲纳甲、世应与变向，使用 `liuyao_title_20` 居中显示本卦和变卦名称。两套字体都包含六爻固定术语、64 卦名和干支子集，不能用于其他应用的长文本。

数字标签使用 Montserrat 14px。首页时钟和收音机刻度等大读数使用数字专用字体；不要由单个应用的用法推导完整的全机字号比例。中文文案变化需核对对应子集字形覆盖。

## Layout

共享构件以 240×320 屏幕定位，顶栏高 26px。电量百分比占 x=160–201，图标占 x=206–232；其余顶栏内容截止于 x=155，至少留 4px 间隔。应用选择页时间使用 x=92、宽 56px 的居中槽位，中心与屏幕 x=120 一致。顶栏文字通过 `ui_pixel_status_text_align()` 按字体行高在分隔线上方的 25px 区域居中，电量文字按电池图标的垂直中心定位。共享 title plate 与 footer 内容宽 216px、左右各 12px。项目未抽象统一间距网格或响应式断点。

共享 footer 有两行：当前操作与次级导航。首页、八字和媒体应用共用这一信息顺序，并根据各自内容高度定位。

## Elevation & Depth

共享背景、顶栏和面板通过表面色、细边界与留白分层，没有显式阴影样式。

## Shapes

共享 `block()`、背景与面板为零圆角。经典主题的普通面板默认有 1px 边框，旅行主题默认无边框；调用方可显式覆盖。title plate 在经典主题为完整边框，旅行主题只保留下边线。不能把无边框写成全局禁令。

## Components

- **顶栏：** `ui_pixel_top_bar()` 使用表面色与 1px 下边线，容纳应用名及状态。
- **面板：** 页面使用语义表面色与共享内边距；选中且可用时 `ui_pixel_set_selected()` 切换至较亮表面与强调色边界。
- **首页：** 左侧纵向四柱、农历日期、仅当天出现的交节提示和下一节气倒计时，右侧时钟、日期、城市、天气和空气质量，底部显示每日一句。四柱使用干支专用字体和五行颜色。
- **应用选择：** 无大卡片或图标框；中央是当前图标和名称，上下露出相邻应用名，只保留一个位置数字，不加小字介绍。
- **按键说明：** 工具页底部使用 272px 分隔线，主要动作位于 276px，返回说明位于 298px。说明提示实体按键，不是触屏按钮。
- **小智：** 使用共享顶栏和双行按键说明，空闲提示居中，交谈时正文左对齐并滚动到最新回复；状态行和正文不加外框，上下键只调音量。
- **收音机：** 保留刻度和真实音柱，音量为短时反馈。
- **八字排盘：** 四柱不显示列头或表格线；大运横向排列，流年两行一屏纵向滚动，当前大运和当前流年使用强调色边界。
- **六爻排盘：** 起卦方式和手动摇卦保持 240×320 竖屏；确认成盘后，盘面与双卦卦意进入 320×240 横屏，退出恢复竖屏。起卦页顶栏左侧显示“六爻排盘-起卦方式”，盘面顶栏左侧合并显示“六爻排盘-时间起卦/手动摇卦”，均不另占章节栏。时间起卦直接读取当前时间；手动摇卦用三枚钱币逐爻摇六次，钱面与 6/7/8/9 在竖屏留痕。盘面以本卦、变卦左右对照为核心，本卦/变卦名称居中，变向使用 `X →` 或 `O →`，主盘不显示逐爻行号，也不要求选择事项类别；右上信号格与电量文字保持独立间距。确定打开两卦白话解释，不提供单爻详参。
- **万年历：** 月历固定显示五周窗口，六周月份随选中日期上移一行。每格显示公历日、农历或节气、日干支；确定进入日期详情，长按下键打开年月跳转。
- **电池与信号：** 共享实现使用几何块。电池百分比固定单行右对齐，给 `100%` 留足空间；电量 0–15 使用危险色、16–35 使用警示色、36–100 使用成功色，未知值显示 `--`。零电量和未知值不显示填充；不要用假测量值装饰应用。
- **设置焦点：** 设置页当前行使用强调色底与 `UI_ON_ACCENT` 文字，恢复默认的待确认行使用危险色。确认前写明“再次确定”，上下键可取消。
- **操作动效：** 应用选择使用 140ms、6px 的图标纵向反馈，文字保持静止；设置焦点使用 140ms 背景渐显。连续操作会取消同对象旧动画，不叠加往返或循环动画。
- **切换：** 继续保留上一画面，目标页面准备好后显示；不添加“正在返回”“正在打开”、空白过渡屏或全屏淡出。
- **静止页面：** 电池、信号和连接点颜色在值变化时才更新；收音机音量柱保留 20Hz 更新机会，时钟与睡眠计时文案按 1Hz 更新。

## Do's and Don'ts

### Do:
- **Do** 通过现有 UI 语义角色读取主题颜色，并保留六套主题的差异。
- **Do** 在实际 240×320 渲染中核对文字覆盖、截断和按键提示。
- **Do** 将游戏场景的构图、色彩及动画限定在应用文档中。

### Don't:
- **Don't** 将单个应用的局部尺寸提升为全机布局规范。
- **Don't** 将字体子集当作可覆盖任意中文的完整字体。
- **Don't** 将 synthetic fixture 图集描述成实机交互、麦克风体验或显示屏验证。
