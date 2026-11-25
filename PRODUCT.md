# Passport OS

<!-- impeccable:product-schema 1 -->

## Platform

Embedded ESP32-C3 firmware with LVGL on a 240×320 screen. This is not a web, iOS or Android application. The browser artifact is a separate interaction preview.

## Users and purpose

The owner and people using the same AI Passport hardware use its physical buttons, display, microphone and speaker for Xiaozhi voice control, radio, music, reminders, lists, timers, Bazi, Liuyao and other small utilities.

## Operating context

The device has three physical buttons. Wi-Fi and personal information are configured through a phone web page. Xiaozhi is the voice-control entry point; media and account-backed services need their own configuration.

## Capabilities and constraints

- ESP32-C3, 8 MB Flash, no PSRAM; preserve memory for Wi-Fi, encrypted connections and recording.
- App switching must release foreground resources. No black "returning" screens.
- Default system theme is classic green; keep the alternative travel theme and existing button semantics.
- Liuyao uses an app-local landscape rotation and must restore portrait before leaving.

## Evidence

Current firmware source, `DESIGN.md`, `docs/CURRENT.md`, and local diagnostics document implementation and validation. Browser simulations, host LVGL snapshots and physical-device tests must be labelled separately.
