// Leptid — minimal JSON value, parser and writer (RFC 8259 subset, UTF-8).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace leptid {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0;
    std::string stringValue;
    std::vector<Json> arrayValue;
    std::vector<std::pair<std::string, Json>> objectValue; // ordered to keep save files stable

    Json() {}
    static Json null() { return Json(); }
    static Json of(bool b) { Json j; j.type = Type::Bool; j.boolValue = b; return j; }
    static Json of(double d) { Json j; j.type = Type::Number; j.numberValue = d; return j; }
    static Json of(int i) { return of((double)i); }
    static Json of(const std::string& s) { Json j; j.type = Type::String; j.stringValue = s; return j; }
    static Json array() { Json j; j.type = Type::Array; return j; }
    static Json object() { Json j; j.type = Type::Object; return j; }

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool() const { return type == Type::Bool; }

    // Object access
    Json& set(const std::string& key, Json v);
    const Json* get(const std::string& key) const;
    // Typed getters with defaults (never throw)
    double num(const std::string& key, double def) const;
    double num(const std::string& key) const { return num(key, 0.0); }
    int    integer(const std::string& key, int def) const;
    int    integer(const std::string& key) const { return integer(key, 0); }
    bool   boolean(const std::string& key, bool def) const;
    std::string str(const std::string& key, const std::string& def) const;
    std::string str(const std::string& key) const { return str(key, ""); }
    bool has(const std::string& key) const { return get(key) != nullptr; }

    void push(Json v) { type = Type::Array; arrayValue.push_back(std::move(v)); }

    std::string dump(int indent = 0) const;   // 0 = compact
    static bool parse(const std::string& text, Json& out);

private:
    static std::string escape(const std::string& s);
    void dumpTo(std::string& out, int indent, int depth) const;
};

} // namespace leptid
