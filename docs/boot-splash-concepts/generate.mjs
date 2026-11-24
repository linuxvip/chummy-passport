import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
mkdirSync(root, { recursive: true });

const W = 240;
const H = 320;
const sans = "PingFang SC, Hiragino Sans GB, sans-serif";
const latin = "Avenir Next, Helvetica Neue, sans-serif";
const mono = "Menlo, Monaco, monospace";

const esc = (value) =>
  String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");

const text = (x, y, value, size, fill, options = {}) => {
  const {
    family = sans,
    weight = 400,
    anchor = "start",
    spacing = 0,
    opacity = 1,
  } = options;
  return `<text x="${x}" y="${y}" fill="${fill}" font-family="${family}" font-size="${size}" font-weight="${weight}" text-anchor="${anchor}" letter-spacing="${spacing}" opacity="${opacity}">${esc(value)}</text>`;
};

const line = (x1, y1, x2, y2, color, width = 1, opacity = 1) =>
  `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${color}" stroke-width="${width}" opacity="${opacity}"/>`;

const rect = (x, y, w, h, fill, options = {}) => {
  const {
    stroke = "none",
    sw = 1,
    rx = 0,
    opacity = 1,
    dash = "",
  } = options;
  return `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${rx}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}" opacity="${opacity}" ${dash ? `stroke-dasharray="${dash}"` : ""}/>`;
};

const circle = (cx, cy, r, fill, options = {}) => {
  const { stroke = "none", sw = 1, opacity = 1, dash = "" } = options;
  return `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}" opacity="${opacity}" ${dash ? `stroke-dasharray="${dash}"` : ""}/>`;
};

const polyline = (points, stroke, width = 1, opacity = 1) =>
  `<polyline points="${points}" fill="none" stroke="${stroke}" stroke-width="${width}" opacity="${opacity}" stroke-linejoin="round" stroke-linecap="round"/>`;

const progress = (x, y, count, gap, size, active, color, inactive) => {
  const result = [];
  for (let i = 0; i < count; i += 1) {
    result.push(
      rect(
        x + i * gap,
        y,
        size,
        size,
        i < active ? color : inactive,
        { opacity: i < active ? 1 : 0.48 },
      ),
    );
  }
  return result.join("");
};

