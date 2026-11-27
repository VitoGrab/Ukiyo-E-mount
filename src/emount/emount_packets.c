/* Engine-owned frames and live packet buffers. Lens-specific templates are
 * loaded from a versioned binary profile before full initialization.
 * Original templates derived from the Techart implementation; see third_party. */
#include "emount_packets.h"
#include "emount.h"
#include "lens_profile.h"
#include <string.h>

/* A u16 as the two little-endian bytes of a template. */
#define EM_LO(v) ((uint8_t)((v) & 0xFFu))
#define EM_HI(v) ((uint8_t)(((v) >> 8) & 0xFFu))

/* An aperture value (256 x AV + 4096) as message 0x05's descriptor byte: 1/8
 * stop, 0x08 = f/1.0, rounded to nearest. */
#define EM_AV_DESC(av) ((uint8_t)(((av) - 4096u + 16u) / 32u + 8u))

/* An aperture value as message 0x08's nameplate f-number: F x 10 in BCD,
 * rounded to the nearest third of a stop on the marked series f/1.0 .. f/9
 * (0x11C0 -> 0x18, f/1.8). */
#define EM_AV_THIRDS(av) ((((av) - 4096u) * 3u + 128u) / 256u)
#define EM_F_BCD(av)     EM_F_BCD_T(EM_AV_THIRDS(av))
#define EM_F_BCD_T(t) ((uint8_t)(                                           \
	(t) ==  0 ? 0x10 : (t) ==  1 ? 0x11 : (t) ==  2 ? 0x12 : (t) ==  3 ? 0x14 : \
	(t) ==  4 ? 0x16 : (t) ==  5 ? 0x18 : (t) ==  6 ? 0x20 : (t) ==  7 ? 0x22 : \
	(t) ==  8 ? 0x25 : (t) ==  9 ? 0x28 : (t) == 10 ? 0x32 : (t) == 11 ? 0x35 : \
	(t) == 12 ? 0x40 : (t) == 13 ? 0x45 : (t) == 14 ? 0x50 : (t) == 15 ? 0x56 : \
	(t) == 16 ? 0x63 : (t) == 17 ? 0x71 : (t) == 18 ? 0x80 : 0x90))

/* Baseline packet values; native MF overrides are applied by emount.c. */
#define UKIYO_MF_08_PL7  0x80
#define UKIYO_MF_08_PL9  0x14
#define UKIYO_MF_08_PL85 0x80
#define UKIYO_MF_01_PL6 0x18
#define UKIYO_MF_01_PL7 0x5e

/* The stock mask plus bits 33 and 45, the 0x04 query records 0x22 and 0x2E
 * (pl[4] 80 -> 82, pl[5] 07 -> 27), as Sony natives and Yongnuo assert; and
 * bit 59, the Drive record 0x3C (pl[7] 50 -> 58), as Yongnuo asserts; and
 * bits 57 and 58, the paged optical file 0x3A/0x3B (pl[7] 58 -> 5e), as
 * Yongnuo asserts -- its last byte is now the DF's own; and bit 52, message
 * 0x35 (pl[6] 08 -> 18), as Yongnuo and TTArtisan assert.  Then minus the
 * ids this firmware has no handler for -- 0x17, 0x1E, 0x29, 0x2A and 0x2B
 * (pl[2] 60 -> 20, pl[3] 7d -> 5d, pl[5] 27 -> 20): an A7 IV refuses to
 * request an undeclared id, but asks for a declared one and waits for the
 * answer.  Then bit 46, the 0x2F row-index record (pl[5] 20 -> 60), as Sony
 * natives assert: without it the body sends the 20-byte 0x03 with no 0x2F
 * record and never asks for a row (emount.c, EM05_ROW_INDEX).  0x2B is the adapter re-identification message; Sony natives and
 * Yongnuo do not declare it, and a native is never asked for it.  0x02 stays
 * with the rest of pl[0]: every device measured, Sony's own included,
 * declares 0x01..0x08.
 *
 * 0x34 is declared (pl[6] 18, with 0x35).  An A7 IV sends it from its
 * release sequence as "move here within n ms" (msg_0x34.md); it is answered
 * with a move cut to what finishes in the time (emount.c, timed_move34). */
uint8_t em_msg_01[EM_LEN_01] = {
	/* +0x00 */ 0xf0, 0x29, 0x00, 0x02, 0x00, 0x01, 0xff, 0x9f,
	/* +0x08 */ 0x20, 0x5d, 0x82, 0x60, UKIYO_MF_01_PL6, UKIYO_MF_01_PL7, 0x00, 0x00,
	/* +0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9f, 0x03,
	/* +0x28 */ 0x55,
};

/* Live status buffer populated from the selected profile. */
uint8_t em_msg_05[EM_LEN_05];

/* Sized for the longest form: event and query-answer blocks extend the frame
 * past its 48-byte core (emount.c, em_pump_status). */
