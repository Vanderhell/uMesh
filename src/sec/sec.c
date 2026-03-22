#include "sec.h"
#include "keys.h"
#include <string.h>

#ifdef UMESH_USE_MICROCRYPT
#include "mcrypt.h"
#else
/* Forward declarations for built-in AES used in CTR mode */
static void aes128_ecb_encrypt_block(const uint8_t key[16],
                                      const uint8_t in[16],
                                      uint8_t out[16]);
/* Built-in SHA-256 / HMAC-SHA256 */
static void hmac_sha256(const uint8_t *key, uint8_t key_len,
                        const uint8_t *data, uint16_t data_len,
                        uint8_t mac[32]);
#endif

/* ── Module state ──────────────────────────────────────── */

#define MAX_REPLAY_STATES  UMESH_MAX_NODES

typedef struct {
    uint8_t  src_node;
    uint16_t last_seq;
    uint32_t window_bitmap;
    bool     valid;
} replay_state_t;

static umesh_security_t s_level       = UMESH_SEC_NONE;
static uint8_t          s_enc_key[16];
static uint8_t          s_auth_key[16];
static uint8_t          s_salt[3];   /* SALT = first 3 bytes SHA256(ENC_KEY) */
static uint8_t          s_net_id     = 0;
static replay_state_t   s_replay[MAX_REPLAY_STATES];

/* ── Helpers ───────────────────────────────────────────── */

static void salt_init(void)
{
    uint8_t digest[32];
#ifdef UMESH_USE_MICROCRYPT
    mcrypt_sha256(s_enc_key, 16, digest);
#else
    /* Use a simple deterministic derivation from enc_key when no SHA256 */
    /* This path is taken only when built-in fallback is used */
    hmac_sha256(s_enc_key, 16, s_enc_key, 16, digest);
#endif
    s_salt[0] = digest[0];
    s_salt[1] = digest[1];
    s_salt[2] = digest[2];
}

/*
 * Build AES-CTR NONCE (16 bytes):
 * [SRC_NODE(1)][NET_ID(1)][SEQ_NUM_LO(1)][SEQ_NUM_HI(1)]
 * [SALT(3)][COUNTER(9)]
 * Counter starts at 0, incremented per 16-byte block.
 */
static void build_nonce(uint8_t src, uint8_t seq_lo, uint8_t seq_hi,
                        uint32_t counter, uint8_t nonce[16])
{
    nonce[0]  = src;
    nonce[1]  = s_net_id;
    nonce[2]  = seq_lo;
    nonce[3]  = seq_hi;
    nonce[4]  = s_salt[0];
    nonce[5]  = s_salt[1];
    nonce[6]  = s_salt[2];
    /* 9 bytes for counter (big-endian) */
    nonce[7]  = 0;
    nonce[8]  = 0;
    nonce[9]  = 0;
    nonce[10] = 0;
    nonce[11] = 0;
    nonce[12] = 0;
    nonce[13] = (uint8_t)((counter >> 16) & 0xFF);
    nonce[14] = (uint8_t)((counter >>  8) & 0xFF);
    nonce[15] = (uint8_t)( counter        & 0xFF);
}

/*
 * AES-CTR encrypt/decrypt (symmetric).
 * Uses ENC_KEY.
 */
static void aes_ctr_crypt(uint8_t src, uint8_t seq_lo, uint8_t seq_hi,
                           uint8_t *data, uint8_t len)
{
    uint8_t nonce[16];
    uint8_t keystream[16];
    uint8_t i = 0;
    uint32_t block = 0;

    while (i < len) {
        uint8_t chunk = (uint8_t)(len - i);
        if (chunk > 16) chunk = 16;

        build_nonce(src, seq_lo, seq_hi, block, nonce);

#ifdef UMESH_USE_MICROCRYPT
        {
            mcrypt_aes128_t ctx;
            mcrypt_aes128_init(&ctx, s_enc_key);
            mcrypt_aes128_encrypt_block(&ctx, nonce, keystream);
        }
#else
        aes128_ecb_encrypt_block(s_enc_key, nonce, keystream);
#endif
        {
            uint8_t j;
            for (j = 0; j < chunk; j++) {
                data[i + j] ^= keystream[j];
            }
        }
        i += chunk;
        block++;
    }
}

/*
 * Constant-time MIC comparison (timing attack protection).
 */
