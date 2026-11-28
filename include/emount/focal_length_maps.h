#pragma once

#include <stdint.h>
/* Generated from profiles/focal_length_maps.json. */
#define UKIYO_FOCAL_MAP_SIZE 40u
static const uint16_t ukiyo_focal_map_standard[UKIYO_FOCAL_MAP_SIZE] = {
    20, 21, 24, 25, 28, 30, 35, 40, 45, 48,
    50, 60, 70, 75, 80, 85, 90, 95, 100, 105,
    110, 115, 120, 125, 130, 135, 140, 145, 150, 155,
    160, 165, 170, 175, 180, 185, 190, 195, 200, 210,
};
static const uint16_t ukiyo_focal_map_telephoto[UKIYO_FOCAL_MAP_SIZE] = {
    35, 40, 50, 60, 70, 75, 80, 85, 90, 100,
    105, 110, 120, 125, 135, 150, 160, 170, 180, 190,
    200, 210, 230, 240, 250, 275, 280, 300, 320, 350,
    375, 400, 425, 450, 475, 500, 525, 550, 575, 600,
};
