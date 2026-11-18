/* SHA-1 (FIPS 180-4), base64 and small text helpers for the WebSocket conformance mods' own servers (ws.c,
   ws_flood.c): what a server needs to answer an upgrade with its Sec-WebSocket-Accept. */
/* ---- SHA-1 (FIPS 180-4) and base64, for the server's Accept ---- */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1(const uint8_t* data, uint32_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t block[64];
    const uint64_t bits = (uint64_t)n * 8u;
    const uint32_t total = ((n + 8u) / 64u + 1u) * 64u;
    for (uint32_t at = 0; at < total; at += 64u) {
        for (uint32_t i = 0; i < 64u; i++) {
            const uint32_t k = at + i;
            uint8_t b = 0;
            if (k < n) b = data[k];
            else if (k == n) b = 0x80u;
            else if (k >= total - 8u) b = (uint8_t)(bits >> (8u * (total - 1u - k)));
            block[i] = b;
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 |
                   block[i * 4 + 3];
        }
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i];
    }
}

static uint32_t base64(const uint8_t* in, uint32_t n, char* out) {
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3u) {
        const uint32_t v = (uint32_t)in[i] << 16 | (i + 1u < n ? (uint32_t)in[i + 1] << 8 : 0u) |
                           (i + 2u < n ? in[i + 2] : 0u);
        out[o++] = abc[(v >> 18) & 63u];
        out[o++] = abc[(v >> 12) & 63u];
        out[o++] = i + 1u < n ? abc[(v >> 6) & 63u] : '=';
        out[o++] = i + 2u < n ? abc[v & 63u] : '=';
    }
    out[o] = 0;
    return o;
}

static uint32_t length_of(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int same(const void* a, const void* b, uint32_t n) {
    const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
    for (uint32_t i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static int starts(const char* text, uint32_t n, const char* with) {
    const uint32_t m = length_of(with);
    return n >= m && same(text, with, m);
}
static uint32_t put(char* to, uint32_t at, const char* s) {
    for (uint32_t i = 0; s[i]; i++) to[at++] = s[i];
    return at;
}