uint8_t em_msg_06[EM06_LEN_MAX] = {
	/* +0x00 */ 0xf0, 0x30, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
	/* +0x08 */ 0x00, 0x10, 0x00, 0x10, 0x10, 0x30, 0x10, 0x00,
	/* +0x10 */ 0x16, 0x00, 0x00, EM_LO(EM_DEFOCUS_SCALE), EM_HI(EM_DEFOCUS_SCALE), 0x1b, 0x06, 0x06,
	/* +0x18 */ 0xc6, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
	/* +0x20 */ 0x6c, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x02, 0x55,
};

uint8_t em_msg_07[EM_LEN_07];

/* Lens characteristics buffer. */
uint8_t em_msg_08[EM_LEN_08];

uint8_t em_msg_09[EM_LEN_09] = {
	/* +0x00 */ 0xf0, 0x14, 0x00, 0x02, 0x00, 0x09, 0x01, 0x01,
	/* +0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x10 */ 0x00, 0x21, 0x00, 0x55,
};

uint8_t em_msg_0B[EM_LEN_0B] = {
	/* +0x00 */ 0xf0, 0x0b, 0x00, 0x02, 0x00, 0x0b, 0x60, 0x20,
	/* +0x08 */ 0x78, 0x00, 0x55,
};

uint8_t em_msg_0C[EM_LEN_0C] = {
	/* +0x00 */ 0xf0, 0x00, 0x00, 0x02, 0x00, 0x0c, 0x01, 0x19,
	/* +0x08 */ 0x00, 0x55,
};

uint8_t em_msg_0D[EM_LEN_0D] = {
	/* +0x00 */ 0xf0, 0x0a, 0x00, 0x02, 0x00, 0x0d, 0x01, 0x1a,
	/* +0x08 */ 0x00, 0x55,
};

uint8_t em_msg_10[EM_LEN_10] = {
	/* +0x00 */ 0xf0, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00,
	/* +0x08 */ 0x00, 0x55,
};

uint8_t em_msg_19[EM_LEN_19] = {
	/* +0x00 */ 0xf0, 0x00, 0x00, 0x02, 0x00, 0x19, 0x00, 0x00,
	/* +0x08 */ 0x00, 0x55,
};

uint8_t em_msg_1B[EM_LEN_1B];

/* pl[9..10]: zero aperture offset, independent of 0x08 request. */
uint8_t em_msg_28[EM_LEN_28];

/* The stock's unadvertised 0x35 template, with this lens's focal length and
 * the same two rows as message 0x28 (msg_0x35.md: the same content, rows split
 * by three bytes).  pl[8] = 01 as Yongnuo and TTArtisan send it (stock ff);
 * the secondary bytes pl[17..19] / pl[26..28] zeroed -- the stock's went with
 * its own rows.  pl[9..10] remains zero, as in message 0x28.
 * pl[0..3] is the subject distance, as message 0x28's: infinity until
 * emount.c writes it (the stock's 42 01 00 7f was another lens's 0.5 m). */
uint8_t em_msg_35[EM_LEN_35];