function astrolabe() {
  const bg = "#03120B";
  const surface = "#091E14";
  const border = "#37694B";
  const accent = "#8CF5A6";
  const dim = "#80AF90";
  const gold = "#F2CA6D";
  const cyan = "#7DD6D1";
  const rose = "#F58FAA";
  const ticks = [];
  const nodes = [];
  const pillars = [];

  for (let i = 0; i < 24; i += 1) {
    const a = (i * Math.PI * 2) / 24;
    const r0 = i % 3 === 0 ? 66 : 71;
    const r1 = 77;
    ticks.push(
      line(
        120 + Math.cos(a) * r0,
        141 + Math.sin(a) * r0,
        120 + Math.cos(a) * r1,
        141 + Math.sin(a) * r1,
        i % 3 === 0 ? accent : border,
        i % 3 === 0 ? 1.4 : 0.8,
        i % 3 === 0 ? 0.85 : 0.5,
      ),
    );
  }

  [
    [120, 64, accent],
    [196, 141, gold],
    [120, 218, dim],
    [44, 141, cyan],
  ].forEach(([x, y, color]) => {
    nodes.push(circle(x, y, 2.4, color));
    nodes.push(circle(x, y, 5.6, "none", { stroke: color, sw: 0.7, opacity: 0.36 }));
  });

  const pillarX = [101, 113, 127, 139];
  const pillarColors = [accent, gold, rose, cyan];
  pillarX.forEach((x, index) => {
    const color = pillarColors[index];
    pillars.push(line(x, 119, x, 164, color, 1));
    pillars.push(rect(x - 3, 121, 6, 6, color));
    pillars.push(rect(x - 3, 139, 6, 18, "none", { stroke: color, sw: 1 }));
    pillars.push(rect(x - 3, 156, 6, 6, color));
    pillars.push(text(x, 111, ["年", "月", "日", "时"][index], 6.5, dim, {
      anchor: "middle",
      spacing: 0,
    }));
  });

  return `
    <defs>
      <radialGradient id="astro-glow" cx="50%" cy="44%" r="58%">
        <stop offset="0" stop-color="${accent}" stop-opacity=".12"/>
        <stop offset="1" stop-color="${bg}" stop-opacity="0"/>
      </radialGradient>
      <pattern id="astro-grid" width="8" height="8" patternUnits="userSpaceOnUse">
        <path d="M 8 0 L 0 0 0 8" fill="none" stroke="${border}" stroke-width=".25" opacity=".16"/>
      </pattern>
    </defs>
    <rect width="240" height="320" fill="${bg}"/>
    <rect width="240" height="320" fill="url(#astro-grid)"/>
    <rect width="240" height="320" fill="url(#astro-glow)"/>
    ${rect(14, 12, 212, 296, "none", { stroke: border, sw: 0.7, opacity: 0.38 })}
    ${line(14, 27, 226, 27, border, 0.7, 0.42)}
    ${text(22, 23, "PASSPORT OS", 7.2, accent, { family: latin, weight: 600, spacing: 1.4 })}
    ${text(218, 23, "1.0.0", 7.2, dim, { family: mono, anchor: "end", spacing: 0.7 })}
    ${circle(120, 141, 82, "none", { stroke: border, sw: 0.6, opacity: 0.4 })}
    ${circle(120, 141, 68, "none", { stroke: border, sw: 0.8, opacity: 0.48 })}
    ${circle(120, 141, 54, surface, { stroke: border, sw: 0.8, opacity: 0.9 })}
    <path d="M 120 59 A 82 82 0 0 1 196.7 110" fill="none" stroke="${accent}" stroke-width="1.7" opacity=".9"/>
    ${ticks.join("")}
    ${nodes.join("")}
    ${circle(120, 141, 29, "none", { stroke: accent, sw: 0.8, opacity: 0.52, dash: "2 3" })}
    <path d="M120 104 L144 141 L120 178 L96 141 Z" fill="none" stroke="${border}" stroke-width=".6" opacity=".5"/>
    ${pillars.join("")}
    ${text(120, 236, "命海拾遗", 27, "#DDFBE4", { anchor: "middle", weight: 600, spacing: 5 })}
    ${text(120, 253, "FOUR PILLARS · ALMANAC", 7, dim, { family: latin, anchor: "middle", spacing: 1.8 })}
    ${progress(49, 275, 14, 11, 6, 9, accent, surface)}
    ${text(120, 300, "命理 · 历法 · 天气 · 声音", 7.2, dim, { anchor: "middle", spacing: 1.2 })}
  `;
}

