/* A game's view of an image: build/hero.h from gesso2c, compiled as strict
   C. The values are worked out by hand from hero.filo. */
#include "hero.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void) {
    CHECK(HERO_WIDTH == 4 && HERO_HEIGHT == 3);
    CHECK(HERO_FRAMES == 2 && HERO_TAGS == 1 && HERO_COLORS == 17);
    CHECK(hero_palette[0] == 0x00000000u);
    CHECK(hero_palette[8] == 0xfffff1e8u);
    CHECK(hero_duration[0] == 120 && hero_duration[1] == 80);
    CHECK(hero_pixels[0][0] == 8);
    CHECK(hero_pixels[0][2 * HERO_WIDTH + 1] == 12 && hero_pixels[0][2 * HERO_WIDTH + 3] == 12);
    CHECK(hero_pixels[0][2 * HERO_WIDTH] == 0);
    CHECK(hero_pixels[1][3] == 9 && hero_pixels[1][0] == 0);
    CHECK(strcmp(hero_tags[0].name, "walk") == 0);
    CHECK(hero_tags[0].first == 0 && hero_tags[0].last == 1 && hero_tags[0].direction == 2);
    if (failures > 0) {
        return 1;
    }
    printf("formats: ok\n");
    return 0;
}
