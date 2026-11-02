#include "asset_upload.h"
#include "bsp.h"
#include "soc.h"
#include <stddef.h>
#include <string.h>
#ifdef ASSET_UPLOAD_SERIAL
#include "ddr_crc.h"
#include "build_id.h"
#endif

#define ASSET_SOH 0x01u
#define ASSET_EOT 0x04u
#define ASSET_ACK 0x06u
#define ASSET_NAK 0x15u
#define ASSET_CAN 0x18u
#define ASSET_BLOCK 128u
#define ASSET_TIMEOUT_SEC 15u

static uint8_t asset_packet[ASSET_BLOCK];
static uint8_t asset_previous[ASSET_BLOCK];
static uint8_t asset_have_previous;
static uint8_t asset_bullet_masks[2][64],asset_bullet_mask_valid;
static uint8_t asset_missile_masks[2][64],asset_missile_mask_valid;
const uint8_t *asset_upload_missile_mask(uint32_t type){
 return type<2u && (asset_missile_mask_valid&(1u<<type))?asset_missile_masks[type]:0;
}
const uint8_t *asset_upload_enemy_bullet_mask(uint32_t type){
 return type<4u && (asset_bullet_mask_valid&1u)?asset_bullet_masks[0]+(type>>1)*32u+(type&1u):0;
}
static uint8_t asset_player_mask_valid,asset_player_mask_opaque,asset_player_mask_pending;
static void asset_mask_invalidate(uint32_t id){
 if(id==25u || id==27u)asset_missile_mask_valid&=(uint8_t)~(1u<<((id-25u)/2u));
 if(id==11u)asset_bullet_mask_valid&=(uint8_t)~1u;
 if(id==13u)asset_bullet_mask_valid&=(uint8_t)~2u;
 if(id==PLAYER_MASK_ID){asset_player_mask_valid=0;asset_player_mask_pending=0;}
}
int asset_upload_player_mask(void){return asset_player_mask_valid?(int)asset_player_mask_opaque:-1;}
const uint8_t *asset_upload_bullet_mask(uint32_t type){
 return type<2u && (asset_bullet_mask_valid&(1u<<type))?asset_bullet_masks[type]:0;
}
static uint8_t asset_expected_seq;
static uint8_t asset_packet_seq;
static uint8_t asset_packet_inverse;
static uint8_t asset_packet_crc_hi;
static uint16_t asset_packet_pos;
enum { AS_IDLE, AS_HEADER, AS_PACKET, AS_DRAIN, AS_PREPARE,
       AS_OPEN, AS_FEED, AS_COMMIT, AS_RESPONSE, AS_CLAIM, AS_VERIFY };
static uint8_t asset_verify_running; /* GHD3S: DDR read-back CRC in flight (AS_VERIFY) */
static uint8_t asset_state;
static uint8_t asset_id;
static uint32_t asset_length;
static uint32_t asset_offset;
static uint32_t asset_received;
static uint32_t asset_crc;
static uint32_t asset_expected_crc;
static uint32_t asset_valid;
static uint32_t asset_valid_hi; /* CMD: IDs 32..63 */
static void asset_valid_set(uint32_t id,int on){uint32_t *w=id<32u?&asset_valid:&asset_valid_hi,b=1u<<(id&31u);if(on)*w|=b;else *w&=~b;}
static uint32_t asset_error;
static u64 asset_last_activity;
static uint8_t asset_cancel_pending;
static uint8_t asset_resource_guard;
static uint8_t asset_word, asset_finish, asset_eot_replay, asset_held_reported;
static uint16_t asset_discard_remaining;
static u64 asset_stage_started;
static int (*asset_prepare_hook)(void);
static void (*asset_resume_hook)(void);

static const uint32_t asset_bases[ASSET_ID_MAX+1u] = ASSET_BASE_VALUES;
static const uint32_t asset_lengths[ASSET_ID_MAX+1u] = ASSET_LENGTH_VALUES;

static uint32_t asset_addr(uint32_t offset)
{
    return IO_APB_SLAVE_0_INPUT + (offset & 0xFFFu);
}

static void asset_tx(uint8_t byte) { uart_write(BSP_UART_TERMINAL, (char)byte); }

static void asset_text(const char *s) { uart_writeStr(BSP_UART_TERMINAL, s); }
static void asset_decimal(uint32_t value){char digits[10];unsigned n=0;
 do{digits[n++]=(char)('0'+value%10u);value/=10u;}while(value);
 while(n)asset_tx((uint8_t)digits[--n]);
}