static bool mic_verify_ct(const uint8_t *a, const uint8_t *b, uint8_t len)
{
    uint8_t diff = 0;
    uint8_t i;
    for (i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (diff == 0);
}

/*
 * Compute 4-byte MIC = first 4 bytes of HMAC-SHA256(AUTH_KEY,
 *   net_id || dst || src || seq_lo || seq_hi || encrypted_payload)
 */
static void compute_mic(const umesh_frame_t *frame, uint8_t mic[4])
{
    uint8_t  msg[6 + UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE];
    uint8_t  msg_len;
    uint8_t  full_mac[32];

    msg[0] = frame->net_id;
    msg[1] = frame->dst;
    msg[2] = frame->src;
    msg[3] = (uint8_t)(frame->seq_num & 0xFF);
    msg[4] = (uint8_t)(frame->seq_num >> 8);
    msg[5] = frame->cmd;
    msg_len = 6;
    if (frame->payload_len > 0) {
        memcpy(&msg[6], frame->payload, frame->payload_len);
        msg_len = (uint8_t)(msg_len + frame->payload_len);
    }

#ifdef UMESH_USE_MICROCRYPT
    mcrypt_hmac_sha256(s_auth_key, 16, msg, msg_len, full_mac);
#else
    hmac_sha256(s_auth_key, 16, msg, msg_len, full_mac);
#endif

    mic[0] = full_mac[0];
    mic[1] = full_mac[1];
    mic[2] = full_mac[2];
    mic[3] = full_mac[3];
}

/* ── Replay window ─────────────────────────────────────── */

static replay_state_t *replay_find_or_alloc(uint8_t src)
{
    uint8_t i;
    for (i = 0; i < MAX_REPLAY_STATES; i++) {
        if (s_replay[i].valid && s_replay[i].src_node == src) {
            return &s_replay[i];
        }
    }
    /* Allocate new slot */
    for (i = 0; i < MAX_REPLAY_STATES; i++) {
        if (!s_replay[i].valid) {
            s_replay[i].valid         = true;
            s_replay[i].src_node      = src;
            s_replay[i].last_seq      = 0;
            s_replay[i].window_bitmap = 0;
            return &s_replay[i];
        }
    }
    return NULL; /* table full */
}

static bool replay_check_and_update(uint8_t src, uint16_t seq)
{
    replay_state_t *rs = replay_find_or_alloc(src);
    uint16_t delta;

    if (!rs) return false; /* no slot — accept conservatively */

    if (!rs->window_bitmap && rs->last_seq == 0) {
        /* First packet from this source */
        rs->last_seq      = seq;
        rs->window_bitmap = 1;
        return true;
    }

    if (seq > rs->last_seq) {
        /* New seq — advance window */
        delta = seq - rs->last_seq;
        if (delta >= UMESH_SEQ_WINDOW) {
            rs->window_bitmap = 0;
        } else {
            rs->window_bitmap <<= delta;
        }
        rs->window_bitmap |= 1;
        rs->last_seq = seq;
        return true;
    }

    /* Old or duplicate */
    delta = rs->last_seq - seq;
    if (delta >= UMESH_SEQ_WINDOW) {
        return false; /* too old */
    }
    if (rs->window_bitmap & ((uint32_t)1 << delta)) {
        return false; /* duplicate */
    }
    /* Within window, not yet seen */
    rs->window_bitmap |= ((uint32_t)1 << delta);
    return true;
}

/* ── Public API ────────────────────────────────────────── */

umesh_result_t sec_init(const uint8_t   *master_key,
                        uint8_t          net_id,
                        umesh_security_t level)
{
    uint8_t i;

    s_level  = level;
    s_net_id = net_id;

    for (i = 0; i < MAX_REPLAY_STATES; i++) {
        s_replay[i].valid = false;
    }

    if (level != UMESH_SEC_NONE) {
        keys_derive(master_key, net_id, s_enc_key, s_auth_key);
        salt_init();
    }
    return UMESH_OK;
}

umesh_result_t sec_encrypt_frame(umesh_frame_t *frame)
{
    uint8_t mic[4];
    uint8_t seq_lo, seq_hi;

    if (!frame) return UMESH_ERR_NULL_PTR;
    if (s_level == UMESH_SEC_NONE) return UMESH_OK;

    seq_lo = (uint8_t)(frame->seq_num & 0xFF);
    seq_hi = (uint8_t)(frame->seq_num >> 8);

    if (s_level == UMESH_SEC_FULL) {
        /* 1. AES-CTR encrypt payload */
        aes_ctr_crypt(frame->src, seq_lo, seq_hi,
                      frame->payload, frame->payload_len);
    }

    /* 2. Compute MIC over header + encrypted payload */
    compute_mic(frame, mic);

    /* 3. Append MIC after payload */
    if (frame->payload_len + UMESH_MIC_SIZE >
        UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE) {
        return UMESH_ERR_TOO_LONG;
    }
    memcpy(&frame->payload[frame->payload_len], mic, UMESH_MIC_SIZE);
    frame->payload_len = (uint8_t)(frame->payload_len + UMESH_MIC_SIZE);
    frame->flags |= UMESH_FLAG_ENCRYPTED;
    return UMESH_OK;
}

umesh_result_t sec_decrypt_frame(umesh_frame_t *frame)
{
    uint8_t mic_received[UMESH_MIC_SIZE];
    uint8_t mic_computed[UMESH_MIC_SIZE];
    uint8_t seq_lo, seq_hi;

    if (!frame) return UMESH_ERR_NULL_PTR;
    if (s_level == UMESH_SEC_NONE) return UMESH_OK;

    if (frame->payload_len < UMESH_MIC_SIZE) {
        return UMESH_ERR_MIC_FAIL;
    }

    /* Strip MIC from end of payload */
    frame->payload_len = (uint8_t)(frame->payload_len - UMESH_MIC_SIZE);
    memcpy(mic_received,
           &frame->payload[frame->payload_len],
           UMESH_MIC_SIZE);

    /* 1. Verify MIC (over header + still-encrypted payload) */
    compute_mic(frame, mic_computed);
    if (!mic_verify_ct(mic_received, mic_computed, UMESH_MIC_SIZE)) {
        return UMESH_ERR_MIC_FAIL;
    }

    /* 2. Check replay */
    if (!replay_check_and_update(frame->src, frame->seq_num)) {
        return UMESH_ERR_REPLAY;
    }

    /* 3. AES-CTR decrypt */
    if (s_level == UMESH_SEC_FULL) {
        seq_lo = (uint8_t)(frame->seq_num & 0xFF);
        seq_hi = (uint8_t)(frame->seq_num >> 8);
        aes_ctr_crypt(frame->src, seq_lo, seq_hi,
                      frame->payload, frame->payload_len);
    }

    frame->flags &= (uint8_t)~UMESH_FLAG_ENCRYPTED;
    return UMESH_OK;
}

void sec_regenerate_salt(void)
{
    salt_init();
}

/* ──────────────────────────────────────────────────────────
 * Built-in AES-128 ECB + SHA-256 + HMAC-SHA256
 * (fallback — only compiled when microcrypt is not available)
 * ──────────────────────────────────────────────────────── */
#ifndef UMESH_USE_MICROCRYPT

/* ── AES-128 (same tables as in keys.c, duplicated for self-contained module) */
static const uint8_t S_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t S_RCON[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint8_t s_xtime(uint8_t a)
{
    return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0x00));
}
static uint8_t s_gmul(uint8_t a, uint8_t b)
{
    uint8_t p = 0, i;
    for (i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        a = s_xtime(a);
        b >>= 1;
    }
    return p;
}
static void s_key_expand(const uint8_t key[16], uint8_t ek[176])
{
    uint8_t i, j, temp[4];
    memcpy(ek, key, 16);
    for (i = 1; i <= 10; i++) {
        temp[0] = S_SBOX[ek[(i-1)*16+13]] ^ S_RCON[i-1];
        temp[1] = S_SBOX[ek[(i-1)*16+14]];
        temp[2] = S_SBOX[ek[(i-1)*16+15]];
        temp[3] = S_SBOX[ek[(i-1)*16+12]];
        for (j = 0; j < 4; j++) {
            ek[i*16+j]    = ek[(i-1)*16+j]    ^ temp[j];
            ek[i*16+j+4]  = ek[(i-1)*16+j+4]  ^ ek[i*16+j];
            ek[i*16+j+8]  = ek[(i-1)*16+j+8]  ^ ek[i*16+j+4];
            ek[i*16+j+12] = ek[(i-1)*16+j+12] ^ ek[i*16+j+8];
        }
    }
}
static void s_add_rk(uint8_t s[16], const uint8_t *rk)
{
    uint8_t i;
    for (i = 0; i < 16; i++) s[i] ^= rk[i];
}
static void s_sub_bytes(uint8_t s[16])
{
    uint8_t i;
    for (i = 0; i < 16; i++) s[i] = S_SBOX[s[i]];
}
static void s_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t = s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t = s[2]; s[2]=s[10]; s[10]=t;
    t = s[6]; s[6]=s[14]; s[14]=t;
    t = s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
}
static void s_mix_cols(uint8_t s[16])
{
    uint8_t c, a0, a1, a2, a3;
    for (c = 0; c < 4; c++) {
        a0=s[c*4]; a1=s[c*4+1]; a2=s[c*4+2]; a3=s[c*4+3];
        s[c*4]   = (uint8_t)(s_gmul(a0,2)^s_gmul(a1,3)^a2         ^a3);
        s[c*4+1] = (uint8_t)(a0         ^s_gmul(a1,2)^s_gmul(a2,3)^a3);
        s[c*4+2] = (uint8_t)(a0         ^a1          ^s_gmul(a2,2)^s_gmul(a3,3));
        s[c*4+3] = (uint8_t)(s_gmul(a0,3)^a1         ^a2          ^s_gmul(a3,2));
    }
}
static void aes128_ecb_encrypt_block(const uint8_t key[16],
                                      const uint8_t in[16],
                                      uint8_t out[16])
{
    uint8_t ek[176], state[16], r;
    s_key_expand(key, ek);
    memcpy(state, in, 16);
    s_add_rk(state, ek);
    for (r = 1; r <= 10; r++) {
        s_sub_bytes(state);
        s_shift_rows(state);
        if (r < 10) s_mix_cols(state);
        s_add_rk(state, ek + r*16);
    }
    memcpy(out, state, 16);
}

