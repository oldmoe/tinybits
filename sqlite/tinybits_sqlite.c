#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <errno.h>
#include "../dist/tinybits.h"

#define MAX_DEPTH 512
#define MAX_STEPS 32

typedef struct {
    tiny_bits_unpacker *un;
    tiny_bits_packer *pk;
    tiny_bits_packer *tree_pk; // reused by tb_group_encode while not busy
    int tree_busy;
} tb_ctx;

typedef struct {
    char *p;
    size_t n, cap;
    int oom;
} sbuf;

static void sb_put(sbuf *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *np = sqlite3_realloc64(b->p, cap);
        if (!np) { b->oom = 1; return; }
        b->p = np;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

static void sb_putc(sbuf *b, char c) { sb_put(b, &c, 1); }

static void sb_json_str(sbuf *b, const char *s, size_t n) {
    static const char hex[] = "0123456789abcdef";
    sb_putc(b, '"');
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        sb_put(b, s + run, i - run);
        run = i + 1;
        switch (c) {
            case '"': sb_put(b, "\\\"", 2); break;
            case '\\': sb_put(b, "\\\\", 2); break;
            case '\n': sb_put(b, "\\n", 2); break;
            case '\r': sb_put(b, "\\r", 2); break;
            case '\t': sb_put(b, "\\t", 2); break;
            case '\b': sb_put(b, "\\b", 2); break;
            case '\f': sb_put(b, "\\f", 2); break;
            default: {
                char e[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                sb_put(b, e, 6);
            }
        }
    }
    sb_put(b, s + run, n - run);
    sb_putc(b, '"');
}

static void sb_double(sbuf *b, double d) {
    char t[40];
    int n = 0;
    for (int prec = 15; prec <= 17; prec++) {
        n = snprintf(t, sizeof t, "%.*g", prec, d);
        if (strtod(t, NULL) == d) break;
    }
    if (!strpbrk(t, ".eE")) { t[n++] = '.'; t[n++] = '0'; }
    sb_put(b, t, n);
}

#define skip_value tiny_bits_unpacker_skip

static int emit(tiny_bits_unpacker *u, sbuf *b, int depth);

static int emit_key(tiny_bits_unpacker *u, sbuf *b) {
    tiny_bits_value v;
    char t[24];
    switch (unpack_value(u, &v)) {
        case TINY_BITS_STR: sb_json_str(b, v.str_blob_val.data, v.str_blob_val.length); return 1;
        case TINY_BITS_INT: sb_put(b, t, snprintf(t, sizeof t, "\"%lld\"", (long long)v.int_val)); return 1;
        default: return 0;
    }
}

static int emit(tiny_bits_unpacker *u, sbuf *b, int depth) {
    if (depth > MAX_DEPTH) return 0;
    tiny_bits_value v;
    char t[24];
    switch (unpack_value(u, &v)) {
        case TINY_BITS_INT: sb_put(b, t, snprintf(t, sizeof t, "%lld", (long long)v.int_val)); return 1;
        case TINY_BITS_DOUBLE: sb_double(b, v.double_val); return 1;
        case TINY_BITS_DATETIME: sb_double(b, v.datetime_val.unixtime); return 1;
        case TINY_BITS_STR: sb_json_str(b, v.str_blob_val.data, v.str_blob_val.length); return 1;
        case TINY_BITS_TRUE: sb_put(b, "true", 4); return 1;
        case TINY_BITS_FALSE: sb_put(b, "false", 5); return 1;
        case TINY_BITS_NULL: case TINY_BITS_NAN: case TINY_BITS_INF: case TINY_BITS_N_INF:
            sb_put(b, "null", 4); return 1;
        case TINY_BITS_BLOB: {
            static const char hex[] = "0123456789abcdef";
            const unsigned char *d = (const unsigned char *)v.str_blob_val.data;
            sb_putc(b, '"');
            for (size_t i = 0; i < v.str_blob_val.length; i++) {
                sb_putc(b, hex[d[i] >> 4]);
                sb_putc(b, hex[d[i] & 15]);
            }
            sb_putc(b, '"');
            return 1;
        }
        case TINY_BITS_ARRAY: {
            size_t n = v.length;
            sb_putc(b, '[');
            for (size_t i = 0; i < n; i++) {
                if (i) sb_putc(b, ',');
                if (!emit(u, b, depth + 1)) return 0;
            }
            sb_putc(b, ']');
            return 1;
        }
        case TINY_BITS_MAP: {
            size_t n = v.length;
            sb_putc(b, '{');
            for (size_t i = 0; i < n; i++) {
                if (i) sb_putc(b, ',');
                if (!emit_key(u, b)) return 0;
                sb_putc(b, ':');
                if (!emit(u, b, depth + 1)) return 0;
            }
            sb_putc(b, '}');
            return 1;
        }
        default: return 0;
    }
}

static int copy_value(tiny_bits_unpacker *u, tiny_bits_packer *p, int depth) {
    if (depth > MAX_DEPTH) return 0;
    tiny_bits_value v;
    switch (unpack_value(u, &v)) {
        case TINY_BITS_INT: return pack_int(p, v.int_val);
        case TINY_BITS_DOUBLE: return pack_double(p, v.double_val);
        case TINY_BITS_DATETIME: return pack_datetime(p, v.datetime_val.unixtime, (int16_t)v.datetime_val.offset);
        case TINY_BITS_STR: return pack_str(p, (char *)v.str_blob_val.data, (uint32_t)v.str_blob_val.length);
        case TINY_BITS_BLOB: return pack_blob(p, v.str_blob_val.data, (int)v.str_blob_val.length);
        case TINY_BITS_TRUE: return pack_true(p);
        case TINY_BITS_FALSE: return pack_false(p);
        case TINY_BITS_NULL: return pack_null(p);
        case TINY_BITS_NAN: return pack_nan(p);
        case TINY_BITS_INF: return pack_infinity(p);
        case TINY_BITS_N_INF: return pack_negative_infinity(p);
        case TINY_BITS_ARRAY: {
            size_t n = v.length;
            if (!pack_arr(p, (int)n)) return 0;
            for (size_t i = 0; i < n; i++) if (!copy_value(u, p, depth + 1)) return 0;
            return 1;
        }
        case TINY_BITS_MAP: {
            size_t n = v.length;
            if (!pack_map(p, (int)n)) return 0;
            for (size_t i = 0; i < 2 * n; i++) if (!copy_value(u, p, depth + 1)) return 0;
            return 1;
        }
        default: return 0;
    }
}

typedef struct {
    int is_idx;
    size_t idx;
    const char *key;
    size_t klen;
} step;

static int parse_path(const char *s, step *steps, int *count) {
    int n = 0;
    if (*s++ != '$') return 0;
    while (*s) {
        if (n == MAX_STEPS) return 0;
        step *st = &steps[n];
        if (*s == '.') {
            s++;
            if (*s == '"') {
                st->key = ++s;
                while (*s && *s != '"') s++;
                if (*s != '"') return 0;
                st->klen = s - st->key;
                s++;
            } else {
                st->key = s;
                while (*s && *s != '.' && *s != '[') s++;
                st->klen = s - st->key;
                if (!st->klen) return 0;
            }
            st->is_idx = 0;
        } else if (*s == '[') {
            s++;
            if (*s == '"') {
                st->key = ++s;
                while (*s && *s != '"') s++;
                if (*s != '"') return 0;
                st->klen = s - st->key;
                s++;
                st->is_idx = 0;
            } else {
                char *e;
                if (*s < '0' || *s > '9') return 0;
                st->idx = strtoull(s, &e, 10);
                s = e;
                st->is_idx = 1;
            }
            if (*s++ != ']') return 0;
        } else {
            return 0;
        }
        n++;
    }
    *count = n;
    return 1;
}

// positions u at the start of the value the path points to; 1 found, 0 missing
static int find(tiny_bits_unpacker *u, const step *steps, int n) {
    tiny_bits_value v, k;
    for (int s = 0; s < n; s++) {
        size_t start = u->current_pos;
        enum tiny_bits_type t = unpack_value(u, &v);
        if (steps[s].is_idx) {
            if (t != TINY_BITS_ARRAY || steps[s].idx >= v.length) return 0;
            for (size_t i = 0; i < steps[s].idx; i++) if (!skip_value(u)) return 0;
        } else {
            if (t != TINY_BITS_MAP) return 0;
            size_t pairs = v.length, i;
            for (i = 0; i < pairs; i++) {
                enum tiny_bits_type kt = unpack_value(u, &k);
                if (kt == TINY_BITS_STR) {
                    if (k.str_blob_val.length == steps[s].klen
                        && memcmp(k.str_blob_val.data, steps[s].key, steps[s].klen) == 0) break;
                } else if (kt != TINY_BITS_INT) {
                    return 0;
                }
                if (!skip_value(u)) return 0;
            }
            if (i == pairs) return 0;
        }
        (void)start;
    }
    return 1;
}

static const unsigned char *blob_arg(sqlite3_context *c, sqlite3_value *v, tb_ctx *x, int *ok) {
    *ok = 0;
    if (sqlite3_value_type(v) != SQLITE_BLOB) {
        sqlite3_result_error(c, "tinybits: argument must be a blob", -1);
        return NULL;
    }
    int n = sqlite3_value_bytes(v);
    if (n < 1) {
        sqlite3_result_error(c, "tinybits: empty blob", -1);
        return NULL;
    }
    const unsigned char *p = sqlite3_value_blob(v);
    tiny_bits_unpacker_set_buffer(x->un, p, (size_t)n);
    *ok = 1;
    return p;
}

// reads blob arg 0 and optional path arg 1, leaves the unpacker at the target
// returns 1 found, 0 missing (result already set to NULL), -1 error (result set)
static int locate(sqlite3_context *c, int argc, sqlite3_value **argv, tb_ctx *x) {
    int ok;
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) { sqlite3_result_null(c); return 0; }
    blob_arg(c, argv[0], x, &ok);
    if (!ok) return -1;
    if (argc < 2) return 1;
    if (sqlite3_value_type(argv[1]) == SQLITE_NULL) { sqlite3_result_null(c); return 0; }
    step steps[MAX_STEPS];
    int n;
    if (!parse_path((const char *)sqlite3_value_text(argv[1]), steps, &n)) {
        sqlite3_result_error(c, "tinybits: bad path", -1);
        return -1;
    }
    if (!find(x->un, steps, n)) { sqlite3_result_null(c); return 0; }
    return 1;
}