static u64 asset_now(void) { return clint_getTime(BSP_CLINT); }

static uint16_t asset_crc16(const uint8_t *data)
{
    uint16_t crc = 0u;
    uint32_t i;
    for (i = 0u; i < ASSET_BLOCK; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc & 0x8000u) ?
                             (uint16_t)((crc << 1) ^ 0x1021u) :
                             (uint16_t)(crc << 1));
    }
    return crc;
}

static uint32_t asset_crc32_update(uint32_t crc, uint8_t byte)
{
    uint8_t bit;
    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit)
        crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    return crc;
}

static int asset_hw_ready(void)
{
    /* Check the complete magic/version before any claim or data write. */
    return read_u32(asset_addr(ASSET_REG_ABI)) == ASSET_HW_ABI;
}

static void asset_release(void)
{
    if (asset_state == AS_IDLE || asset_state == AS_DRAIN) return;
    write_u32(ASSET_CTRL_ABORT, asset_addr(ASSET_REG_CONTROL));
    /* The controller drains delayed START/AW/W/B and resets only its write
     * FIFO. Keep the software lease until both OWNER=0 and IDLE are true. */
    asset_state = AS_DRAIN;
    asset_stage_started = asset_now();
    asset_held_reported = 0u;
    asset_have_previous = 0u;
}

void asset_upload_init(void)
{
    asset_bullet_mask_valid=asset_missile_mask_valid=0u;
    asset_state = 0u; asset_id = 0u; asset_length = 0u; asset_offset = 0u;
    asset_received = 0u; asset_crc = 0xFFFFFFFFu; asset_expected_crc = 0u;
    asset_valid = 0u; asset_valid_hi = 0u; asset_error = 0u; asset_have_previous = 0u;
    asset_player_mask_valid=asset_player_mask_opaque=asset_player_mask_pending=0;
    asset_packet_pos = 0u; asset_cancel_pending = 0u; asset_resource_guard = 0u;
    asset_word = asset_finish = asset_eot_replay = asset_held_reported = 0u;
    asset_verify_running = 0u;
    asset_discard_remaining = 0u;
    asset_prepare_hook = 0; asset_resume_hook = 0;
}

void asset_upload_set_hooks(int (*prepare)(void), void (*resume)(void))
{ asset_prepare_hook = prepare; asset_resume_hook = resume; }

void asset_upload_set_resource_guard(int blocked)
{
    asset_resource_guard = blocked ? 1u : 0u;
}

int asset_upload_active(void) { return asset_state != 0u; }
int asset_upload_binary_active(void) { return asset_state == 2u; }
int asset_upload_route_byte(uint8_t byte)
{
    return asset_discard_remaining || asset_upload_binary_active() ||
           (asset_upload_active() && (byte == ASSET_SOH || byte == ASSET_EOT || byte == ASSET_CAN)) ||
           (asset_eot_replay && byte == ASSET_EOT);
}
int asset_upload_resource_guard(void) { return asset_state != 0u; }
uint32_t asset_upload_valid_mask(void) { return asset_valid; }
uint32_t asset_upload_valid_mask_hi(void) { return asset_valid_hi; }

static int asset_parse_u32(const char *s, uint32_t *v)
{
    uint32_t x = 0u, i = 0u;
    if (!s || !*s) return 0;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return 0;
        if (x > (0xFFFFFFFFu - (uint32_t)(s[i] - '0')) / 10u) return 0;
        x = x * 10u + (uint32_t)(s[i] - '0'); ++i;
    }
    *v = x; return 1;
}

static int asset_parse_hex(const char *s, uint32_t *v)
{
    uint32_t x = 0u, i;
    if (!s || strlen(s) != 8u) return 0;
    for (i = 0u; i < 8u; ++i) {
        uint8_t c = (uint8_t)s[i]; uint32_t d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10u;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10u;
        else return 0;
        x = (x << 4) | d;
    }
    *v = x; return 1;
}

static int asset_token(const char **cursor, char *out, uint32_t out_size)
{
    const char *p = *cursor; uint32_t n = 0u;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') return 0;
    while (p[n] != '\0' && p[n] != ' ' && p[n] != '\t' &&
           p[n] != '\r' && p[n] != '\n') {
        if (n + 1u >= out_size) return -1;
        out[n] = p[n]; ++n;
    }
    out[n] = '\0'; *cursor = p + n; return 1;
}

