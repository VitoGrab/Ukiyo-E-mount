#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include "ukiyo-e-mount/profile_usb.h"
#include "ukiyo-e-mount/emulator.h"
#include "ukiyo-e-mount/camera_link.h"
#include "emount/lens_profile.h"

namespace ukiyoemount { namespace profile_usb {
#if defined(UKIYO_PROFILE_FLASH) && UKIYO_PROFILE_FLASH && defined(__SAMD21G18A__)
namespace {
extern "C" {
extern const uint8_t __ukiyo_profile_start__[];
extern const uint8_t __ukiyo_profile_end__[];
}
constexpr uintptr_t kStart = 0x3e000u;
constexpr uintptr_t kEnd = 0x40000u;
constexpr unsigned kPage = 64;
constexpr unsigned kRow = 256;
constexpr unsigned kPages = (UKIYO_PROFILE_SIZE + kPage - 1) / kPage;
constexpr unsigned kRows = (UKIYO_PROFILE_SIZE + kRow - 1) / kRow;
constexpr unsigned kChunk = 24;
constexpr uint32_t kTimeoutMs = 10000;
constexpr uint16_t kErrors = NVMCTRL_STATUS_PROGE | NVMCTRL_STATUS_LOCKE |
                             NVMCTRL_STATUS_NVME;
static_assert(kRows * kRow <= UKIYO_PROFILE_FLASH_SIZE, "profile region overflow");

uint8_t incoming[UKIYO_PROFILE_SIZE];
unsigned received = 0;
bool receiving = false;
uint32_t last_activity = 0;

// The existing LOGIC_VCC divider pulls A0 low when disconnected. The
// physical camera must also be detached; voltage alone cannot prove that.
bool idle_and_unpowered() {
    return Serial && !emulator::active() && !camera_link::active() &&
           analogRead(A0) < 50;
}

bool stable_idle() {
    for (unsigned i = 0; i < 20; ++i) {
        if (!idle_and_unpowered()) return false;
        delay(1);
    }
    return true;
}

uint32_t user_row() {
    return *reinterpret_cast<const volatile uint32_t*>(0x00804000u);
}

const char* layout_error() {
    if (reinterpret_cast<uintptr_t>(__ukiyo_profile_start__) != kStart ||
        reinterpret_cast<uintptr_t>(__ukiyo_profile_end__) != kEnd)
        return "LINKER_LAYOUT";
    const uint32_t param = NVMCTRL->PARAM.reg;
    if ((param & NVMCTRL_PARAM_NVMP_Msk) != NVMCTRL_PARAM_NVMP(4096u) ||
        (param & NVMCTRL_PARAM_PSZ_Msk) != NVMCTRL_PARAM_PSZ_64)
        return "NVM_GEOMETRY";
    // SAM D21 EEPROM fuse [6:4] = 7 means no reserved EEPROM.
    // Conservatively refuse any EEPROM reservation rather than move a slot.
    if (((user_row() >> 4) & 7u) != 7u) return "EEPROM_RESERVED";
    if (!(NVMCTRL->LOCK.reg & (1u << 15))) return "REGION_LOCKED";
    return nullptr;
}

bool wait_ready() {
    // No timing dependency on SysTick while the flash array is busy.
    for (uint32_t n = 0; n < 4800000u; ++n)
        if (NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) return true;
    return false;
}

bool nvm_command(uint16_t command) {
    if (!wait_ready()) return false;
    NVMCTRL->STATUS.reg = kErrors;
    NVMCTRL->INTFLAG.reg = NVMCTRL_INTFLAG_ERROR;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | command;
    return wait_ready() && !(NVMCTRL->STATUS.reg & kErrors) &&
           !(NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_ERROR);
}

bool erase_row(unsigned row) {
    if (row >= kRows) return false;
    NVMCTRL->ADDR.reg = (kStart + row * kRow) / 2u;
    return nvm_command(NVMCTRL_CTRLA_CMD_ER);
}

bool write_page(unsigned page) {
    if (page >= kPages || !nvm_command(NVMCTRL_CTRLA_CMD_PBC)) return false;
    const uintptr_t address = kStart + page * kPage;
    auto* words = reinterpret_cast<volatile uint32_t*>(address);
    for (unsigned i = 0; i < kPage / 4; ++i) {
        uint32_t word = 0;
        for (unsigned b = 0; b < 4; ++b) {
            const unsigned offset = page * kPage + i * 4 + b;
            word |= uint32_t(offset < UKIYO_PROFILE_SIZE ? incoming[offset] : 0xffu)
                    << (8u * b);
        }
        words[i] = word;
    }
    if ((NVMCTRL->STATUS.reg & kErrors) ||
        (NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_ERROR)) return false;
    NVMCTRL->ADDR.reg = address / 2u;
    return nvm_command(NVMCTRL_CTRLA_CMD_WP);
}

bool write_image() {
    if (!stable_idle() || layout_error() ||
        !ukiyo_profile_validate(incoming, sizeof(incoming)) || !wait_ready())
        return false;
    if (!memcmp(__ukiyo_profile_start__, incoming, sizeof(incoming))) return true;

    const uint32_t old = NVMCTRL->CTRLB.reg;
    NVMCTRL->CTRLB.reg = old | NVMCTRL_CTRLB_MANW | NVMCTRL_CTRLB_CACHEDIS;
    bool ok = true;
    // Erase the old header first. Write page zero last as the commit marker.
    // Only the first three rows of the reserved region are touched.
    for (unsigned row = 0; ok && row < kRows; ++row) {
        if (!idle_and_unpowered()) { ok = false; break; }
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        ok = erase_row(row);
        __set_PRIMASK(primask);
    }
    for (unsigned page = 1; ok && page < kPages; ++page) {
        if (!idle_and_unpowered()) { ok = false; break; }
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        ok = write_page(page);
        __set_PRIMASK(primask);
    }
    if (ok && idle_and_unpowered()) {
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        ok = write_page(0);
        __set_PRIMASK(primask);
    } else ok = false;
    if (!wait_ready()) return false;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const bool cache_ok = nvm_command(NVMCTRL_CTRLA_CMD_INVALL);
    if (wait_ready()) NVMCTRL->CTRLB.reg = old;
    __set_PRIMASK(primask);
    return ok && cache_ok &&
           !memcmp(__ukiyo_profile_start__, incoming, sizeof(incoming)) &&
           ukiyo_profile_validate(__ukiyo_profile_start__, UKIYO_PROFILE_SIZE);
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool offset4(const char* p, unsigned& result) {
    if (strlen(p) < 4) return false;
    result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const int v = nibble(p[i]);
        if (v < 0) return false;
        result = (result << 4) | unsigned(v);
    }
    return true;
}
void error(const char* reason) {
    cancel();
    Serial.print("PROFILE_ERROR=");
    Serial.println(reason);
}
void print_hex(uint8_t b) {
    const char hex[] = "0123456789abcdef";
    Serial.print(hex[b >> 4]);
    Serial.print(hex[b & 15]);
}
} // namespace

void cancel() {
    receiving = false;
    received = 0;
}
void poll() {
    if (receiving && (!Serial || millis() - last_activity >= kTimeoutMs))
        cancel();
}
bool handle(const char* cmd) {
    if (strncmp(cmd, "profile", 7) != 0 || (cmd[7] && cmd[7] != ' ')) return false;
    if (!strcmp(cmd, "profile abort")) {
        cancel();
        Serial.println("PROFILE_ABORTED");
        return true;
    }
    if (!idle_and_unpowered()) {
        error("STOP_AND_DISCONNECT_CAMERA");
        return true;
    }
    if (!strcmp(cmd, "profile info")) {
        Serial.print("PROFILE_BOOTPROT="); Serial.println(user_row() & 7u);
        Serial.print("PROFILE_EEPROM="); Serial.println((user_row() >> 4) & 7u);
        Serial.print("PROFILE_LOCK=0x"); Serial.println(NVMCTRL->LOCK.reg, HEX);
        const char* reason = layout_error();
        if (reason) { error(reason); return true; }
        Serial.print("PROFILE_VALID=");
        Serial.println(ukiyo_profile_validate(__ukiyo_profile_start__, UKIYO_PROFILE_SIZE));
        Serial.println("PROFILE_INFO_END");
        return true;
    }
    const char* reason = layout_error();
    if (reason) { error(reason); return true; }
    if (!strcmp(cmd, "profile begin")) {
        cancel();
        if (!stable_idle()) { error("STOP_AND_DISCONNECT_CAMERA"); return true; }
        receiving = true;
        last_activity = millis();
        Serial.println("PROFILE_READY");
    } else if (!strncmp(cmd, "profile data ", 13)) {
        unsigned offset;
        const char* p = cmd + 13;
        if (!receiving || !offset4(p, offset) || p[4] != ' ' ||
            offset != received) { error("OFFSET_OR_STATE"); return true; }
        p += 5;
        const unsigned length = strlen(p);
        if (!length || length % 2 || length > 2 * kChunk ||
            received + length / 2 > sizeof(incoming)) {
            error("CHUNK_LENGTH"); return true;
        }
        for (unsigned i = 0; i < length; i += 2) {
            const int hi = nibble(p[i]), lo = nibble(p[i + 1]);
            if (hi < 0 || lo < 0) { error("HEX"); return true; }
            incoming[received++] = uint8_t(hi * 16 + lo);
        }
        last_activity = millis();
        Serial.print("PROFILE_NEXT="); Serial.println(received);
    } else if (!strcmp(cmd, "profile commit")) {
        if (!receiving || received != sizeof(incoming) ||
            !ukiyo_profile_validate(incoming, sizeof(incoming))) {
            error("INVALID_IMAGE"); return true;
        }
        cancel();
        if (!write_image()) { error("WRITE_OR_VERIFY_FAILED"); return true; }
        // Do not select new metadata mid-session. Bus stays OFF; the next
        // full handshake (or reset) can use the verified profile.
        if (!ukiyo_profile_stage(incoming, sizeof(incoming))) {
            error("STAGING_FAILED"); return true;
        }
        Serial.println("PROFILE_WRITTEN");
    } else if (!strncmp(cmd, "profile read ", 13)) {
        unsigned offset;
        const char* p = cmd + 13;
        if (receiving || !offset4(p, offset) || p[4] || offset >= UKIYO_PROFILE_SIZE) {
            error("READ_OFFSET_OR_STATE"); return true;
        }
        Serial.print("PROFILE_DATA=");
        for (unsigned i = offset; i < UKIYO_PROFILE_SIZE && i < offset + kChunk; ++i)
            print_hex(__ukiyo_profile_start__[i]);
        Serial.println();
    } else {
        error("UNKNOWN_COMMAND");
    }
    return true;
}
#else
void cancel() {}
void poll() {}
bool handle(const char* cmd) {
    if (strncmp(cmd, "profile", 7) != 0 || (cmd[7] && cmd[7] != ' ')) return false;
    Serial.println("PROFILE_ERROR=UNSUPPORTED_TARGET");
    return true;
}
#endif
} }