uint8_t em_msg_34[EM_LEN_34] = {
	/* +0x00 */ 0xf0, 0x20, 0x00, 0x02, 0x00, 0x34, 0x00, 0x00,
	/* +0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x56, 0x00, 0x55,
};

uint8_t em_msg_3D[EM_LEN_3D] = {
	/* +0x00 */ 0xf0, 0x48, 0x00, 0x02, 0x00, 0x3d, 0x01, 0x00,
	/* +0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* +0x40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x55,
};

uint8_t em_msg_3F[EM_LEN_3F];

/* Binary profile v1: a fixed sequence of seven complete packet templates. */
#include "default_lens_profile.h"
#include "../ukiyo-e-mount/version.h"

static uint8_t staged_profile[UKIYO_PROFILE_SIZE];
static uint8_t profile_staged;
static const struct ea9_packet profile_frames[] = {
    {0x05, em_msg_05, EM_LEN_05}, {0x07, em_msg_07, EM_LEN_07},
    {0x08, em_msg_08, EM_LEN_08}, {0x1b, em_msg_1B, EM_LEN_1B},
    {0x28, em_msg_28, EM_LEN_28}, {0x35, em_msg_35, EM_LEN_35},
    {0x3f, em_msg_3F, EM_LEN_3F}
};

static uint16_t profile_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t profile_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t profile_crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc = 0xffffffffu;
    while (n--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static uint16_t profile_sum(const uint8_t *p, unsigned n)
{
    uint16_t sum = 0;
    for (unsigned i = 1; i < n - 3; ++i) sum += p[i];
    return sum;
}

static uint8_t profile_bcd2(unsigned n)
{
    return (uint8_t)(((n / 10u % 10u) << 4) | (n % 10u));
}

uint8_t ukiyo_profile_validate(const uint8_t *image, unsigned size)
{
    unsigned offset = UKIYO_PROFILE_HEADER_SIZE;
    const uint8_t *f[7];
    if (!image || size != UKIYO_PROFILE_SIZE ||
        memcmp(image, "UKLP", 4) || profile_u16(image + 4) != 1u ||
        profile_u16(image + 6) != size || profile_u32(image + 12) != 0u ||
        profile_crc32(image + offset, size - offset) != profile_u32(image + 8))
        return 0;
    for (unsigned i = 0; i < 7; ++i) {
        const struct ea9_packet *entry = &profile_frames[i];
        unsigned n = entry->len;
        const uint8_t *p = image + offset;
        if (offset + n > size || p[0] != 0xf0 || p[5] != entry->id ||
            profile_u16(p + 1) != n || p[3] != (i == 0 ? 1u : 2u) ||
            p[n - 1] != 0x55 || profile_u16(p + n - 3) != profile_sum(p, n))
            return 0;
        f[i] = p;
        offset += n;
    }
    if (offset != size) return 0;
    const uint8_t *status = f[0], *identity = f[1], *spec = f[2],
                  *iris = f[3], *capture = f[4], *capture35 = f[5], *name = f[6];
    unsigned focal = profile_u16(status + 30);
    unsigned max = profile_u16(spec + 6), min = profile_u16(spec + 8);
    unsigned now = profile_u16(status + 6);
    if (focal < 10u || focal > 5000u || focal % 10u ||
        profile_u16(status + 32) != focal ||
        profile_u16(capture + 10) != focal || profile_u16(capture + 12) != focal ||
        profile_u16(capture35 + 10) != focal || profile_u16(capture35 + 12) != focal ||
        spec[22] != profile_bcd2(focal / 1000u) || spec[23] != profile_bcd2(focal / 10u) ||
        spec[24] || spec[25] || spec[27] || capture[15] || capture[16] ||
        capture35[15] || capture35[16] ||
        max < 4096u || max > now || now > 5719u || now > min || min > 7168u ||
        profile_u16(status + 8) != now || profile_u16(iris + 6) != now ||
        profile_u16(iris + 8) != now ||
        status[50] != EM_AV_DESC(max) || status[52] != EM_AV_DESC(max) ||
        status[57] != EM_AV_DESC(max) || status[58] != EM_AV_DESC(min) ||
        spec[26] != EM_F_BCD(max) ||
        identity[6] != 1u || identity[8] || identity[9] || identity[11] || identity[12] ||
        spec[13] != UKIYO_MF_08_PL7 || spec[15] != UKIYO_MF_08_PL9 ||
        spec[19] != 0xe1 || spec[91] != UKIYO_MF_08_PL85 ||
        name[6] || !name[7] || name[70]) return 0;
    uint8_t ended = 0;
    for (unsigned i = 7; i < 71; ++i) {
        if (!name[i]) ended = 1;
        else if (ended || name[i] > 127u) return 0;
    }
    return 1;
}

uint8_t ukiyo_profile_stage(const uint8_t *image, unsigned size)
{
    if (!ukiyo_profile_validate(image, size)) return 0;
    memcpy(staged_profile, image, UKIYO_PROFILE_SIZE);
    profile_staged = 1;
    return 1;
}

void ukiyo_profile_apply(void)
{
    const uint8_t *p = (profile_staged ? staged_profile : ukiyo_default_profile)
                      + UKIYO_PROFILE_HEADER_SIZE;
    for (unsigned i = 0; i < 7; ++i) {
        memcpy(profile_frames[i].frame, p, profile_frames[i].len);
        p += profile_frames[i].len;
    }
    /* These fields belong to the executing firmware, never the profile. */
    em_msg_07[8] = EM_LO(UKIYO_PROTOCOL_U16);
    em_msg_07[9] = EM_HI(UKIYO_PROTOCOL_U16);
    em_msg_07[11] = UKIYO_FIRMWARE_MINOR;
    em_msg_07[12] = UKIYO_FIRMWARE_MAJOR;
    uint16_t sum = profile_sum(em_msg_07, EM_LEN_07);
    em_msg_07[EM_LEN_07 - 3] = EM_LO(sum);
    em_msg_07[EM_LEN_07 - 2] = EM_HI(sum);
}

const struct ea9_packet ea9_packets[EA9_PACKET_COUNT] = {
	{ 0x01, em_msg_01, EM_LEN_01 },
	{ 0x05, em_msg_05, EM_LEN_05 },
	{ 0x06, em_msg_06, EM_LEN_06 },
	{ 0x07, em_msg_07, EM_LEN_07 },
	{ 0x08, em_msg_08, EM_LEN_08 },
	{ 0x09, em_msg_09, EM_LEN_09 },
	{ 0x0b, em_msg_0B, EM_LEN_0B },
	{ 0x0c, em_msg_0C, EM_LEN_0C },
	{ 0x0d, em_msg_0D, EM_LEN_0D },
	{ 0x10, em_msg_10, EM_LEN_10 },
	{ 0x19, em_msg_19, EM_LEN_19 },
	{ 0x1b, em_msg_1B, EM_LEN_1B },
	{ 0x28, em_msg_28, EM_LEN_28 },
	{ 0x34, em_msg_34, EM_LEN_34 },
	{ 0x35, em_msg_35, EM_LEN_35 },
	{ 0x3d, em_msg_3D, EM_LEN_3D },
	{ 0x3f, em_msg_3F, EM_LEN_3F },
};
