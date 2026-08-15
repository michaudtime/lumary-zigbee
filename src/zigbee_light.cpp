#include "zigbee_light.h"
#include "config.h"
#include "scene_store.h"
#include "effect_params.h"
#include <Arduino.h>
#include "Zigbee.h"

static LightState s_state;

static void apply_effect(uint8_t index);

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
        light_state_set_color(&s_state, CRGB{r, g, b});   // moves out of scene mode
    }
}

// Fires when the coordinator drives the Colour Control cluster in colour
// temperature mode -- i.e. the white slider in Home Assistant. Routed straight
// to the CW/WW string, which is what actually makes white in this fixture.
static void on_light_change_temp(bool state, uint8_t level, uint16_t mireds) {
    s_state.on    = state;
    s_state.level = level;
    light_state_set_cct(&s_state, mireds);
}

static void on_identify(uint16_t time) {
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
    static bool s_ota_requested = false;
    if (!s_ota_requested && Zigbee.connected()) {
        s_ep.requestOTAUpdate();
        s_ota_requested = true;
        log_i("Zigbee joined; OTA update requested");
    }
}

const LightState* zigbee_light_state() {
    return &s_state;
}

// Runs on the Zigbee task. light_state_set_scene rejects an out-of-range index
// outright, so re-read the state rather than trusting what was asked for --
// otherwise a bad write would persist a scene the effect engine can't render.
static void apply_effect(uint8_t index) {
    light_state_set_scene(&s_state, index, EFFECT_COUNT);
    if (s_state.scene != index) {
        log_w("Ignored out-of-range effect %u", index);
        return;
    }
    scene_store_set_active(s_state.scene);

    // Mirror it back into the attribute so a read -- and therefore the Z2M/HA
    // control -- reports what is actually running, including after a reboot or
    // a selection made locally.
    esp_zb_zcl_set_attribute_val(LIGHT_ENDPOINT, LUMARY_CLUSTER_ID,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 LUMARY_ATTR_EFFECT, &s_state.scene, false);

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
