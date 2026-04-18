#pragma once
#include "effect_params.h"
#include <stdint.h>
#include <stdbool.h>

void    scene_store_init();
uint8_t scene_store_get_active();
void    scene_store_set_active(uint8_t index);
bool    scene_store_load(uint8_t index, EffectParams* out);
void    scene_store_save(uint8_t index, const EffectParams* params);