static int asset_line_end(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return *p == '\0';
}

int asset_upload_handle_line(const char *line)
{
    const char *cursor;
    char command[12], sid[12], slen[16], scrc[16];
    uint32_t id, length, crc;
    if (!line || strncmp(line, "ASSET", 5u) != 0 ||
        (line[5] != '\0' && line[5] != ' ' && line[5] != '\t')) return 0;
    cursor = line + 5;
    if (asset_token(&cursor, command, sizeof(command)) != 1) {
        asset_text("ASSET ERROR SYNTAX\r\n"); return 1;
    }
    if (strcmp(command, "HELLO") == 0) {
        asset_text(asset_hw_ready() ? "ASSET HELLO 1 FIXED128 ABI=41530100\r\n" :
                                      "ASSET ERROR UNSUPPORTED\r\n");
        return 1;
    }
    if (strcmp(command, "CAPS") == 0 && asset_line_end(cursor)) {
        asset_text("ASSET CAPS ABI=");
        uart_writeHex(BSP_UART_TERMINAL, (int)read_u32(asset_addr(ASSET_REG_EXT_ABI)));
        asset_text(" MASK=");
        uart_writeHex(BSP_UART_TERMINAL, (int)read_u32(asset_addr(ASSET_REG_CAP_MASK)));
        asset_text(" MASK_HI=");
        uart_writeHex(BSP_UART_TERMINAL, (int)read_u32(asset_addr(ASSET_REG_CAP_MASK_HI)));
        asset_text("\r\n"); return 1;
    }
    if (strcmp(command, "STATUS") == 0) {
        asset_text("ASSET STATUS VALID_MASK=");
        uart_writeHex(BSP_UART_TERMINAL, (int)asset_valid);
        asset_text(" VALID_MASK_HI=");
        uart_writeHex(BSP_UART_TERMINAL, (int)asset_valid_hi);
        asset_text(" ACTIVE=");
        uart_write(BSP_UART_TERMINAL, (char)('0' + (char)(asset_state != 0u)));
        asset_text(" ERROR="); uart_writeHex(BSP_UART_TERMINAL, (int)asset_error);
        asset_text(" STAGE="); uart_writeHex(BSP_UART_TERMINAL, (int)asset_state);
        asset_text(" COMMITTED="); uart_writeHex(BSP_UART_TERMINAL, (int)asset_received);
        asset_text(asset_state == AS_DRAIN ? " OWNER_HELD\r\n" : "\r\n");
        return 1;
    }
#ifdef ASSET_UPLOAD_SERIAL
    if (strcmp(command, "BUILD") == 0 && asset_line_end(cursor)) { /* GHD3S: identity of the loaded ELF and bit */
        asset_text("ASSET BUILD ID=" BUILD_ID " HUD="); uart_writeHex(BSP_UART_TERMINAL, (int)read_u32(asset_addr(0x1ECu)));
        asset_text(" DDRCRC="); uart_writeHex(BSP_UART_TERMINAL, (int)read_u32(asset_addr(0x3E0u)));
        asset_text(" VERIFY="); asset_tx((uint8_t)(ddr_crc_present() ? '1' : '0')); asset_text("\r\n"); return 1;
    }
#endif
    if (strcmp(command, "CANCEL") == 0) {
        if (asset_state != 0u) {
            asset_cancel_pending = 1u;
            asset_text("ASSET CANCELLING\r\n");
        } else {
            asset_text("ASSET CANCELLED\r\n");
        }
        return 1;
    }
    if (strcmp(command, "BEGIN") != 0 ||
        asset_token(&cursor, sid, sizeof(sid)) != 1 ||
        asset_token(&cursor, slen, sizeof(slen)) != 1 ||
        asset_token(&cursor, scrc, sizeof(scrc)) != 1 ||
        !asset_line_end(cursor) ||
        !asset_parse_u32(sid, &id) || !asset_parse_u32(slen, &length) ||
        !asset_parse_hex(scrc, &crc) || id < 1u || id > ASSET_ID_MAX ||
        (id > 3u && (read_u32(asset_addr(ASSET_REG_EXT_ABI)) != ASSET_EXT_ABI ||
                      !((read_u32(asset_addr(id < 32u ? ASSET_REG_CAP_MASK : ASSET_REG_CAP_MASK_HI)) >> (id & 31u)) & 1u))) ||
        length != asset_lengths[id] || !asset_hw_ready() || asset_state != 0u) {
        asset_text("ASSET ERROR BEGIN\r\n"); return 1;
    }
    if(asset_resource_guard){asset_text("ASSET ERROR BUSY\r\n");return 1;}
    asset_mask_invalidate(id);
    asset_id = (uint8_t)id; asset_length = length; asset_expected_crc = crc;
    asset_offset = 0u; asset_received = 0u; asset_crc = 0xFFFFFFFFu;
    asset_error = 0u; asset_valid_set(id, 0); asset_have_previous = 0u;
    asset_expected_seq = 1u; asset_packet_pos = 0u;
    asset_cancel_pending = asset_finish = asset_eot_replay = 0u;
    /* Claim is deferred until the main-loop boundary.  A line can arrive in
     * the renderer's synchronous writer callback; that callback may parse but
     * must not acquire the shared DDR writer recursively. */
    asset_state = AS_PREPARE; asset_last_activity = asset_stage_started = asset_now();
    return 1;
}