function dawn() {
  const bg = "#03131B";
  const deep = "#092730";
  const surface = "#0B2C35";
  const gold = "#F0C76B";
  const cream = "#EEE7D6";
  const dim = "#8BA6A5";
  const cyan = "#7DD6D1";
  const stars = [];
  const rays = [];

  [
    [29, 58, 1.1, 0.58],
    [61, 39, 0.7, 0.38],
    [89, 71, 0.8, 0.48],
    [154, 43, 1.0, 0.46],
    [186, 69, 0.7, 0.42],
    [213, 37, 0.9, 0.5],
    [206, 109, 0.55, 0.3],
  ].forEach(([x, y, r, op]) => stars.push(circle(x, y, r, cream, { opacity: op })));

  for (let i = 0; i < 11; i += 1) {
    const angle = Math.PI + (i * Math.PI) / 10;
    const x1 = 120 + Math.cos(angle) * 47;
    const y1 = 151 + Math.sin(angle) * 47;
    const x2 = 120 + Math.cos(angle) * 60;
    const y2 = 151 + Math.sin(angle) * 60;
    rays.push(line(x1, y1, x2, y2, gold, 0.7, 0.34));
  }

  return `
    <defs>
      <linearGradient id="dawn-sky" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stop-color="#020B12"/>
        <stop offset=".58" stop-color="${bg}"/>
        <stop offset="1" stop-color="${deep}"/>
      </linearGradient>
      <radialGradient id="dawn-sun" cx="50%" cy="50%" r="50%">
        <stop offset="0" stop-color="#FFF0B1"/>
        <stop offset=".6" stop-color="${gold}"/>
        <stop offset="1" stop-color="${gold}" stop-opacity=".2"/>
      </radialGradient>
      <linearGradient id="dawn-water" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0" stop-color="${surface}" stop-opacity="0"/>
        <stop offset=".5" stop-color="${cyan}" stop-opacity=".2"/>
        <stop offset="1" stop-color="${surface}" stop-opacity="0"/>
      </linearGradient>
    </defs>
    <rect width="240" height="320" fill="url(#dawn-sky)"/>
    ${stars.join("")}
    ${circle(120, 151, 49, "none", { stroke: gold, sw: 0.6, opacity: 0.18, dash: "1 4" })}
    ${circle(120, 151, 38, "url(#dawn-sun)")}
    ${rays.join("")}
    ${line(29, 171, 211, 171, gold, 0.8, 0.8)}
    ${polyline("23,188 52,178 73,190 101,180 120,186 139,180 167,190 188,178 217,188", cyan, 0.9, 0.34)}
    ${polyline("17,203 49,192 81,204 120,194 159,204 191,192 223,203", gold, 0.8, 0.32)}
    ${polyline("28,218 67,207 101,219 120,213 139,219 173,207 212,218", cream, 0.5, 0.18)}
    <rect x="0" y="220" width="240" height="24" fill="url(#dawn-water)" opacity=".72"/>
    ${text(120, 27, "PASSPORT OS", 7.5, dim, { family: latin, anchor: "middle", weight: 600, spacing: 2.4 })}
    ${text(120, 240, "命海拾遗", 28, cream, { anchor: "middle", weight: 600, spacing: 5 })}
    ${text(120, 257, "从一日之始，拾起岁月", 7.8, dim, { anchor: "middle", spacing: 1.7 })}
    ${progress(50, 278, 14, 10.8, 5, 10, gold, surface)}
    ${text(120, 300, "ALMANAC · WEATHER · VOICE", 7, dim, { family: latin, anchor: "middle", spacing: 1.2 })}
  `;
}

function seal() {
  const paper = "#F1EEE6";
  const ink = "#252D30";
  const dim = "#66716D";
  const red = "#A5315B";
  const faint = "#D5CEC3";
  const grain = [];
  for (let y = 18; y < 310; y += 19) {
    grain.push(line(17, y, 223, y, faint, 0.35, 0.24));
  }
  return `
    <defs>
      <pattern id="seal-grid" width="24" height="24" patternUnits="userSpaceOnUse">
        <path d="M24 0H0V24" fill="none" stroke="${faint}" stroke-width=".35" opacity=".3"/>
      </pattern>
    </defs>
    <rect width="240" height="320" fill="${paper}"/>
    <rect width="240" height="320" fill="url(#seal-grid)"/>
    ${grain.join("")}
    ${rect(14, 14, 212, 292, "none", { stroke: ink, sw: 0.7, opacity: 0.28 })}
    ${rect(20, 20, 200, 280, "none", { stroke: red, sw: 0.55, opacity: 0.42 })}
    ${text(25, 34, "PASSPORT OS", 7, ink, { family: latin, weight: 600, spacing: 1.4 })}
    ${text(215, 34, "卷一 / 1.0.0", 6.8, dim, { anchor: "end", spacing: 0.7 })}
    ${rect(35, 55, 82, 91, red, { rx: 1 })}
    ${rect(40, 60, 72, 81, "none", { stroke: "#F1D9E1", sw: 0.8, opacity: 0.62 })}
    ${text(76, 105, "命", 42, paper, { anchor: "middle", weight: 700 })}
    ${text(76, 132, "海", 20, paper, { anchor: "middle", weight: 600, spacing: 2 })}
    ${text(139, 63, "干支", 6.3, dim, { spacing: 0.8 })}
    ${text(139, 76, "历法", 6.3, dim, { spacing: 0.8 })}
    ${line(139, 83, 204, 83, ink, 0.5, 0.38)}
    ${text(139, 98, "丙", 13, ink, { anchor: "middle" })}
    ${text(154, 98, "午", 13, ink, { anchor: "middle" })}
    ${text(169, 98, "庚", 13, ink, { anchor: "middle" })}
    ${text(184, 98, "子", 13, ink, { anchor: "middle" })}
    ${text(139, 114, "年", 6, dim, { anchor: "middle" })}
    ${text(154, 114, "月", 6, dim, { anchor: "middle" })}
    ${text(169, 114, "日", 6, dim, { anchor: "middle" })}
    ${text(184, 114, "时", 6, dim, { anchor: "middle" })}
    ${line(139, 123, 204, 123, ink, 0.5, 0.28)}
    ${text(139, 136, "离线命理", 6.8, dim, { spacing: 1 })}
    ${text(120, 188, "命海拾遗", 27, ink, { anchor: "middle", weight: 600, spacing: 5 })}
    ${line(61, 201, 179, 201, red, 0.8, 0.52)}
    ${text(120, 216, "一页历法，一方随身", 8, dim, { anchor: "middle", spacing: 1.4 })}
    ${rect(48, 241, 144, 23, "none", { stroke: ink, sw: 0.7, opacity: 0.38 })}
    ${text(120, 256, "PASSPORT · ALMANAC · 1.0", 7.2, ink, { family: latin, anchor: "middle", spacing: 1.1 })}
    ${progress(55, 282, 12, 11, 4, 8, red, "#D8D0C5")}
    ${text(120, 299, "以今日为始", 6.8, dim, { anchor: "middle", spacing: 2 })}
  `;
}