static void fn_json(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    int r = locate(c, argc, argv, x);
    if (r <= 0) return;
    sbuf b = {0};
    int ok = emit(x->un, &b, 0);
    if (b.oom) { sqlite3_free(b.p); sqlite3_result_error_nomem(c); return; }
    if (!ok) { sqlite3_free(b.p); sqlite3_result_error(c, "tinybits: malformed blob", -1); return; }
    sqlite3_result_text64(c, b.p, b.n, sqlite3_free, SQLITE_UTF8);
}

static void fn_extract(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    int r = locate(c, argc, argv, x);
    if (r <= 0) return;
    tiny_bits_unpacker *u = x->un;
    size_t start = u->current_pos;
    tiny_bits_value v;
    switch (unpack_value(u, &v)) {
        case TINY_BITS_INT: sqlite3_result_int64(c, v.int_val); break;
        case TINY_BITS_DOUBLE: sqlite3_result_double(c, v.double_val); break;
        case TINY_BITS_DATETIME: sqlite3_result_double(c, v.datetime_val.unixtime); break;
        case TINY_BITS_STR:
            sqlite3_result_text64(c, v.str_blob_val.data, v.str_blob_val.length, SQLITE_TRANSIENT, SQLITE_UTF8);
            break;
        case TINY_BITS_BLOB:
            sqlite3_result_blob64(c, v.str_blob_val.data, v.str_blob_val.length, SQLITE_TRANSIENT);
            break;
        case TINY_BITS_TRUE: sqlite3_result_int(c, 1); break;
        case TINY_BITS_FALSE: sqlite3_result_int(c, 0); break;
        case TINY_BITS_NULL: sqlite3_result_null(c); break;
        case TINY_BITS_NAN: sqlite3_result_double(c, NAN); break;
        case TINY_BITS_INF: sqlite3_result_double(c, INFINITY); break;
        case TINY_BITS_N_INF: sqlite3_result_double(c, -INFINITY); break;
        case TINY_BITS_ARRAY:
        case TINY_BITS_MAP:
            u->current_pos = start; // only the header was read, no strings were registered
            tiny_bits_packer_reset(x->pk);
            if (!copy_value(u, x->pk, 0)) { sqlite3_result_error(c, "tinybits: malformed blob", -1); break; }
            sqlite3_result_blob64(c, tiny_bits_packer_data(x->pk), tiny_bits_packer_size(x->pk), SQLITE_TRANSIENT);
            break;
        default: sqlite3_result_error(c, "tinybits: malformed blob", -1);
    }
}

