#include "scene_store.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static nvs_handle_t s_nvs;

static void scene_key(uint8_t index, const char* field, char* out) {
    snprintf(out, 16, "s%u_%s", index, field);
}

void scene_store_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);

    uint8_t ver = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_FMT_VER, &ver) != ESP_OK || ver != NVS_FMT_VER_CURRENT) {
        for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
            scene_store_save(i, &kDefaultParams[i]);
        }
        nvs_set_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, 0);
        nvs_set_u8(s_nvs, NVS_KEY_FMT_VER, NVS_FMT_VER_CURRENT);
        nvs_commit(s_nvs);
    }
}

uint8_t scene_store_get_active() {
    uint8_t idx = 0;
    nvs_get_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, &idx);
    if (idx >= NVS_MAX_SCENES) idx = 0;
    return idx;
}

void scene_store_set_active(uint8_t index) {
    if (index >= NVS_MAX_SCENES) index = 0;
    nvs_set_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, index);
    nvs_commit(s_nvs);
}

bool scene_store_load(uint8_t index, EffectParams* out) {
    if (index >= NVS_MAX_SCENES) { *out = kDefaultParams[0]; return false; }
    char key[16];
    uint8_t type, hue, sat, bri, spd;

    scene_key(index, "type", key);
    if (nvs_get_u8(s_nvs, key, &type) != ESP_OK) {
        *out = (index < EFFECT_COUNT) ? kDefaultParams[index] : kDefaultParams[0];
        return false;
    }
    scene_key(index, "hue",  key); nvs_get_u8(s_nvs, key, &hue);
    scene_key(index, "sat",  key); nvs_get_u8(s_nvs, key, &sat);
    scene_key(index, "bri",  key); nvs_get_u8(s_nvs, key, &bri);
    scene_key(index, "spd",  key); nvs_get_u8(s_nvs, key, &spd);

    out->type       = (EffectType)type;
    out->hue        = hue;
    out->sat        = sat;
    out->brightness = bri;
    out->speed      = spd;
    return true;
}

void scene_store_save(uint8_t index, const EffectParams* p) {
    if (index >= NVS_MAX_SCENES) return;
    char key[16];
    scene_key(index, "type", key); nvs_set_u8(s_nvs, key, (uint8_t)p->type);
    scene_key(index, "hue",  key); nvs_set_u8(s_nvs, key, p->hue);
    scene_key(index, "sat",  key); nvs_set_u8(s_nvs, key, p->sat);
    scene_key(index, "bri",  key); nvs_set_u8(s_nvs, key, p->brightness);
    scene_key(index, "spd",  key); nvs_set_u8(s_nvs, key, p->speed);
    nvs_commit(s_nvs);
}
