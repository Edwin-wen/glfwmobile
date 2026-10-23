#include "scene.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "jsmn.h"

namespace skc {
namespace {

thread_local std::string g_last_error;

/** DFS 先序存储：一棵子树的 token 数 */
int TokenSpan(const jsmntok_t *tokens, int index) {
    const jsmntok_t &token = tokens[index];
    if (token.type != JSMN_OBJECT && token.type != JSMN_ARRAY) {
        return 1;
    }
    int cursor = index + 1;
    // jsmn 的 size 语义：object 是键值对数量，array 是元素数量
    if (token.type == JSMN_OBJECT) {
        for (int pair = 0; pair < token.size; ++pair) {
            cursor += 1;                         // key 一定是单个 string token
            cursor += TokenSpan(tokens, cursor); // value 可能是子树
        }
    } else {
        for (int i = 0; i < token.size; ++i) {
            cursor += TokenSpan(tokens, cursor);
        }
    }
    return cursor - index;
}

/** 遍历 object 的键值对，回调 (key token 下标, value token 下标) */
template <typename Fn>
void ForEachMember(const jsmntok_t *tokens, int object_index, Fn fn) {
    const jsmntok_t &object = tokens[object_index];
    if (object.type != JSMN_OBJECT) {
        return;
    }
    int cursor = object_index + 1;
    for (int pair = 0; pair < object.size; ++pair) {
        if (tokens[cursor].type != JSMN_STRING) {
            return;
        }
        const int value_index = cursor + 1;
        fn(cursor, value_index);
        cursor = value_index + TokenSpan(tokens, value_index);
    }
}

bool TokenEquals(const char *json, const jsmntok_t &token, const char *text) {
    if (token.type != JSMN_STRING) {
        return false;
    }
    const size_t length = static_cast<size_t>(token.end - token.start);
    return std::strlen(text) == length && std::memcmp(json + token.start, text, length) == 0;
}

bool TokenToFloat(const char *json, const jsmntok_t &token, float *out) {
    if (token.type != JSMN_PRIMITIVE) {
        return false;
    }
    const size_t length = static_cast<size_t>(token.end - token.start);
    if (length == 0 || length >= 32) {
        return false;
    }
    char buffer[32];
    std::memcpy(buffer, json + token.start, length);
    buffer[length] = '\0';
    char *end = nullptr;
    const float value = std::strtof(buffer, &end);
    if (end == buffer || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

bool TokenToString(const char *json, const jsmntok_t &token, std::string *out) {
    if (token.type != JSMN_STRING) {
        return false;
    }
    out->assign(json + token.start, static_cast<size_t>(token.end - token.start));
    return true;
}

struct OpFields {
    bool has_op = false;
    bool has_color = false;
    bool ok = true;
    std::string name;
    Color color = 0xFFFF0000;
    float x = 0, y = 0, w = 0, h = 0;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, width = 1;
    bool has_x = false, has_y = false, has_w = false, has_h = false;
    bool has_x1 = false, has_y1 = false, has_x2 = false, has_y2 = false;
};

void ReadFloat(const char *json, const char *key, const jsmntok_t &key_token,
               const jsmntok_t &value, float *slot, bool *present, OpFields *fields) {
    if (!TokenEquals(json, key_token, key)) {
        return;
    }
    *present = true;
    if (!TokenToFloat(json, value, slot)) {
        fields->ok = false;
    }
}

bool ParseOp(const char *json, const jsmntok_t *tokens, int index, Op *out) {
    if (tokens[index].type != JSMN_OBJECT) {
        return false;
    }
    OpFields fields;
    ForEachMember(tokens, index, [&](int key_index, int value_index) {
        const jsmntok_t &key = tokens[key_index];
        const jsmntok_t &value = tokens[value_index];
        if (TokenEquals(json, key, "op")) {
            if (TokenToString(json, value, &fields.name)) {
                fields.has_op = true;
            } else {
                fields.ok = false;
            }
            return;
        }
        if (TokenEquals(json, key, "color")) {
            std::string text;
            if (TokenToString(json, value, &text) &&
                ParseColor(text.c_str(), text.size(), &fields.color)) {
                fields.has_color = true;
            } else {
                fields.ok = false;
            }
            return;
        }
        ReadFloat(json, "x", key, value, &fields.x, &fields.has_x, &fields);
        ReadFloat(json, "y", key, value, &fields.y, &fields.has_y, &fields);
        ReadFloat(json, "w", key, value, &fields.w, &fields.has_w, &fields);
        ReadFloat(json, "h", key, value, &fields.h, &fields.has_h, &fields);
        ReadFloat(json, "x1", key, value, &fields.x1, &fields.has_x1, &fields);
        ReadFloat(json, "y1", key, value, &fields.y1, &fields.has_y1, &fields);
        ReadFloat(json, "x2", key, value, &fields.x2, &fields.has_x2, &fields);
        ReadFloat(json, "y2", key, value, &fields.y2, &fields.has_y2, &fields);
        if (TokenEquals(json, key, "width") && !TokenToFloat(json, value, &fields.width)) {
            fields.ok = false;
        }
    });

    if (!fields.ok || !fields.has_op) {
        return false;
    }

    Op op;
    op.color = fields.color;
    if (fields.name == "clear") {
        op.type = OpType::kClear;
    } else if (fields.name == "rect") {
        if (!fields.has_x || !fields.has_y || !fields.has_w || !fields.has_h) {
            return false;
        }
        op.type = OpType::kFillRect;
        op.rect = {fields.x, fields.y, fields.w, fields.h};
    } else if (fields.name == "line") {
        if (!fields.has_x1 || !fields.has_y1 || !fields.has_x2 || !fields.has_y2) {
            return false;
        }
        op.type = OpType::kStrokeLine;
        op.line = {fields.x1, fields.y1, fields.x2, fields.y2, fields.width};
    } else {
        return false;
    }
    *out = op;
    return true;
}

} // namespace

bool ParseColor(const char *text, size_t length, Color *out) {
    if (text == nullptr || length == 0) {
        return false;
    }
    if (text[0] == '#') {
        ++text;
        --length;
    }
    if (length != 3 && length != 6 && length != 8) {
        return false;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<uint32_t>(c - 'a') + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<uint32_t>(c - 'A') + 10;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }

    if (length == 3) {
        // #RGB -> #FFRRGGBB，每个分量重复一次（#F00 -> 0xFF）
        const uint32_t r = (value >> 8) & 0xF;
        const uint32_t g = (value >> 4) & 0xF;
        const uint32_t b = value & 0xF;
        *out = 0xFF000000u | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    } else if (length == 6) {
        *out = 0xFF000000u | value;
    } else {
        *out = value;
    }
    return true;
}

bool ParseScene(const char *json, Scene *out) {
    g_last_error.clear();
    if (json == nullptr || *json == '\0' || out == nullptr) {
        g_last_error = "empty scene";
        return false;
    }

    const size_t json_length = std::strlen(json);
    std::vector<jsmntok_t> tokens(128);
    jsmn_parser parser;
    int result = 0;
    // token 数量随 scene 规模增长，最多重试到 64K 个 token
    while (tokens.size() <= 65536) {
        jsmn_init(&parser);
        result = jsmn_parse(&parser, json, json_length, tokens.data(),
                            static_cast<unsigned int>(tokens.size()));
        if (result != JSMN_ERROR_NOMEM) {
            break;
        }
        tokens.resize(tokens.size() * 2);
    }
    if (result < 0) {
        g_last_error = result == JSMN_ERROR_NOMEM ? "scene too large" : "invalid json";
        return false;
    }
    if (result == 0 || tokens[0].type != JSMN_OBJECT) {
        g_last_error = "root must be an object";
        return false;
    }

    Scene parsed;
    bool found_ops = false;
    bool valid = true;
    ForEachMember(tokens.data(), 0, [&](int key_index, int value_index) {
        if (!valid || !TokenEquals(json, tokens[key_index], "ops")) {
            return;
        }
        const jsmntok_t &ops = tokens[value_index];
        if (ops.type != JSMN_ARRAY) {
            valid = false;
            g_last_error = "ops must be an array";
            return;
        }
        found_ops = true;
        int op_index = value_index + 1;
        for (int i = 0; i < ops.size && valid; ++i) {
            Op op;
            if (!ParseOp(json, tokens.data(), op_index, &op)) {
                valid = false;
                g_last_error = "invalid op[" + std::to_string(i) + "]";
                return;
            }
            parsed.ops.push_back(op);
            op_index += TokenSpan(tokens.data(), op_index);
        }
    });

    if (!valid) {
        return false;
    }
    if (!found_ops) {
        g_last_error = "missing ops";
        return false;
    }
    *out = std::move(parsed);
    return true;
}

const char *LastSceneError() {
    return g_last_error.c_str();
}

} // namespace skc