static void fn_type(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    int r = locate(c, argc, argv, x);
    if (r <= 0) return;
    tiny_bits_value v;
    const char *s;
    switch (unpack_value(x->un, &v)) {
        case TINY_BITS_INT: s = "integer"; break;
        case TINY_BITS_DOUBLE: case TINY_BITS_NAN: case TINY_BITS_INF: case TINY_BITS_N_INF: s = "real"; break;
        case TINY_BITS_DATETIME: s = "datetime"; break;
        case TINY_BITS_STR: s = "text"; break;
        case TINY_BITS_BLOB: s = "blob"; break;
        case TINY_BITS_TRUE: s = "true"; break;
        case TINY_BITS_FALSE: s = "false"; break;
        case TINY_BITS_NULL: s = "null"; break;
        case TINY_BITS_ARRAY: s = "array"; break;
        case TINY_BITS_MAP: s = "object"; break;
        default: sqlite3_result_error(c, "tinybits: malformed blob", -1); return;
    }
    sqlite3_result_text(c, s, -1, SQLITE_STATIC);
}

static void fn_array_length(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    int r = locate(c, argc, argv, x);
    if (r <= 0) return;
    tiny_bits_value v;
    enum tiny_bits_type t = unpack_value(x->un, &v);
    if (t == TINY_BITS_ARRAY) sqlite3_result_int64(c, (sqlite3_int64)v.length);
    else if (t == TINY_BITS_ERROR || t == TINY_BITS_FINISHED) sqlite3_result_error(c, "tinybits: malformed blob", -1);
    else sqlite3_result_int(c, 0);
}

static void fn_valid(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    (void)argc;
    if (sqlite3_value_type(argv[0]) != SQLITE_BLOB || sqlite3_value_bytes(argv[0]) < 1) {
        sqlite3_result_int(c, 0);
        return;
    }
    tiny_bits_unpacker_set_buffer(x->un, sqlite3_value_blob(argv[0]), (size_t)sqlite3_value_bytes(argv[0]));
    sqlite3_result_int(c, skip_value(x->un) && x->un->current_pos == x->un->size);
}

typedef struct {
    const char *p, *end;
    tiny_bits_packer *pk;
    sbuf scratch;
} jp;

static void ws(jp *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\t' || *j->p == '\r')) j->p++;
}

// number of top level elements in the container whose opening bracket was just consumed, -1 if unterminated
static long count_elems(const char *p, const char *end) {
    long commas = 0;
    int depth = 0, any = 0;
    for (; p < end; p++) {
        char c = *p;
        if (c == '"') {
            any = 1;
            for (p++; p < end && *p != '"'; p++) if (*p == '\\') p++;
            if (p >= end) return -1;
        } else if (c == '[' || c == '{') {
            depth++; any = 1;
        } else if (c == ']' || c == '}') {
            if (depth == 0) return any ? commas + 1 : 0;
            depth--;
        } else if (c == ',' && depth == 0) {
            commas++;
        } else if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
            any = 1;
        }
    }
    return -1;
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return 0;
    }
    *out = v;
    return 1;
}

