#include <assert.h>
#include <string.h>
#include "lens_profile.h"
#include "emount_packets.h"

static unsigned u16(const uint8_t *p) {
    return p[0] | ((unsigned)p[1] << 8);
}

static void checksum(const uint8_t *p, unsigned n) {
    unsigned sum = 0;
    for (unsigned i = 1; i < n - 3; ++i) sum += p[i];
    assert(u16(p + n - 3) == (sum & 65535u));
}

int main(void) {
    ukiyo_lens_profile p = {135, 0, 0x1300, 0x1800, 0x1400, "Test 135mm"};
    ukiyo_profile_apply();
    assert(u16(em_msg_05 + 30) == u16(em_msg_28 + 10));
    assert(u16(em_msg_05 + 30) == u16(em_msg_35 + 10));
    unsigned original = u16(em_msg_05 + 30);
    assert(ukiyo_profile_stage(&p));
    assert(u16(em_msg_05 + 30) == original);
    p.focal_mm = 0;
    assert(!ukiyo_profile_stage(&p));
    ukiyo_profile_apply();
    assert(u16(em_msg_05 + 30) == 1350 && u16(em_msg_05 + 32) == 1350);
    assert(u16(em_msg_28 + 10) == 1350 && u16(em_msg_28 + 12) == 1350);
    assert(u16(em_msg_35 + 10) == 1350 && u16(em_msg_35 + 12) == 1350);
    assert(em_msg_08[22] == 0x01 && em_msg_08[23] == 0x35);
    assert(em_msg_08[24] == 0 && em_msg_08[25] == 0 && em_msg_08[27] == 0);
    assert(em_msg_08[26] == 0x28);
    assert(u16(em_msg_05 + 6) == 0x1400 && u16(em_msg_1B + 6) == 0x1400);
    assert(u16(em_msg_08 + 6) == 0x1300 && u16(em_msg_08 + 8) == 0x1800);
    assert(em_msg_05[50] == 32 && em_msg_05[52] == 32 && em_msg_05[57] == 32);
    assert(em_msg_05[58] == 72);
    assert(u16(em_msg_28 + 15) == 0 && u16(em_msg_35 + 15) == 0);
    assert(strcmp((const char *)em_msg_3F + 7, "Test 135mm") == 0);
    checksum(em_msg_07, EM_LEN_07);
    checksum(em_msg_3F, EM_LEN_3F);
    p.focal_mm = 35;
    p.lens_id = 42;
    strcpy(p.model, "35mm");
    assert(ukiyo_profile_stage(&p));
    ukiyo_profile_apply();
    assert(u16(em_msg_07 + 15) == 42);
    assert(u16(em_msg_05 + 30) == 350);
    assert(strcmp((const char *)em_msg_3F + 7, "35mm") == 0);
    assert(em_msg_3F[12] == 0);
    checksum(em_msg_07, EM_LEN_07);
    checksum(em_msg_3F, EM_LEN_3F);
    return 0;
}
