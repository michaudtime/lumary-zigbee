#include "zigbee_light.h"
#include "config.h"
#include "scene_store.h"
#include "effect_params.h"
#include "identify.h"
#include <Arduino.h>
#include <string.h>
#include "Zigbee.h"

static LightState s_state;

// Written on the Zigbee task, read on the Arduino render task. A single
// aligned 32-bit word needs no mutex, but the volatile is load-bearing.
static volatile uint32_t s_identify_until = 0;

static void apply_effect(uint8_t index);
static void publish_effect_attr();

// ZigbeeColorDimmableLight plus one manufacturer-specific cluster carrying the
// effect index. The Arduino wrapper has no cluster-building API, so the
// constructor reaches _cluster_list (protected on ZigbeeEP) and uses the raw
// esp_zb calls -- the same pattern the base class itself uses to bolt extra
// attributes onto Colour Control.
//
// The attribute is READ-ONLY on purpose. Selection comes in as a command
// instead, because zbAttributeSet is private in the base class: a subclass may
// override it but cannot call it, so intercepting attribute writes would strand
// on/off, level and colour with no handler at all.
class LumaryLight : public ZigbeeColorDimmableLight {
public:
    explicit LumaryLight(uint8_t endpoint) : ZigbeeColorDimmableLight(endpoint) {
        uint8_t effect = 0;
        esp_zb_attribute_list_t* custom = esp_zb_zcl_attr_list_create(LUMARY_CLUSTER_ID);
        esp_zb_custom_cluster_add_custom_attr(custom, LUMARY_ATTR_EFFECT,
                                              ESP_ZB_ZCL_ATTR_TYPE_U8,
                                              ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
                                              &effect);
        esp_zb_cluster_list_add_custom_cluster(_cluster_list, custom,
                                               ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, FW_VERSION_STRING);
        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, FW_DATE_CODE);
    }

    // Push the fixture's true state to the coordinator. Nothing else does this
    // after a reboot, so Z2M keeps showing whatever it last saw -- typically
    // "on" for a light that came back off, which it then reports to HA.
    //
    // Deliberately not built on setLightState()/setLightLevel(): those no-op
    // when the value is unchanged (so at boot, where off == off, they would
    // push nothing at all), and when the value HAS changed they re-enter our
    // own light-changed callback, which would drag the light into MODE_COLOR.
    // Writing the attributes and reporting them directly avoids both.
    void publishState(bool on, uint8_t level, uint8_t effect) {
        uint8_t on_val = on ? 1 : 0;
        setAttr(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on_val);
        setAttr(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &level);
        setAttr(LUMARY_CLUSTER_ID, LUMARY_ATTR_EFFECT, &effect);

        reportAttr(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                   ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
        reportAttr(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                   ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
        // The effect attribute is deliberately NOT reported. esp_zb rejects it
        // with ESP_ERR_NOT_SUPPORTED unless the attribute carries
        // ESP_ZB_ZCL_ATTR_ACCESS_REPORTING -- and adding that flag to a custom
        // cluster attribute makes Zigbee.begin() hang before it ever starts the
        // stack (bisected on hardware 2026-08-15: with the flag, setup() reached
        // "LED driver init ok" and never logged "Zigbee started"; without it,
        // joined at 134 ms). The value is still written above, so a READ returns
        // the truth, which is what the Z2M converter's convertGet uses.
    }

private:
    // ZCL character strings are length-prefixed, not null-terminated: byte 0 is
    // the length. Same encoding the base class does by hand for manufacturer
    // and model. Without these two attributes HA's device page reads
    // "Firmware: unknown" and the update card shows a bare integer.
    void addBasicStringAttr(uint16_t attr_id, const char* value) {
        char zcl[ZB_MAX_NAME_LENGTH + 2];
        const size_t len = strlen(value);
        if (len > ZB_MAX_NAME_LENGTH) {
            log_e("Basic attr 0x%04x too long (%u)", attr_id, unsigned(len));
            return;
        }
        zcl[0] = char(len);
        memcpy(zcl + 1, value, len);
        zcl[len + 1] = '\0';

        esp_zb_attribute_list_t* basic = esp_zb_cluster_list_get_cluster(
            _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        if (basic == nullptr) {
            log_e("No basic cluster for attr 0x%04x", attr_id);
            return;
        }
        const esp_err_t ret = esp_zb_basic_cluster_add_attr(basic, attr_id, (void*)zcl);
        if (ret != ESP_OK) {
            log_e("Failed to add basic attr 0x%04x: %s", attr_id, esp_err_to_name(ret));
        }
    }

    void setAttr(uint16_t cluster, uint16_t attr, void* value) {
        esp_zb_zcl_set_attribute_val(_endpoint, cluster,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     attr, value, false);
    }

    void reportAttr(uint16_t cluster, uint16_t attr) {
        // Addressed at the coordinator explicitly rather than left to the
        // binding table (ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT, which
        // is what the library's sensor endpoints use). This device was
        // interviewed by Z2M under a generic definition before the external
        // converter existed, so the bindings a sensor would rely on may never
        // have been configured -- and a report sent into an empty binding table
        // is dropped silently, which is exactly what was happening.
        esp_zb_zcl_report_attr_cmd_t cmd = {};
        cmd.zcl_basic_cmd.src_endpoint          = _endpoint;
        cmd.zcl_basic_cmd.dst_endpoint          = 1;        // coordinator
        cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;   // coordinator
        cmd.address_mode     = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        cmd.clusterID        = cluster;
        cmd.attributeID      = attr;
        cmd.direction        = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        cmd.manuf_specific   = 0x00U;
        cmd.dis_default_resp = 0x00U;
        cmd.manuf_code       = 0x0000U;
        reportClusterAttribute(&cmd);
    }
};

static LumaryLight s_ep(LIGHT_ENDPOINT);

// The endpoint reports state, level and colour together on every change, so the
// only way to tell a colour command from a plain dim is to compare against the
// last colour we saw. Without this, nudging the brightness would kick the light
// out of whatever scene it was running.
static uint8_t s_last_r = 255, s_last_g = 255, s_last_b = 255;

// Runs on the Zigbee task, not in loop(). Every field it touches is byte-sized
// and the reader only renders frames from them, so the worst a race can do is
// show one frame of mixed state -- not worth a mutex on the render path.
static void on_light_change_rgb(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
    s_state.on    = state;
    s_state.level = level;
    if (r != s_last_r || g != s_last_g || b != s_last_b) {
        s_last_r = r;
        s_last_g = g;
        s_last_b = b;
        const LightMode was = s_state.mode;
        light_state_set_color(&s_state, CRGB{r, g, b});   // moves out of scene mode
        if (was == MODE_SCENE) publish_effect_attr();     // ...so stop naming one
    }
}

// Fires when the coordinator drives the Colour Control cluster in colour
// temperature mode -- i.e. the white slider in Home Assistant. Routed straight
// to the CW/WW string, which is what actually makes white in this fixture.
static void on_light_change_temp(bool state, uint8_t level, uint16_t mireds) {
    s_state.on    = state;
    s_state.level = level;
    const LightMode was = s_state.mode;
    light_state_set_cct(&s_state, mireds);
    if (was == MODE_SCENE) publish_effect_attr();
}

// Identify is an overlay: it does not touch LightState, so when the deadline
// passes the fixture resumes whatever it was doing with no restore step.
//
// ZCL treats IdentifyTime = 0 as "stop identifying", which is how a
// coordinator cancels. Encoding it as a deadline of now makes
// identify_active() false immediately, rather than scheduling a zero-length
// blink that would leave the ring lit for one frame.
static void on_identify(uint16_t time) {
    const uint32_t now = millis();
    s_identify_until = (time == 0) ? now : now + uint32_t(time) * 1000;
    log_i("Zigbee identify for %us", time);
}

// Effect selection. Runs on the Zigbee task. Everything is validated before it
// reaches light_state, because this payload comes straight off the air.
static void on_custom_command(const esp_zb_zcl_custom_cluster_command_message_t* message) {
    if (message->info.cluster != LUMARY_CLUSTER_ID) return;
    if (message->info.command.id != LUMARY_CMD_SET_EFFECT) {
        log_w("Ignored unknown Lumary command 0x%02x", message->info.command.id);
        return;
    }
    if (message->data.value == nullptr || message->data.size < 1) {
        log_w("Ignored empty set-effect command");
        return;
    }
    apply_effect(*(uint8_t*)message->data.value);
}

void zigbee_light_init() {
    light_state_init(&s_state);
    s_state.scene = scene_store_get_active();

    s_ep.onLightChangeRgb(on_light_change_rgb);
    s_ep.onLightChangeTemp(on_light_change_temp);
    s_ep.onIdentify(on_identify);
    s_ep.onCustomClusterCommand(on_custom_command);
    s_ep.setManufacturerAndModel("Lumary", "LumaryBrainRevA");

    // Advertise colour temperature alongside colour, then publish the range the
    // fixture can actually reach. Both must happen before the stack starts, and
    // the range setter refuses to run unless the capability bit is already set.
    s_ep.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION
                                 | ZIGBEE_COLOR_CAPABILITY_X_Y
                                 | ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
    if (!s_ep.setLightColorTemperatureRange(CCT_MIRED_COOL, CCT_MIRED_WARM)) {
        log_e("Failed to publish colour temperature range");
    }

    // Zigbee OTA. The coordinator only offers images numbered above the running
    // version, so ZB_FW_VERSION must match the .ota image's --file-version.
    if (!s_ep.addOTAClient(ZB_FW_VERSION, ZB_FW_VERSION_DL, ZB_HW_VERSION,
                           ZB_MANUFACTURER_CODE, ZB_IMAGE_TYPE)) {
        log_e("Failed to add OTA client");
    }

    Zigbee.addEndpoint(&s_ep);

    // Router, not end device: these are mains-powered ceiling fixtures, so each
    // one should extend the mesh for the others.
    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        // Deliberately not fatal. A light that can't reach the network must
        // still turn on locally, so carry on rendering and let the caller retry.
        log_e("Zigbee failed to start; continuing with local control only");
        return;
    }
    log_i("Zigbee started, waiting for network");
}

bool zigbee_light_connected() {
    return Zigbee.connected();
}

void zigbee_light_loop() {
    // The OTA query can only be issued once we're on a network. After this first
    // request the stack re-queries hourly on its own.
    static bool s_joined_once = false;
    if (!s_joined_once && Zigbee.connected()) {
        s_joined_once = true;
        s_ep.requestOTAUpdate();
        log_i("Zigbee joined; OTA update requested");

        // Tell the coordinator what we actually are, before anything asks. The
        // light boots off with the stored effect, and the effect attribute is
        // built during static init -- before setup() opens NVS -- so without
        // this both are wrong: HA shows the pre-reboot on/off state, and a read
        // reports effect 0 whatever is really running.
        const uint8_t effect = light_state_effect_value(&s_state);
        s_ep.publishState(s_state.on, s_state.level, effect);
        log_i("Published state: on=%d level=%u effect=%u",
              s_state.on, s_state.level, effect);
    }
}

const LightState* zigbee_light_state() {
    return &s_state;
}

// Mirrors the running effect into the attribute so a read -- which is all the
// coordinator can do, the attribute being unreportable -- returns the truth.
// Called on transitions OUT of effect mode as well as into one: without that
// half the attribute goes on naming the last effect while the ring holds a
// static colour, and Home Assistant's dropdown shows an effect that stopped.
static void publish_effect_attr() {
    uint8_t value = light_state_effect_value(&s_state);
    esp_zb_zcl_set_attribute_val(LIGHT_ENDPOINT, LUMARY_CLUSTER_ID,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 LUMARY_ATTR_EFFECT, &value, false);
}

// Runs on the Zigbee task. light_state_set_scene rejects an out-of-range index
// outright, so re-read the state rather than trusting what was asked for --
// otherwise a bad write would persist a scene the effect engine can't render.
static void apply_effect(uint8_t index) {
    if (index == LIGHT_EFFECT_NONE) {
        // "None" in the Home Assistant dropdown: stop the effect and hold the
        // colour already on show. Deliberately NOT persisted -- the stored
        // active scene is what a power cycle should come back to, and
        // light_state_init() boots in MODE_SCENE regardless, so persisting
        // "none" would only make the two disagree.
        light_state_clear_scene(&s_state);
        publish_effect_attr();
        log_i("Effect cleared; holding the current colour");
        return;
    }

    light_state_set_scene(&s_state, index, EFFECT_COUNT);
    if (s_state.scene != index) {
        log_w("Ignored out-of-range effect %u", index);
        return;
    }
    scene_store_set_active(s_state.scene);
    publish_effect_attr();
    zigbee_light_report();
    log_i("Effect %u selected", s_state.scene);
}

void zigbee_light_set_effect(uint8_t index) {
    apply_effect(index);
}

void zigbee_light_report() {
    s_ep.setLightState(s_state.on);
    s_ep.setLightLevel(s_state.level);
}

uint32_t zigbee_light_identify_until() {
    return s_identify_until;
}