static void put_utf8(sbuf *b, unsigned cp) {
    char t[4];
    if (cp < 0x80) { t[0] = cp; sb_put(b, t, 1); }
    else if (cp < 0x800) { t[0] = 0xC0 | (cp >> 6); t[1] = 0x80 | (cp & 63); sb_put(b, t, 2); }
    else if (cp < 0x10000) { t[0] = 0xE0 | (cp >> 12); t[1] = 0x80 | ((cp >> 6) & 63); t[2] = 0x80 | (cp & 63); sb_put(b, t, 3); }
    else { t[0] = 0xF0 | (cp >> 18); t[1] = 0x80 | ((cp >> 12) & 63); t[2] = 0x80 | ((cp >> 6) & 63); t[3] = 0x80 | (cp & 63); sb_put(b, t, 4); }
}

// parses a string at j->p (on the opening quote), points s/n at its decoded bytes
static int parse_string(jp *j, const char **s, size_t *n) {
    const char *p = j->p + 1, *q = p;
    while (q < j->end && *q != '"' && *q != '\\') q++;
    if (q >= j->end) return 0;
    if (*q == '"') {
        *s = p; *n = q - p;
        j->p = q + 1;
        return 1;
    }
    j->scratch.n = 0;
    p = j->p + 1;
    while (p < j->end && *p != '"') {
        if (*p != '\\') { sb_putc(&j->scratch, *p++); continue; }
        if (++p >= j->end) return 0;
        switch (*p) {
            case '"': case '\\': case '/': sb_putc(&j->scratch, *p); p++; break;
            case 'n': sb_putc(&j->scratch, '\n'); p++; break;
            case 'r': sb_putc(&j->scratch, '\r'); p++; break;
            case 't': sb_putc(&j->scratch, '\t'); p++; break;
            case 'b': sb_putc(&j->scratch, '\b'); p++; break;
            case 'f': sb_putc(&j->scratch, '\f'); p++; break;
            case 'u': {
                unsigned cp, lo;
                if (p + 5 > j->end || !hex4(p + 1, &cp)) return 0;
                p += 5;
                if (cp >= 0xD800 && cp < 0xDC00) {
                    if (p + 6 > j->end || p[0] != '\\' || p[1] != 'u' || !hex4(p + 2, &lo) || lo < 0xDC00 || lo > 0xDFFF) return 0;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return 0;
                }
                put_utf8(&j->scratch, cp);
                break;
            }
            default: return 0;
        }
    }
    if (p >= j->end || j->scratch.oom) return 0;
    *s = j->scratch.p ? j->scratch.p : "";
    *n = j->scratch.n;
    j->p = p + 1;
    return 1;
}

static int parse_value(jp *j, int depth);

static int parse_container(jp *j, int depth, int is_map) {
    char close = is_map ? '}' : ']';
    j->p++;
    long n = count_elems(j->p, j->end);
    if (n < 0) return 0;
    if (!(is_map ? pack_map(j->pk, (int)n) : pack_arr(j->pk, (int)n))) return 0;
    for (long i = 0; i < n; i++) {
        ws(j);
        if (is_map) {
            const char *s;
            size_t len;
            if (j->p >= j->end || *j->p != '"' || !parse_string(j, &s, &len)) return 0;
            if (!pack_str(j->pk, (char *)s, (uint32_t)len)) return 0;
            ws(j);
            if (j->p >= j->end || *j->p++ != ':') return 0;
        }
        if (!parse_value(j, depth + 1)) return 0;
        ws(j);
        if (i < n - 1 && (j->p >= j->end || *j->p++ != ',')) return 0;
    }
    ws(j);
    return j->p < j->end && *j->p++ == close;
}

static int parse_value(jp *j, int depth) {
    if (depth > MAX_DEPTH) return 0;
    ws(j);
    if (j->p >= j->end) return 0;
    char c = *j->p;
    if (c == '{') return parse_container(j, depth, 1);
    if (c == '[') return parse_container(j, depth, 0);
    if (c == '"') {
        const char *s;
        size_t len;
        return parse_string(j, &s, &len) && pack_str(j->pk, (char *)s, (uint32_t)len);
    }
    size_t left = j->end - j->p;
    if (left >= 4 && !memcmp(j->p, "true", 4)) { j->p += 4; return pack_true(j->pk); }
    if (left >= 5 && !memcmp(j->p, "false", 5)) { j->p += 5; return pack_false(j->pk); }
    if (left >= 4 && !memcmp(j->p, "null", 4)) { j->p += 4; return pack_null(j->pk); }
    char num[128];
    size_t n = 0;
    int is_float = 0;
    while (j->p + n < j->end && n < sizeof num - 1) {
        char d = j->p[n];
        if (d == '.' || d == 'e' || d == 'E') is_float = 1;
        else if (!((d >= '0' && d <= '9') || d == '-' || d == '+')) break;
        num[n++] = d;
    }
    if (!n) return 0;
    num[n] = 0;
    j->p += n;
    char *e;
    if (!is_float) {
        errno = 0;
        long long v = strtoll(num, &e, 10);
        if (*e == 0 && errno == 0) return pack_int(j->pk, v);
    }
    double d = strtod(num, &e);
    return *e == 0 && pack_double(j->pk, d);
}