int asset_upload_consume_byte(uint8_t byte)
{
    uint16_t crc;
    uint8_t expected_inverse;
    if (asset_discard_remaining) { --asset_discard_remaining; return 1; }
    if (asset_state == AS_IDLE) {
        if (asset_eot_replay && byte == ASSET_EOT) {
            asset_tx(ASSET_ACK); asset_text("ASSET STORED\r\n"); return 1;
        }
        return 0;
    }
    asset_last_activity = asset_now();
    if (byte == ASSET_SOH && asset_state != AS_HEADER && asset_state != AS_PACKET) {
        /* A premature retry still owns its entire wire packet. Never let its
         * payload fall through to the text command dispatcher. */
        asset_discard_remaining = ASSET_BLOCK + 4u; return 1;
    }
    if (asset_state != 2u && byte == ASSET_CAN) {
        asset_cancel_pending = 1u; return 1;
    }
    if (asset_cancel_pending) return 1;
    if (asset_state == 1u) {
        if (byte == ASSET_SOH) { asset_state = 2u; asset_packet_pos = 0u; }
        else if (byte == ASSET_EOT) {
            if (asset_received != asset_length ||
                (asset_crc ^ 0xFFFFFFFFu) != asset_expected_crc ||
                (asset_id==10u && asset_expected_crc!=BULLET_PACKED_COLOR_CRC) ||
                (asset_id==11u && asset_expected_crc!=BULLET_PACKED_MASK_CRC) ||
                (asset_id>=24u && asset_id<=27u && asset_expected_crc!=((const uint32_t[])MISSILE_CRC_VALUES)[asset_id-24u])) {
                asset_error = 2u; asset_text("ASSET ERROR CRC_OR_LENGTH\r\n");
            } else asset_finish = 1u;
            asset_release();
        }
        else return 1;
        return 1;
    }
    if (asset_state != 2u) return 1;
    if (asset_packet_pos == 0u) { asset_packet_seq = byte; asset_packet_pos++; return 1; }
    if (asset_packet_pos == 1u) { asset_packet_inverse = byte; asset_packet_pos++; return 1; }
    if (asset_packet_pos < (2u + ASSET_BLOCK)) {
        asset_packet[asset_packet_pos - 2u] = byte; asset_packet_pos++; return 1;
    }
    if (asset_packet_pos == (2u + ASSET_BLOCK)) {
        asset_packet_crc_hi = byte; asset_packet_pos++; return 1;
    }
    /* Last CRC byte completes the packet. */
    crc = (uint16_t)((uint16_t)asset_packet_crc_hi << 8) | byte;
    expected_inverse = (uint8_t)(255u - asset_packet_seq);
    if (asset_packet_inverse != expected_inverse ||
        crc != asset_crc16(asset_packet)) {
        asset_tx(ASSET_NAK); asset_packet_pos = 0u; asset_state = AS_HEADER; return 1;
    }
    if (asset_packet_seq == (uint8_t)(asset_expected_seq - 1u) && asset_have_previous) {
        asset_tx(memcmp(asset_previous, asset_packet, ASSET_BLOCK) == 0 ? ASSET_ACK : ASSET_NAK);
        asset_packet_pos = 0u; asset_state = 1u; return 1;
    }
    if (asset_packet_seq != asset_expected_seq || asset_received + ASSET_BLOCK > asset_length) {
        asset_tx(ASSET_NAK); asset_packet_pos = 0u; asset_state = AS_HEADER; return 1;
    }
    /* Hold this packet until its own successful B response. All MMIO work
     * runs at a guarded main-loop boundary, never recursively in a writer. */
    asset_state = AS_OPEN; asset_stage_started = asset_now();
    asset_packet_pos = 0u; asset_word = 0u;
    return 1;
}

