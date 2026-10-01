// main/common/gb_score_key.c —— 见 gb_score_key.h。
#include "gb_score_key.h"

bool gb_game_id_valid(const char *id) {
    if (!id || !id[0]) return false;
    size_t len = 0;
    for (const char *p = id; *p; p++, len++) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok || len >= GB_GAME_ID_MAX) return false;
    }
    return true;
}

bool gb_store_make_key(char prefix, const char *id, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (prefix < 'a' || prefix > 'z') return false;
    if (out_size < GB_SCORE_KEY_LEN || !gb_game_id_valid(id)) return false;
    out[0] = prefix;
    out[1] = '_';
    size_t i = 0;
    for (; id[i]; i++) out[2 + i] = id[i];
    out[2 + i] = '\0';
    return true;
}

bool gb_score_make_key(const char *id, char *out, size_t out_size) {
    return gb_store_make_key('b', id, out, out_size);
}