static void fn_from_json(sqlite3_context *c, int argc, sqlite3_value **argv) {
    tb_ctx *x = sqlite3_user_data(c);
    (void)argc;
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) { sqlite3_result_null(c); return; }
    const char *s = (const char *)sqlite3_value_text(argv[0]);
    int len = sqlite3_value_bytes(argv[0]);
    jp j = {s, s + len, x->pk, {0}};
    tiny_bits_packer_reset(x->pk);
    int ok = parse_value(&j, 0);
    if (ok) { ws(&j); ok = j.p == j.end; }
    sqlite3_free(j.scratch.p);
    if (!ok) { sqlite3_result_error(c, "tinybits: malformed JSON", -1); return; }
    sqlite3_result_blob64(c, tiny_bits_packer_data(x->pk), tiny_bits_packer_size(x->pk), SQLITE_TRANSIENT);
}

/*
 * tb_group_encode(spec, nkeys, node, k1..k_nkeys, v1, ..., vN) builds one TinyBits value from flat rows,
 * one row per object: a row of node n is an object, nested under its parent row.
 * spec lists the nodes in preorder, ';' separated: "<parent> <kind> <name> <col>,<col>..."
 * (parent -1 for the root, kind m = list, o = one object or null). An object's keys are its
 * columns, then its child nodes in spec order. With nkeys > 0 rows come in any order: each
 * carries integer sort keys (NULL first) whose order is depth first, and tb_group_encode buffers and
 * sorts them. With nkeys 0 rows must already come in depth first order. tb_group_encode returns a list
 * of roots, tb_group_encode_one the first root or NULL. One packer writes everything, so strings dedupe
 * across the whole tree.
 */

#define TREE_MAX_NODES 64
#define TREE_MAX_DEPTH 64
#define TREE_MAX_KEYS 64
#define TREE_NOMEM ((const char *)1)

typedef struct {
    const char *s;
    size_t n;
} tree_str;

typedef struct {
    int parent, slot, nassoc, ncols;
    char kind;
    tree_str name;
    tree_str *cols;
} tree_node;

typedef struct {
    int node, next_slot, open; // open: slot whose list is being filled, -1 none
    size_t hdr;
    uint64_t count;
} tree_frame;

typedef struct {
    int node;
    size_t off; // its values in scratch
} tree_row;

typedef struct {
    int init, err, one, roots, nkeys;
    tb_ctx *x;
    tiny_bits_packer *pk;
    char *spec;   // one allocation: the nodes, the column names, then the spec text
    int nnodes;
    tree_node *nodes;
    int depth;
    tree_frame stack[TREE_MAX_DEPTH];
    size_t root_hdr;
    uint64_t root_count;
    // buffered rows (nkeys > 0)
    tree_row *rows;
    int64_t *keys;
    size_t nrows, cap;
    sbuf scratch;
} tree_state;

static int tree_parse(tree_state *s, const char *text, int len) {
    int nodes = 1, cols = 1;
    for (int i = 0; i < len; i++) {
        if (text[i] == ';') nodes++, cols++;
        else if (text[i] == ',') cols++;
    }
    if (nodes > TREE_MAX_NODES) return 0;
    size_t head = sizeof(tree_node) * nodes + sizeof(tree_str) * cols;
    s->spec = sqlite3_malloc64(head + len + 1);
    if (!s->spec) return 0;
    s->nodes = (tree_node *)s->spec;
    tree_str *col = (tree_str *)(s->spec + sizeof(tree_node) * nodes);
    char *p = s->spec + head;
    memcpy(p, text, len);
    p[len] = 0;
    int n = 0;
    while (*p) {
        tree_node *t = &s->nodes[n];
        char *e;
        t->parent = (int)strtol(p, &e, 10);
        if (e == p || *e != ' ' || t->parent >= n || t->parent < -1 || (n == 0) != (t->parent == -1)) return 0;
        p = e + 1;
        t->kind = *p++;
        if ((t->kind != 'm' && t->kind != 'o') || *p++ != ' ') return 0;
        t->name.s = p;
        while (*p && *p != ' ') p++;
        t->name.n = p - t->name.s;
        if (*p++ != ' ') return 0;
        t->cols = col;
        t->ncols = 0;
        t->nassoc = 0;
        while (*p && *p != ';') {
            col->s = p;
            while (*p && *p != ',' && *p != ';') p++;
            col->n = p - col->s;
            if (!col->n) return 0;
            col++;
            t->ncols++;
            if (*p == ',') p++;
        }
        if (*p == ';') p++;
        t->slot = t->parent >= 0 ? s->nodes[t->parent].nassoc++ : 0;
        n++;
    }
    s->nnodes = n;
    return n > 0;
}

// widens a 1 byte placeholder list header at hdr to its real size, shifting what follows
static int tree_patch(tiny_bits_packer *pk, size_t hdr, uint64_t count) {
    uint8_t len[9];
    int extra = count < TB_ARR_LEN ? 0 : encode_varint(count - TB_ARR_LEN, len); // varint_size is only an upper bound
    if (extra) {
        if (!tiny_bits_packer_ensure_capacity(pk, extra)) return 0;
        memmove(pk->buffer + hdr + 1 + extra, pk->buffer + hdr + 1, pk->current_pos - hdr - 1);
        pk->current_pos += extra;
        if (pk->features & TB_FEATURE_STRING_DEDUPE) {
            HashTable *t = &pk->encode_table;
            for (uint32_t i = t->cache_pos; i > 0 && t->cache[i - 1].offset > hdr; i--) t->cache[i - 1].offset += extra;
        }
        pk->buffer[hdr] = TB_ARR_TAG | TB_ARR_LEN;
        memcpy(pk->buffer + hdr + 1, len, extra);
    } else {
        pk->buffer[hdr] = TB_ARR_TAG | (uint8_t)count;
    }
    return 1;
}