function pixelTravel() {
  const bg = "#181B1C";
  const surface = "#202526";
  const border = "#59605D";
  const gold = "#DDB36C";
  const cream = "#EEE7D6";
  const dim = "#B3B2A5";
  const green = "#9DB89A";
  const route = [];
  for (let x = 35; x <= 205; x += 10) {
    const t = (x - 35) / 170;
    const y = 170 - Math.sin(t * Math.PI * 2) * 16 + Math.sin(t * Math.PI * 4) * 5;
    route.push(circle(x, y, 0.8, gold, { opacity: 0.32 }));
  }
  return `
    <defs>
      <pattern id="travel-dots" width="6" height="6" patternUnits="userSpaceOnUse">
        <circle cx="1" cy="1" r=".45" fill="${border}" opacity=".24"/>
      </pattern>
    </defs>
    <rect width="240" height="320" fill="${bg}"/>
    <rect width="240" height="320" fill="url(#travel-dots)"/>
    ${rect(14, 13, 212, 294, "none", { stroke: border, sw: 0.8, opacity: 0.58 })}
    ${text(22, 29, "TRAVEL / 01", 7, dim, { family: mono, weight: 600, spacing: 1.3 })}
    ${text(218, 29, "PASSPORT OS", 7, gold, { family: latin, anchor: "end", spacing: 1.3 })}
    ${line(14, 36, 226, 36, border, 0.7, 0.7)}
    ${rect(53, 53, 134, 141, surface, { stroke: border, sw: 1.2, rx: 2 })}
    ${rect(59, 59, 122, 129, "none", { stroke: gold, sw: 0.7, opacity: 0.48, rx: 1 })}
    ${line(53, 73, 187, 73, border, 0.8, 0.85)}
    ${text(65, 68, "PASSPORT", 7.4, cream, { family: latin, weight: 600, spacing: 1.5 })}
    ${text(175, 68, "C3 / 8M", 5.8, dim, { family: mono, anchor: "end", spacing: 0.5 })}
    ${text(74, 119, "命", 39, gold, { anchor: "middle", weight: 600 })}
    ${line(98, 93, 98, 145, border, 0.7, 0.7)}
    ${text(112, 101, "命海拾遗", 13.5, cream, { weight: 600, spacing: 1.5 })}
    ${text(112, 116, "A POCKET ALMANAC", 5.5, dim, { family: latin, spacing: 1 })}
    ${text(112, 137, "NO. 03·024", 6.2, green, { family: mono, spacing: 0.7 })}
    ${rect(68, 155, 22, 23, "none", { stroke: cream, sw: 0.7, opacity: 0.42 })}
    ${circle(79, 166.5, 4.5, "none", { stroke: cream, sw: 0.7, opacity: 0.54 })}
    ${line(68, 171, 90, 171, cream, 0.5, 0.36)}
    ${text(101, 162, "干支 · 农历 · 节气", 6.4, dim, { spacing: 0.8 })}
    ${text(101, 176, "四方天气尽在一册", 6.4, dim, { spacing: 0.5 })}
    ${route.join("")}
    ${text(120, 235, "命海拾遗", 27, cream, { anchor: "middle", weight: 600, spacing: 5 })}
    ${text(120, 252, "TRAVEL INSTRUMENT", 7, gold, { family: latin, anchor: "middle", spacing: 2.4 })}
    ${progress(49, 273, 14, 11, 6, 11, gold, surface)}
    ${text(31, 300, "31.2N", 6, dim, { family: mono })}
    ${text(209, 300, "121.5E", 6, dim, { family: mono, anchor: "end" })}
    ${text(120, 300, "行李已就绪", 6.8, dim, { anchor: "middle", spacing: 1.5 })}
  `;
}

