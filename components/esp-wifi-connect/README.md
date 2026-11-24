# ESP Wi-Fi Connect

This component owns Wi-Fi station profiles, connection retries, scanning and
the configuration access point. It deliberately does not embed a web UI:
Passport OS serves the phone setup page from `main/apps/home`, so there is only
one HTTP server and one configuration flow.

## Owned resources

- Wi-Fi station state and internet probing
- Saved network profiles in NVS
- The `Passport-WiFi-xxxx` configuration AP and captive DNS
- Scan results for the on-device Wi-Fi picker and phone setup page
- Bounded connection attempts and disconnect diagnostics

## Typical use

```cpp
WifiManagerConfig config;
config.ssid_prefix = "Passport-WiFi";
WifiManager::GetInstance().Initialize(config);
WifiManager::GetInstance().StartStation();

// Device-owned setup UI:
WifiManager::GetInstance().StartConfigAp();
const std::string ssid = WifiManager::GetInstance().GetApSsid();
const std::string ip = WifiManager::GetInstance().GetIpAddress();

bool ok = WifiManager::GetInstance().ConfigureNetwork(ssid, password, true);
WifiManager::GetInstance().StopConfigAp();
```

`StartConfigAp(true)` selects station-only mode for the on-device Wi-Fi picker.
The default starts the configuration AP with captive DNS.

The project-level setup page is available at `http://192.168.4.1` while the AP
is active. It is implemented in `main/apps/home/home_upload.cc` and
`main/apps/home/phone_setup.html`, not in this component.

## NVS

Saved networks are stored under the `wifi` namespace. The component never logs
Wi-Fi passwords. Credentials and personal data must not be committed to the
repository or included in issue reports.