/* Registration of a completely stored asset: collision metadata from the last packet, then the valid bit. */
static void asset_register(void)
{
    if(asset_id==11u || asset_id==13u){
        uint32_t type=asset_id==13u?1u:0u;
        memcpy(asset_bullet_masks[type],asset_previous,64u);
        asset_bullet_mask_valid|=(uint8_t)(1u<<type);
    }
    if(asset_id==25u || asset_id==27u){
        uint32_t type=(asset_id-25u)/2u;
        memcpy(asset_missile_masks[type],asset_previous,64u);
        asset_missile_mask_valid|=(uint8_t)(1u<<type);
    }
    if(asset_id==PLAYER_MASK_ID){asset_player_mask_opaque=asset_player_mask_pending;asset_player_mask_valid=1;}
    asset_valid_set(asset_id, 1); asset_eot_replay = 1u;
    asset_tx(ASSET_ACK); asset_text("ASSET STORED\r\n");
}
uint32_t asset_upload_error(void) { return asset_error; }
#ifdef ASSET_UPLOAD_SERIAL
/* GHD3S: the transport CRC proved what arrived over the UART; this proves what is in DDR. The hardware reader (rtl/ddr_crc_reader.v)
 * reads the slot back through the compositor port while no PL owner is active; only a matching CRC registers the asset. A busy or
 * refused reader is retried until ASSET_TIMEOUT_SEC. A mismatch answers NAK + ASSET ERROR DDR_CRC and leaves the ID invalid. */