static int tree_list_open(tiny_bits_packer *pk, size_t *hdr) {
    if (!tiny_bits_packer_ensure_capacity(pk, 1)) return 0;
    *hdr = pk->current_pos++;
    return 1;
}

static int tree_empty_slot(tree_state *s, const tree_node *t) {
    return pack_str(s->pk, (char *)t->name.s, (uint32_t)t->name.n) && (t->kind == 'm' ? pack_arr(s->pk, 0) : pack_null(s->pk));
}

// the node of the child in slot k of parent p
static const tree_node *tree_child(tree_state *s, int p, int k) {
    for (int i = p + 1; i < s->nnodes; i++) if (s->nodes[i].parent == p && s->nodes[i].slot == k) return &s->nodes[i];
    return NULL;
}

static int tree_close(tree_state *s) {
    tree_frame *f = &s->stack[--s->depth];
    if (f->open >= 0 && !tree_patch(s->pk, f->hdr, f->count)) return 0;
    const tree_node *t = &s->nodes[f->node];
    for (int k = f->next_slot; k < t->nassoc; k++) if (!tree_empty_slot(s, tree_child(s, f->node, k))) return 0;
    return 1;
}

static int tree_value(tiny_bits_packer *pk, sqlite3_value *v) {
    switch (sqlite3_value_type(v)) {
        case SQLITE_INTEGER: return pack_int(pk, sqlite3_value_int64(v));
        case SQLITE_FLOAT: return pack_double(pk, sqlite3_value_double(v));
        case SQLITE_TEXT: {
            const char *p = (const char *)sqlite3_value_text(v);
            return p && pack_str(pk, (char *)p, (uint32_t)sqlite3_value_bytes(v));
        }
        case SQLITE_BLOB: return pack_blob(pk, sqlite3_value_blob(v), sqlite3_value_bytes(v));
        default: return pack_null(pk);
    }
}

// buffered values: a type byte, then 8 bytes (integer, real) or a 4 byte length and the bytes
static void tree_save_value(sbuf *b, sqlite3_value *v) {
    char t = (char)sqlite3_value_type(v);
    sb_putc(b, t);
    switch (t) {
        case SQLITE_INTEGER: { sqlite3_int64 i = sqlite3_value_int64(v); sb_put(b, (char *)&i, 8); break; }
        case SQLITE_FLOAT: { double d = sqlite3_value_double(v); sb_put(b, (char *)&d, 8); break; }
        case SQLITE_TEXT: case SQLITE_BLOB: {
            const char *p = t == SQLITE_TEXT ? (const char *)sqlite3_value_text(v) : sqlite3_value_blob(v);
            uint32_t n = (uint32_t)sqlite3_value_bytes(v);
            sb_put(b, (char *)&n, 4);
            if (n) sb_put(b, p, n);
            break;
        }
    }
}

static int tree_load_value(tiny_bits_packer *pk, const char **at) {
    const char *p = *at;
    char t = *p++;
    int ok;
    switch (t) {
        case SQLITE_INTEGER: { int64_t i; memcpy(&i, p, 8); p += 8; ok = pack_int(pk, i); break; }
        case SQLITE_FLOAT: { double d; memcpy(&d, p, 8); p += 8; ok = pack_double(pk, d); break; }
        case SQLITE_TEXT: case SQLITE_BLOB: {
            uint32_t n;
            memcpy(&n, p, 4);
            p += 4;
            ok = t == SQLITE_TEXT ? pack_str(pk, (char *)p, n) : pack_blob(pk, p, (int)n);
            p += n;
            break;
        }
        default: ok = pack_null(pk);
    }
    *at = p;
    return ok;
}

// writes the object of one row of node n, its values from argv or (argv NULL) from scratch at off
static const char *tree_emit(tree_state *s, int n, sqlite3_value **argv, size_t off) {
    const tree_node *t = &s->nodes[n];
    if (t->parent < 0) {
        while (s->depth) if (!tree_close(s)) return TREE_NOMEM;
        if (s->one && s->roots) return "tb_group_encode_one: more than one root row";
        s->roots++;
        s->root_count++;
    } else {
        while (s->depth && s->stack[s->depth - 1].node != t->parent) if (!tree_close(s)) return TREE_NOMEM;
        if (!s->depth) return "tb_group_encode: row without its parent (check the sort keys)";
        tree_frame *f = &s->stack[s->depth - 1];
        if (f->open == t->slot) {
            f->count++;
        } else {
            if (f->open >= 0) {
                if (!tree_patch(s->pk, f->hdr, f->count)) return TREE_NOMEM;
                f->open = -1;
            }
            if (t->slot < f->next_slot) return "tb_group_encode: more than one row for a one object";
            for (int k = f->next_slot; k < t->slot; k++) if (!tree_empty_slot(s, tree_child(s, t->parent, k))) return TREE_NOMEM;
            if (!pack_str(s->pk, (char *)t->name.s, (uint32_t)t->name.n)) return TREE_NOMEM;
            if (t->kind == 'm') {
                if (!tree_list_open(s->pk, &f->hdr)) return TREE_NOMEM;
                f->open = t->slot;
                f->count = 1;
            }
            f->next_slot = t->slot + 1;
        }
    }
    if (s->depth == TREE_MAX_DEPTH) return "tb_group_encode: too deep";
    if (!pack_map(s->pk, t->ncols + t->nassoc)) return TREE_NOMEM;
    const char *at = s->scratch.p + off;
    for (int i = 0; i < t->ncols; i++) {
        if (!pack_str(s->pk, (char *)t->cols[i].s, (uint32_t)t->cols[i].n)) return TREE_NOMEM;
        if (!(argv ? tree_value(s->pk, argv[i]) : tree_load_value(s->pk, &at))) return TREE_NOMEM;
    }
    s->stack[s->depth++] = (tree_frame){n, 0, -1, 0, 0};
    return NULL;
}