const designs = [
  { id: "01", slug: "astrolabe", title: "星轨四柱", content: astrolabe() },
  { id: "02", slug: "dawn", title: "命海日出", content: dawn() },
  { id: "03", slug: "seal", title: "朱砂册页", content: seal() },
  { id: "04", slug: "pixel", title: "像素驿站", content: pixelTravel() },
];

const svg = (content, width = W, height = H) =>
  `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${W} ${H}">${content}</svg>\n`;

for (const design of designs) {
  writeFileSync(join(root, `${design.id}-${design.slug}.svg`), svg(design.content));
}

const panelW = 280;
const panelH = 405;
const gap = 20;
const overviewW = 4 * panelW + 5 * gap;
const overviewH = 500;
const panels = designs
  .map((design, index) => {
    const x = gap + index * (panelW + gap);
    return `
      <g>
        ${text(x + 20, 54, `${design.id} / ${design.title}`, 16, "#EDE8DD", {
          weight: 600,
          spacing: 1.2,
        })}
        <rect x="${x + 20}" y="71" width="240" height="320" fill="#000" stroke="#414743" stroke-width="1.5"/>
        <svg x="${x + 20}" y="71" width="240" height="320" viewBox="0 0 ${W} ${H}">${design.content}</svg>
        ${text(x + 20, 426, ["代码绘制 · 中", "自定义绘制 · 中高", "代码绘制 · 低", "代码绘制 · 低"][index], 10, "#8E9993", {
          spacing: 0.8,
        })}
      </g>
    `;
  })
  .join("");

const overview = `
<svg xmlns="http://www.w3.org/2000/svg" width="${overviewW}" height="${overviewH}" viewBox="0 0 ${overviewW} ${overviewH}">
  <rect width="${overviewW}" height="${overviewH}" fill="#0D100F"/>
  ${text(38, 31, "PASSPORT OS · BOOT SPLASH DIRECTIONS", 18, "#EEE7D6", {
    family: latin,
    weight: 600,
    spacing: 1.7,
  })}
  ${text(overviewW - 40, 31, "240 × 320 · 2× PREVIEW", 10, "#8E9993", {
    family: mono,
    anchor: "end",
    spacing: 0.8,
  })}
  ${line(40, 44, overviewW - 40, 44, "#414743", 1, 0.8)}
  ${panels}
  ${line(40, 458, overviewW - 40, 458, "#414743", 0.8, 0.65)}
  ${text(40, 480, "命海拾遗 / 四款启动画面概念 · 静态效果稿", 10, "#B3B2A5", { spacing: 1 })}
</svg>
`;

writeFileSync(join(root, "00-overview.svg"), overview);
console.log(`Generated ${designs.length} concepts and overview in ${root}`);