static void asset_verify_service(u64 now)
{
    uint32_t c = 0u; int r;
    if (!asset_verify_running) {
        if (ddr_crc_start(asset_bases[asset_id], asset_length) == 0) { asset_verify_running = 1u; return; }
        if (now - asset_stage_started >= (u64)BSP_CLINT_HZ * ASSET_TIMEOUT_SEC) {
            asset_state = AS_IDLE; asset_error = 16u; asset_cancel_pending = 0u;
            asset_tx(ASSET_NAK); asset_text("ASSET ERROR DDR_VERIFY_BUSY\r\n");
        }
        return;
    }
    r = ddr_crc_poll(&c);
    if (r == 0) return;
    asset_verify_running = 0u; asset_state = AS_IDLE;
    if (asset_cancel_pending) { asset_cancel_pending = 0u; asset_text("ASSET CANCELLED\r\n"); return; }
    if (r > 0 && c == asset_expected_crc) { asset_register(); return; }
    asset_error = 15u; asset_tx(ASSET_NAK);
    asset_text("ASSET ERROR DDR_CRC ID="); asset_decimal(asset_id);
    asset_text(" DDR="); uart_writeHex(BSP_UART_TERMINAL, (int)c);
    asset_text(" EXPECT="); uart_writeHex(BSP_UART_TERMINAL, (int)asset_expected_crc);
    asset_text(r < 0 ? " READ_ERROR\r\n" : "\r\n");
}
#endif
void asset_upload_service(void)
{
    uint32_t status, i;
    u64 now;
    if (asset_resource_guard)
        return;
    if (asset_state == AS_IDLE) return;
    now = asset_now();
#ifdef ASSET_UPLOAD_SERIAL
    if (asset_state == AS_VERIFY) { asset_verify_service(now); return; }
#endif
    if (asset_cancel_pending) { asset_finish = 0u; asset_release(); }
    status = read_u32(asset_addr(ASSET_REG_STATUS));
    if (asset_state == AS_DRAIN) {
        if ((status & (ASSET_STATUS_OWNER | ASSET_STATUS_IDLE)) == ASSET_STATUS_IDLE) {
            asset_state = AS_IDLE;
            if (asset_resume_hook) asset_resume_hook();
            if (asset_finish) {
#ifdef ASSET_UPLOAD_SERIAL
                if (ddr_crc_present()) { asset_state = AS_VERIFY; asset_verify_running = 0u; asset_stage_started = now; return; }
#endif
                asset_register();
            } else if (asset_cancel_pending) asset_text("ASSET CANCELLED\r\n");
            asset_cancel_pending = 0u;
        } else if (!asset_held_reported && now - asset_stage_started >= (u64)BSP_CLINT_HZ * ASSET_TIMEOUT_SEC) {
            asset_held_reported = 1u; asset_text("ASSET ERROR OWNER_HELD\r\n");
        }
        return;
    }
    if (now - ((asset_state == AS_HEADER || asset_state == AS_PACKET) ? asset_last_activity : asset_stage_started) >=
        (u64)BSP_CLINT_HZ * ASSET_TIMEOUT_SEC) {
        asset_error = 3u; asset_text("ASSET ERROR TIMEOUT\r\n"); asset_release(); return;
    }
    if (asset_state == AS_PREPARE) {
        int ready = asset_prepare_hook ? asset_prepare_hook() : 1;
        if (ready < 0) { asset_error = 1u; asset_text("ASSET ERROR PREPARE\r\n"); asset_release(); return; }
        if (!ready) return;
        write_u32(ASSET_CTRL_CLEAR, asset_addr(ASSET_REG_CONTROL));
        write_u32(asset_id, asset_addr(ASSET_REG_ID));
        write_u32(ASSET_CTRL_CLAIM, asset_addr(ASSET_REG_CONTROL));
        asset_state = AS_CLAIM; return;
    }
    if (asset_state == AS_CLAIM) {
        if (!(status & ASSET_STATUS_OWNER)) {
            /* A scanout descriptor may close the quiescent grant window.
             * CLAIM is not queued in RTL; retry only with no held owner. */
            asset_state = AS_PREPARE; return;
        }
        asset_state = AS_HEADER; asset_last_activity = now;
        asset_text("ASSET READY ");
        asset_decimal(asset_id);
        asset_text("\r\n");
        return;
    }
    if ((status & ASSET_STATUS_ERROR) || !(status & ASSET_STATUS_OWNER)) {
        asset_error = 13u; asset_text("ASSET ERROR DDR\r\n"); asset_release(); return;
    }
    switch (asset_state) {
    case AS_OPEN:
        write_u32(asset_offset, asset_addr(ASSET_REG_OFFSET));
        write_u32(ASSET_CTRL_OPEN, asset_addr(ASSET_REG_CONTROL));
        asset_state = AS_FEED; break;
    case AS_FEED:
        if (!(status & ASSET_STATUS_DATA_READY)) break;
        i = (uint32_t)asset_word * 4u;
        write_u32((uint32_t)asset_packet[i] | ((uint32_t)asset_packet[i+1u] << 8) |
                  ((uint32_t)asset_packet[i+2u] << 16) | ((uint32_t)asset_packet[i+3u] << 24),
                  asset_addr(ASSET_REG_DATA));
        if (++asset_word == 32u) asset_state = AS_COMMIT;
        break;
    case AS_COMMIT:
        if (!(status & ASSET_STATUS_COMMIT_READY)) break;
        write_u32(ASSET_CTRL_COMMIT, asset_addr(ASSET_REG_CONTROL));
        asset_state = AS_RESPONSE; break;
    case AS_RESPONSE:
        if (!(status & ASSET_STATUS_DONE)) break;
        if (read_u32(asset_addr(ASSET_REG_COMMITTED)) != asset_offset + ASSET_BLOCK ||
            read_u32(asset_addr(ASSET_REG_DONE_OFFSET)) != asset_offset) {
            asset_error = 14u; asset_text("ASSET ERROR COMPLETION\r\n"); asset_release(); break;
        }
        memcpy(asset_previous, asset_packet, ASSET_BLOCK);
        if(asset_id==PLAYER_MASK_ID)
            for(i=0u;i<ASSET_BLOCK;++i)if(asset_packet[i])asset_player_mask_pending=1;
        asset_have_previous = 1u; asset_received += ASSET_BLOCK; asset_offset += ASSET_BLOCK;
        for (i=0u;i<ASSET_BLOCK;++i) asset_crc = asset_crc32_update(asset_crc,asset_packet[i]);
        asset_expected_seq = (uint8_t)(asset_expected_seq + 1u);
        asset_tx(ASSET_ACK); asset_state = AS_HEADER; asset_last_activity = now;
        break;
    default: break;
    }
}