static void tree_fail(sqlite3_context *c, tree_state *s, const char *msg) {
    s->err = 1;
    if (msg == TREE_NOMEM) sqlite3_result_error_nomem(c);
    else sqlite3_result_error(c, msg, -1);
}

static const char *tree_buffer(tree_state *s, int n, sqlite3_value **keys, sqlite3_value **vals) {
    if (s->nrows == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 256;
        tree_row *rows = sqlite3_realloc64(s->rows, cap * sizeof *rows);
        if (rows) s->rows = rows;
        int64_t *k = sqlite3_realloc64(s->keys, cap * s->nkeys * sizeof *k);
        if (k) s->keys = k;
        if (!rows || !k) return TREE_NOMEM;
        s->cap = cap;
    }
    int64_t *k = s->keys + s->nrows * s->nkeys;
    for (int i = 0; i < s->nkeys; i++) {
        switch (sqlite3_value_type(keys[i])) {
            case SQLITE_INTEGER: k[i] = sqlite3_value_int64(keys[i]); break;
            case SQLITE_NULL: k[i] = INT64_MIN; break;
            default: return "tb_group_encode: sort keys must be integers or NULL";
        }
    }
    s->rows[s->nrows] = (tree_row){n, s->scratch.n};
    for (int i = 0; i < s->nodes[n].ncols; i++) tree_save_value(&s->scratch, vals[i]);
    if (s->scratch.oom) return TREE_NOMEM;
    s->nrows++;
    return NULL;
}

static int tree_cmp(const tree_state *s, uint32_t a, uint32_t b) {
    const int64_t *x = s->keys + (size_t)a * s->nkeys, *y = s->keys + (size_t)b * s->nkeys;
    for (int i = 0; i < s->nkeys; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

// stable bottom up merge sort of row indexes by their keys; returns the sorted array
static uint32_t *tree_sort(const tree_state *s, uint32_t *a, uint32_t *tmp, size_t n) {
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            if (mid == hi || tree_cmp(s, a[mid - 1], a[mid]) <= 0) { // already in order: copy the run
                memcpy(tmp + lo, a + lo, (hi - lo) * sizeof *a);
                continue;
            }
            while (i < mid && j < hi) tmp[k++] = tree_cmp(s, a[j], a[i]) < 0 ? a[j++] : a[i++];
            while (i < mid) tmp[k++] = a[i++];
            while (j < hi) tmp[k++] = a[j++];
        }
        uint32_t *t = a; a = tmp; tmp = t;
    }
    return a;
}

static void tree_step(sqlite3_context *c, int argc, sqlite3_value **argv, int one) {
    tree_state *s = sqlite3_aggregate_context(c, sizeof(tree_state));
    if (!s) { sqlite3_result_error_nomem(c); return; }
    if (s->err) return;
    if (!s->init) {
        s->init = 1;
        s->one = one;
        s->x = sqlite3_user_data(c);
        if (argc < 3 || sqlite3_value_type(argv[0]) != SQLITE_TEXT
            || !tree_parse(s, (const char *)sqlite3_value_text(argv[0]), sqlite3_value_bytes(argv[0]))) {
            tree_fail(c, s, "tb_group_encode: bad spec");
            return;
        }
        s->nkeys = sqlite3_value_int(argv[1]);
        if (s->nkeys < 0 || s->nkeys > TREE_MAX_KEYS) { tree_fail(c, s, "tb_group_encode: bad key count"); return; }
        if (!s->x->tree_busy) {
            s->x->tree_busy = 1;
            s->pk = s->x->tree_pk;
            tiny_bits_packer_reset(s->pk);
        } else {
            s->pk = tiny_bits_packer_create(1024, TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS);
            if (!s->pk) { tree_fail(c, s, TREE_NOMEM); return; }
        }
        if (!one && !tree_list_open(s->pk, &s->root_hdr)) { tree_fail(c, s, TREE_NOMEM); return; }
    }
    int n = sqlite3_value_int(argv[2]);
    if (n < 0 || n >= s->nnodes) { tree_fail(c, s, "tb_group_encode: bad node"); return; }
    if (argc < 3 + s->nkeys + s->nodes[n].ncols) { tree_fail(c, s, "tb_group_encode: too few values"); return; }
    const char *err = s->nkeys ? tree_buffer(s, n, argv + 3, argv + 3 + s->nkeys) : tree_emit(s, n, argv + 3, 0);
    if (err) tree_fail(c, s, err);
}

