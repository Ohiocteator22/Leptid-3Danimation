// Leptid — JSON implementation.
#include "formats/json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace leptid {

Json& Json::set(const std::string& key, Json v) {
    type = Type::Object;
    for (auto& kv : objectValue)
        if (kv.first == key) { kv.second = std::move(v); return *this; }
    objectValue.emplace_back(key, std::move(v));
    return *this;
}

const Json* Json::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (auto& kv : objectValue)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

double Json::num(const std::string& key, double def) const {
    const Json* j = get(key);
    return (j && j->type == Type::Number) ? j->numberValue : def;
}
int Json::integer(const std::string& key, int def) const {
    const Json* j = get(key);
    return (j && j->type == Type::Number) ? (int)llround(j->numberValue) : def;
}
bool Json::boolean(const std::string& key, bool def) const {
    const Json* j = get(key);
    if (!j) return def;
    if (j->type == Type::Bool) return j->boolValue;
    if (j->type == Type::Number) return j->numberValue != 0;
    return def;
}
std::string Json::str(const std::string& key, const std::string& def) const {
    const Json* j = get(key);
    return (j && j->type == Type::String) ? j->stringValue : def;
}

std::string Json::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    return out;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    auto pad = [&](int d) {
        if (indent <= 0) return;
        out += '\n';
        out.append((size_t)(indent * d), ' ');
    };
    switch (type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += boolValue ? "true" : "false"; break;
        case Type::Number: {
            if (std::isfinite(numberValue)) {
                char b[40];
                if (numberValue == (double)(long long)numberValue && std::fabs(numberValue) < 1e15)
                    snprintf(b, sizeof b, "%lld", (long long)numberValue);
                else snprintf(b, sizeof b, "%.10g", numberValue);
                out += b;
            } else out += "null";
            break;
        }
        case Type::String: out += '"'; out += escape(stringValue); out += '"'; break;
        case Type::Array: {
            out += '[';
            for (size_t i = 0; i < arrayValue.size(); ++i) {
                if (i) out += ',';
                pad(depth + 1);
                arrayValue[i].dumpTo(out, indent, depth + 1);
            }
            if (!arrayValue.empty()) pad(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            for (size_t i = 0; i < objectValue.size(); ++i) {
                if (i) out += ',';
                pad(depth + 1);
                out += '"'; out += escape(objectValue[i].first); out += indent > 0 ? "\": " : "\":";
                objectValue[i].second.dumpTo(out, indent, depth + 1);
            }
            if (!objectValue.empty()) pad(depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

// ---------------- parser ----------------
namespace {

struct Parser {
    const char* p;
    const char* end;
    bool ok = true;

    void skipWs() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool fail() { ok = false; return false; }

    bool parseValue(Json& out, int depth) {
        if (depth > 64) return fail();
        skipWs();
        if (p >= end) return fail();
        char c = *p;
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') { out.type = Json::Type::String; return parseString(out.stringValue); }
        if (c == 't') { return lit("true", out, true); }
        if (c == 'f') { return lit("false", out, false); }
        if (c == 'n') {
            if (end - p >= 4 && p[0] == 'n' && p[1] == 'u' && p[2] == 'l' && p[3] == 'l') {
                p += 4; out = Json::null(); return true;
            }
            return fail();
        }
        return parseNumber(out);
    }
    bool lit(const char* word, Json& out, bool v) {
        size_t n = strlen(word);
        if ((size_t)(end - p) < n || strncmp(p, word, n) != 0) return fail();
        p += n;
        out.type = Json::Type::Bool; out.boolValue = v;
        return true;
    }
    bool parseNumber(Json& out) {
        char* endp = nullptr;
        double v = strtod(p, &endp);
        if (endp == p) return fail();
        p = endp;
        out.type = Json::Type::Number; out.numberValue = v;
        return true;
    }
    bool parseHex4(unsigned& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end) return fail();
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return fail();
        }
        return true;
    }
    void utf8Append(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool parseString(std::string& out) {
        if (p >= end || *p != '"') return fail();
        ++p;
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= end) return fail();
                char e = *p++;
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned h;
                        if (!parseHex4(h)) return fail();
                        unsigned cp = h;
                        if (h >= 0xD800 && h <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo;
                            if (!parseHex4(lo)) return fail();
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((h - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8Append(out, cp);
                        break;
                    }
                    default: return fail();
                }
            } else {
                out += c;
            }
        }
        return fail();
    }
    bool parseArray(Json& out, int depth) {
        ++p; // [
        out = Json::array();
        skipWs();
        if (p < end && *p == ']') { ++p; return true; }
        while (true) {
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.arrayValue.push_back(std::move(v));
            skipWs();
            if (p >= end) return fail();
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return fail();
        }
    }
    bool parseObject(Json& out, int depth) {
        ++p; // {
        out = Json::object();
        skipWs();
        if (p < end && *p == '}') { ++p; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') return fail();
            ++p;
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.objectValue.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (p >= end) return fail();
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return fail();
        }
    }
};

} // namespace

bool Json::parse(const std::string& text, Json& out) {
    Parser ps{text.data(), text.data() + text.size()};
    if (!ps.parseValue(out, 0)) return false;
    ps.skipWs();
    return ps.ok && ps.p >= ps.end;
}

} // namespace leptid