/* ── SHA-256 ────────────────────────────────────────────── */
#define ROTR32(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define CH(x,y,z)   (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z)  (((x)&(y))^((x)&(z))^((y)&(z)))
#define S0(x)       (ROTR32(x,2)^ROTR32(x,13)^ROTR32(x,22))
#define S1(x)       (ROTR32(x,6)^ROTR32(x,11)^ROTR32(x,25))
#define G0(x)       (ROTR32(x,7)^ROTR32(x,18)^((x)>>3))
#define G1(x)       (ROTR32(x,17)^ROTR32(x,19)^((x)>>10))

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total_bits;
} sha256_ctx_t;

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->h[0] = 0x6a09e667; ctx->h[1] = 0xbb67ae85;
    ctx->h[2] = 0x3c6ef372; ctx->h[3] = 0xa54ff53a;
    ctx->h[4] = 0x510e527f; ctx->h[5] = 0x9b05688c;
    ctx->h[6] = 0x1f83d9ab; ctx->h[7] = 0x5be0cd19;
    ctx->buf_len   = 0;
    ctx->total_bits = 0;
}

static void sha256_process_block(sha256_ctx_t *ctx, const uint8_t blk[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|
               ((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    }
    for (i = 16; i < 64; i++) {
        w[i] = G1(w[i-2]) + w[i-7] + G0(w[i-15]) + w[i-16];
    }
    a=ctx->h[0]; b=ctx->h[1]; c=ctx->h[2]; d=ctx->h[3];
    e=ctx->h[4]; f=ctx->h[5]; g=ctx->h[6]; h=ctx->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + S1(e) + CH(e,f,g) + SHA256_K[i] + w[i];
        t2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }
    ctx->h[0]+=a; ctx->h[1]+=b; ctx->h[2]+=c; ctx->h[3]+=d;
    ctx->h[4]+=e; ctx->h[5]+=f; ctx->h[6]+=g; ctx->h[7]+=h;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        ctx->total_bits += 8;
        if (ctx->buf_len == 64) {
            sha256_process_block(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32])
{
    uint64_t bits = ctx->total_bits;
    int i;
    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 56) {
        while (ctx->buf_len < 64) ctx->buf[ctx->buf_len++] = 0;
        sha256_process_block(ctx, ctx->buf);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56) ctx->buf[ctx->buf_len++] = 0;
    for (i = 7; i >= 0; i--) {
        ctx->buf[56 + (7-i)] = (uint8_t)(bits >> (i*8));
    }
    sha256_process_block(ctx, ctx->buf);
    for (i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(ctx->h[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->h[i] >>  8);
        digest[i*4+3] = (uint8_t)(ctx->h[i]       );
    }
}

static void hmac_sha256(const uint8_t *key, uint8_t key_len,
                        const uint8_t *data, uint16_t data_len,
                        uint8_t mac[32])
{
    sha256_ctx_t ctx;
    uint8_t ipad[64], opad[64], inner[32];
    uint8_t i;

    memset(ipad, 0x36, 64);
    memset(opad, 0x5C, 64);
    for (i = 0; i < key_len && i < 64; i++) {
        ipad[i] ^= key[i];
        opad[i] ^= key[i];
    }
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, (uint32_t)data_len);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, mac);
}

#endif /* !UMESH_USE_MICROCRYPT */
