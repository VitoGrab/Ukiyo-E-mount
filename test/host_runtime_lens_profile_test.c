#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lens_profile.h"
#include "emount_packets.h"
#include "ukiyo-e-mount/version.h"

static unsigned u16(const uint8_t *p) {
    return p[0] | ((unsigned)p[1] << 8);
}

static void read_image(const char *path, uint8_t *image) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fread(image, 1, UKIYO_PROFILE_SIZE, f) == UKIYO_PROFILE_SIZE);
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void check(unsigned focal, unsigned id, unsigned maximum,
                  unsigned minimum, unsigned now, const char *model) {
    assert(u16(em_msg_05 + 30) == focal && u16(em_msg_05 + 32) == focal);
    assert(u16(em_msg_28 + 10) == focal && u16(em_msg_28 + 12) == focal);
    assert(u16(em_msg_35 + 10) == focal && u16(em_msg_35 + 12) == focal);
    assert(u16(em_msg_05 + 6) == now && u16(em_msg_1B + 6) == now);
    assert(u16(em_msg_08 + 6) == maximum && u16(em_msg_08 + 8) == minimum);
    assert(em_msg_08[24] == 0 && em_msg_08[25] == 0 && em_msg_08[27] == 0);
    assert(u16(em_msg_28 + 15) == 0 && u16(em_msg_35 + 15) == 0);
    assert(u16(em_msg_07 + 15) == id);
    assert(u16(em_msg_07 + 8) == UKIYO_PROTOCOL_U16);
    assert(em_msg_07[11] == UKIYO_FIRMWARE_MINOR);
    assert(em_msg_07[12] == UKIYO_FIRMWARE_MAJOR);
    assert(strcmp((const char *)em_msg_3F + 7, model) == 0);
    unsigned sum = 0;
    for (unsigned i = 1; i < EM_LEN_07 - 3; ++i) sum += em_msg_07[i];
    assert(u16(em_msg_07 + EM_LEN_07 - 3) == (sum & 65535u));
}

int main(int argc, char **argv) {
    uint8_t image[UKIYO_PROFILE_SIZE], snapshot[EM_LEN_01];
    memcpy(snapshot, em_msg_01, sizeof(snapshot));
    ukiyo_profile_apply();
    check(580, 0, 0x1200, 0x1800, 0x1200, "HELIOS 44M-4 58mm f2");
    read_image("profiles/helios-44m-4.bin", image);
    assert(ukiyo_profile_validate(image, sizeof(image)));
    assert(ukiyo_profile_stage(image, sizeof(image)));
    ukiyo_profile_apply();
    check(580, 0, 0x1200, 0x1800, 0x1200, "HELIOS 44M-4 58mm f2");

    assert(!ukiyo_profile_stage(image, sizeof(image) - 1));
    assert(!ukiyo_profile_stage(NULL, sizeof(image)));
    image[40] ^= 1;
    assert(!ukiyo_profile_stage(image, sizeof(image)));
    ukiyo_profile_apply();
    check(580, 0, 0x1200, 0x1800, 0x1200, "HELIOS 44M-4 58mm f2");

    if (argc >= 3) {
        read_image(argv[1], image);
        assert(ukiyo_profile_stage(image, sizeof(image)));
        assert(u16(em_msg_05 + 30) == 580); /* staging is non-mutating */
        ukiyo_profile_apply();
        check(1350, 42, 0x1300, 0x1800, 0x1400, "Test 135mm");
        assert(em_msg_08[22] == 0x01 && em_msg_08[23] == 0x35);
        read_image(argv[2], image);
        assert(ukiyo_profile_stage(image, sizeof(image)));
        ukiyo_profile_apply();
        check(350, 0, 0x1200, 0x1800, 0x1200, "35mm");
        assert(em_msg_3F[12] == 0);
    }
    for (int i = 3; i < argc; ++i) {
        read_image(argv[i], image);
        assert(!ukiyo_profile_stage(image, sizeof(image)));
        ukiyo_profile_apply();
        check(350, 0, 0x1200, 0x1800, 0x1200, "35mm");
    }
    assert(memcmp(snapshot, em_msg_01, sizeof(snapshot)) == 0);
    puts("RUNTIME_PROFILE=PASS");
    return 0;
}
