// Exercise the actual offline phone flow after removing avatar/card uploads.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const html = fs.readFileSync('main/apps/home/phone_setup.html', 'utf8');
const cityCatalog = JSON.parse(fs.readFileSync('main/apps/home/cities.json', 'utf8'));
const script = html.match(/<script>\r?\n([\s\S]*?)<\/script>/)[1];

for (const forbidden of ['/api/avatar', '/api/share', '/api/cards', 'portraitSection', 'cardSection']) {
  assert.ok(!html.includes(forbidden), `removed image feature remains: ${forbidden}`);
}
assert.ok(!/<(?:script|img|link)\b[^>]*(?:src|href)=["']https?:|url\(["']?https?:/i.test(html),
  'the captive page must not load internet resources');

{
  const line = script.split('\n').find(value => value.includes('pt=('));
  for (const [cookie, token] of [['a=1; pt=4294967295', '4294967295'], ['xpt=9', '']]) {
    const window = {};
    vm.runInNewContext(line, { window, document: { cookie } });
    assert.equal(window.__T, token);
  }
}

const elements = new Map();
const requests = [];
const timers = new Map();
let nextTimer = 1;

class Element {
  constructor() {
    this.value = '';
    this.textContent = '';
    this.style = {};
    this.dataset = {};
    this.options = [];
    this.hidden = false;
    this.disabled = false;
    this.classList = { toggle() {} };
    this.type = 'password';
  }
  add(option) { this.options.push(option); }
  replaceChildren(...options) {
    this.options = options;
    this.value = '';
  }
  setAttribute(key, value) { this[key] = value; }
  removeAttribute(key) { delete this[key]; }
  focus() {}
  get selectedOptions() { return this.options.filter(option => option.value === this.value); }
}

const get = id => {
  if (!elements.has(id)) elements.set(id, new Element());
  return elements.get(id);
};

let state = 'idle';
let saved = false;
let emptyScan = true;
let failScan = false;
let connectError = '';
let denyFinish = false;
let savedProfile = {
  name: '测试',
  signature: '已有签名',
  address: '原地址',
  city: '珠海',
  region: '广东',
  radio_city: '珠海',
  country_code: 'CN',
  lat: '22.2707',
  lon: '113.5767',
};

const phoneWindow = {
  __T: 314159,
  scrollTo() {},
  addEventListener() {},
  isSecureContext: false,
  crypto: require('node:crypto').webcrypto,
  location: { origin: 'http://192.168.4.1' },
};

const context = vm.createContext({
  document: { getElementById: get, createElement: () => new Element() },
  window: phoneWindow,
  navigator: {},
  Option: class {
    constructor(text, value) {
      this.text = text;
      this.value = value;
      this.dataset = {};
    }
  },
  TextEncoder,
  AbortController,
  URL,
  URLSearchParams,
  setTimeout(fn, ms) {
    const id = nextTimer++;
    timers.set(id, { fn, ms });
    return id;
  },
  clearTimeout: id => timers.delete(id),
  fetch: async (url, options) => {
    requests.push({ url, options });
    let result = {};
    let ok = true;
    if (url.startsWith('/api/cities')) result = cityCatalog;
    if (url.startsWith('/api/profile') && !options.method) result = savedProfile;
    if (url.startsWith('/api/setup')) {
      result = {
        state,
        saved_network: saved,
        saved_networks: saved ? ['Absent Wi-Fi', 'Test Wi-Fi'] : [],
        ssid: 'Test Wi-Fi',
      };
    }
    if (url.startsWith('/api/connect') && connectError) {
      ok = false;
      result = { error: connectError };
    }
    if (url.startsWith('/api/networks')) {
      if (failScan) throw new Error('offline');
      result = emptyScan ? [] : [{ ssid: 'Test Wi-Fi', secure: true }];
    }
    if (url.startsWith('/api/finish') && denyFinish) {
      ok = false;
      result = { error: 'network_required' };
    }
    return { ok, json: async () => result };
  },
});

vm.runInContext(script, context);
const flush = () => new Promise(resolve => setImmediate(resolve));
const posts = () => requests.filter(request => request.options.method === 'POST');

(async () => {
  await flush();
  await flush();

  assert.match(get('scanStatus').textContent, /等待扫描结果/);
  const retry = [...timers.entries()].find(([, timer]) => timer.ms === 1500);
  assert.ok(retry, 'empty first scan schedules a retry');
  emptyScan = false;
  timers.delete(retry[0]);
  await retry[1].fn();
  assert.match(get('scanStatus').textContent, /已找到 1/);

  get('network').value = 'Test Wi-Fi';
  failScan = true;
  await context.scan();
  assert.equal(get('network').value, 'Test Wi-Fi', 'failed refresh preserves selection');
  failScan = false;
  await context.scan();

  await get('save').onclick();
  assert.equal(posts().length, 0, 'first boot cannot complete without Wi-Fi');
  get('network').value = 'Test Wi-Fi';
  get('password').value = 'short';
  await get('connect').onclick();
  assert.equal(posts().length, 0, 'invalid password is rejected locally');
  get('password').value = '  secret with spaces  ';
  await get('connect').onclick();

  let request = posts().at(-1);
  assert.equal(request.url, '/api/connect?t=314159');
  assert.equal(JSON.parse(request.options.body).password, '  secret with spaces  ');
  state = 'failed';
  await context.poll();
  assert.equal(get('connect').disabled, false);
  assert.match(get('networkStatus').textContent, /检查密码/);
  await get('connect').onclick();
  state = 'connected';
  await context.poll();
  assert.equal(get('step2').hidden, false);
  assert.equal(get('password').value, '');
  assert.equal(get('step1').hidden, true);

  get('name').value = '新名字';
  denyFinish = true;
  await get('save').onclick();
  assert.match(get('saveStatus').textContent, /连接 Wi-Fi/);
  denyFinish = false;
  await get('save').onclick();
  assert.equal(get('step3').hidden, false);
  assert.equal(get('doneName').textContent, '新名字');
  request = posts().find(entry => entry.url.startsWith('/api/profile'));
  const profile = JSON.parse(request.options.body);
  assert.equal(profile.name, '新名字');
  assert.equal(profile.address, '原地址');
  assert.equal(profile.city, '珠海');
  assert.equal(profile.lat, '22.2707');
  assert.ok(posts().some(entry => entry.url.startsWith('/api/finish')));

  state = 'connecting';
  saved = true;
  await context.load();
  assert.equal(get('connect').disabled, true);
  assert.equal(get('reuse').hidden, false);
  assert.equal(get('savedField').hidden, false);
  assert.equal(get('savedNetwork').disabled, true);
  state = 'failed';
  await context.poll();
  get('savedNetwork').value = 'Test Wi-Fi';
  await get('reuse').onclick();
  const reuse = JSON.parse(posts().at(-1).options.body);
  assert.equal(reuse.saved, true);
  assert.equal(reuse.ssid, 'Test Wi-Fi');
  assert.ok(Number.isInteger(reuse.epoch));

  state = 'failed';
  await context.poll();
  get('savedNetwork').value = '';
  connectError = 'saved_not_found';
  await get('reuse').onclick();
  assert.equal(get('reuse').disabled, false);
  assert.match(get('networkStatus').textContent, /已保存网络/);
  connectError = '';
  get('keepSaved').onclick();
  assert.equal(get('step2').hidden, false);
  assert.match(get('connectionSummary').textContent, /完成后自动重连/);

  assert.ok(cityCatalog.length >= 3000);
  assert.equal(new Set(cityCatalog.map(city => city[4])).size, 34);
  get('province').value = '广东';
  get('province').onchange();
  get('citySearch').value = '珠海';
  get('citySearch').oninput();
  const zhuhai = cityCatalog.find(city => city[1] === '珠海' && city[4] === '广东');
  get('city').value = zhuhai[0];
  get('city').onchange();
  await get('save').onclick();
  const cityProfile = JSON.parse(posts().filter(entry => entry.url.startsWith('/api/profile')).at(-1).options.body);
  assert.deepEqual(
    {
      city: cityProfile.city,
      region: cityProfile.region,
      radio_city: cityProfile.radio_city,
      country_code: cityProfile.country_code,
      lat: cityProfile.lat,
      lon: cityProfile.lon,
    },
    { city: '珠海', region: '广东', radio_city: '珠海', country_code: 'CN', lat: '22.2707', lon: '113.5767' },
  );

  get('city').value = '__custom';
  get('city').onchange();
  get('customCity').value = '测试新区';
  get('customLat').value = '91';
  get('customLon').value = '113.6';
  const beforeInvalid = posts().length;
  await get('save').onclick();
  assert.equal(posts().length, beforeInvalid);
  assert.match(get('saveStatus').textContent, /有效/);

  console.log('Phone setup: Wi-Fi, saved-network fallback, cookies, nationwide cities and text profile: PASS');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