static void tree_release(tree_state *s) {
    if (s->pk == s->x->tree_pk) {
        s->x->tree_busy = 0;
        if (s->pk->capacity > (1 << 20)) { // don't keep a big result's buffer around
            tiny_bits_packer *fresh = tiny_bits_packer_create(1024, TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS);
            if (fresh) { tiny_bits_packer_destroy(s->pk); s->x->tree_pk = fresh; }
        }
    } else if (s->pk) {
        tiny_bits_packer_destroy(s->pk);
    }
    sqlite3_free(s->spec);
    sqlite3_free(s->rows);
    sqlite3_free(s->keys);
    sqlite3_free(s->scratch.p);
}

static void tree_final(sqlite3_context *c, int one) {
    tree_state *s = sqlite3_aggregate_context(c, 0);
    if (!s || !s->init) {
        if (one) sqlite3_result_null(c);
        else sqlite3_result_blob(c, "\x08", 1, SQLITE_STATIC);
        return;
    }
    if (!s->err && s->nrows) {
        uint32_t *idx = sqlite3_malloc64(2 * s->nrows * sizeof *idx);
        if (!idx) {
            tree_fail(c, s, TREE_NOMEM);
        } else {
            for (size_t i = 0; i < s->nrows; i++) idx[i] = (uint32_t)i;
            uint32_t *order = tree_sort(s, idx, idx + s->nrows, s->nrows);
            for (size_t i = 0; i < s->nrows && !s->err; i++) {
                const tree_row *r = &s->rows[order[i]];
                const char *err = tree_emit(s, r->node, NULL, r->off);
                if (err) tree_fail(c, s, err);
            }
            sqlite3_free(idx);
        }
    }
    if (!s->err) {
        int ok = 1;
        while (ok && s->depth) ok = tree_close(s);
        if (ok && !one) ok = tree_patch(s->pk, s->root_hdr, s->root_count);
        if (!ok) sqlite3_result_error_nomem(c);
        else if (one && !s->roots) sqlite3_result_null(c);
        else sqlite3_result_blob64(c, tiny_bits_packer_data(s->pk), tiny_bits_packer_size(s->pk), SQLITE_TRANSIENT);
    }
    tree_release(s);
}

static void fn_tree_step(sqlite3_context *c, int argc, sqlite3_value **argv) { tree_step(c, argc, argv, 0); }
static void fn_tree_one_step(sqlite3_context *c, int argc, sqlite3_value **argv) { tree_step(c, argc, argv, 1); }
static void fn_tree_final(sqlite3_context *c) { tree_final(c, 0); }
static void fn_tree_one_final(sqlite3_context *c) { tree_final(c, 1); }

static void ctx_free(void *p) {
    tb_ctx *x = p;
    tiny_bits_unpacker_destroy(x->un);
    tiny_bits_packer_destroy(x->pk);
    tiny_bits_packer_destroy(x->tree_pk);
    sqlite3_free(x);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_tinybits_init(sqlite3 *db, char **err, const sqlite3_api_routines *api) {
    SQLITE_EXTENSION_INIT2(api);
    tb_ctx *x = sqlite3_malloc(sizeof *x);
    if (!x) return SQLITE_NOMEM;
    x->un = tiny_bits_unpacker_create();
    x->pk = tiny_bits_packer_create(1024, TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS);
    x->tree_pk = tiny_bits_packer_create(1024, TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS);
    x->tree_busy = 0;
    if (!x->un || !x->pk || !x->tree_pk) { ctx_free(x); return SQLITE_NOMEM; }
    int flags = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;
    static const struct { const char *name; int nargs; void (*fn)(sqlite3_context *, int, sqlite3_value **); } fns[] = {
        {"tb_json", 1, fn_json}, {"tb_json", 2, fn_json},
        {"tb_from_json", 1, fn_from_json},
        {"tb_extract", 2, fn_extract},
        {"tb_type", 1, fn_type}, {"tb_type", 2, fn_type},
        {"tb_array_length", 1, fn_array_length}, {"tb_array_length", 2, fn_array_length},
        {"tb_valid", 1, fn_valid},
    };
    int rc = SQLITE_OK;
    for (size_t i = 0; i < sizeof fns / sizeof *fns && rc == SQLITE_OK; i++) {
        rc = sqlite3_create_function_v2(db, fns[i].name, fns[i].nargs, flags, x, fns[i].fn, NULL, NULL,
                                        i == 0 ? ctx_free : NULL);
    }
    if (rc == SQLITE_OK)
        rc = sqlite3_create_function_v2(db, "tb_group_encode", -1, SQLITE_UTF8 | SQLITE_INNOCUOUS, x, NULL, fn_tree_step, fn_tree_final, NULL);
    if (rc == SQLITE_OK)
        rc = sqlite3_create_function_v2(db, "tb_group_encode_one", -1, SQLITE_UTF8 | SQLITE_INNOCUOUS, x, NULL, fn_tree_one_step, fn_tree_one_final, NULL);
    (void)err;
    return rc;
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_extension_init(sqlite3 *db, char **err, const sqlite3_api_routines *api) {
    return sqlite3_tinybits_init(db, err, api);
}
