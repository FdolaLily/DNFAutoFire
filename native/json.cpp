#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dafclient::json {
namespace {
const Value& nullValue() { static const Value value; return value; }

class Parser {
public:
    Parser(const std::string& text, const ParseOptions& options) : s_(text), options_(options) {
        if (s_.compare(0, 3, "\xef\xbb\xbf") == 0) pos_ = 3;
    }
    Value document() {
        Value value = parseValue(0);
        skip();
        if (pos_ != s_.size()) fail("unexpected trailing characters");
        return value;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') { ++line; column = 1; }
            else if ((static_cast<unsigned char>(s_[i]) & 0xc0) != 0x80) ++column;
        }
        char message[160];
        std::snprintf(message, sizeof(message), "JSON error at line %zu, column %zu: %s", line, column, what);
        throw ParseError(message, line, column);
    }
    bool atEnd() const { return pos_ >= s_.size(); }
    char peek() const { return atEnd() ? '\0' : s_[pos_]; }
    void skip() {
        for (;;) {
            while (!atEnd() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) ++pos_;
            if (!options_.comments || peek() != '/' || pos_ + 1 >= s_.size()) return;
            if (s_[pos_ + 1] == '/') {
                while (!atEnd() && s_[pos_] != '\n') ++pos_;
            } else if (s_[pos_ + 1] == '*') {
                const size_t end = s_.find("*/", pos_ + 2);
                if (end == std::string::npos) { pos_ = s_.size(); fail("unterminated comment"); }
                pos_ = end + 2;
            } else return;
        }
    }
    void expect(char c, const char* what) { skip(); if (peek() != c) fail(what); ++pos_; }
    bool literal(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n; return true;
    }
    Value parseValue(int depth) {
        if (depth > 64) fail("nesting is too deep");
        skip();
        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': return Value(parseString());
        case 't': if (literal("true")) return Value(true); break;
        case 'f': if (literal("false")) return Value(false); break;
        case 'n': if (literal("null")) return Value(); break;
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parseNumber();
        }
        fail(atEnd() ? "unexpected end of text" : "unexpected character");
    }
    Value parseObject(int depth) {
        ++pos_;
        Value object = Value::object();
        skip();
        if (peek() == '}') { ++pos_; return object; }
        for (;;) {
            skip();
            if (peek() != '"') fail("expected a quoted member name");
            std::wstring key = parseString();
            expect(':', "expected ':' after member name");
            Value value = parseValue(depth + 1);
            if (object.find(key)) fail("duplicate member name");
            object.members().push_back({std::move(key), std::move(value)});
            skip();
            if (peek() == ',') {
                ++pos_; skip();
                if (options_.trailingCommas && peek() == '}') { ++pos_; return object; }
                continue;
            }
            if (peek() == '}') { ++pos_; return object; }
            fail("expected ',' or '}'");
        }
    }
    Value parseArray(int depth) {
        ++pos_;
        Value array = Value::array();
        skip();
        if (peek() == ']') { ++pos_; return array; }
        for (;;) {
            array.items().push_back(parseValue(depth + 1));
            skip();
            if (peek() == ',') {
                ++pos_; skip();
                if (options_.trailingCommas && peek() == ']') { ++pos_; return array; }
                continue;
            }
            if (peek() == ']') { ++pos_; return array; }
            fail("expected ',' or ']'");
        }
    }
    unsigned hex4() {
        if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
            else fail("invalid \\u escape");
        }
        return value;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 0x3f)); }
        else if (cp < 0x10000) { out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
        else { out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3f)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
    }
    std::wstring parseString() {
        ++pos_;
        std::string bytes;
        for (;;) {
            if (atEnd()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { bytes += c; continue; }
            if (atEnd()) fail("unterminated escape");
            switch (s_[pos_++]) {
            case '"': bytes += '"'; break;
            case '\\': bytes += '\\'; break;
            case '/': bytes += '/'; break;
            case 'b': bytes += '\b'; break;
            case 'f': bytes += '\f'; break;
            case 'n': bytes += '\n'; break;
            case 'r': bytes += '\r'; break;
            case 't': bytes += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (pos_ + 2 > s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') fail("unpaired surrogate");
                    pos_ += 2;
                    const unsigned low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) fail("unpaired surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unpaired surrogate");
                appendUtf8(bytes, cp);
                break;
            }
            default: fail("invalid escape");
            }
        }
        try { return widen(bytes); } catch (...) { fail("invalid UTF-8 in string"); }
    }
    Value parseNumber() {
        const size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') ++pos_;
        else if (peek() >= '1' && peek() <= '9') while (peek() >= '0' && peek() <= '9') ++pos_;
        else fail("invalid number");
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string text = s_.substr(start, pos_ - start);
        const double value = std::strtod(text.c_str(), nullptr);
        if (!std::isfinite(value)) fail("number out of range");
        return Value(value);
    }

    const std::string& s_;
    ParseOptions options_;
    size_t pos_ = 0;
};

void writeString(std::string& out, const std::wstring& text) {
    out += '"';
    const std::string bytes = narrow(text);
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (u < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof(buffer), "\\u%04x", u); out += buffer; }
            else out += c;
        }
    }
    out += '"';
}
void writeNumber(std::string& out, double value) {
    char buffer[40];
    if (std::fabs(value) < 9007199254740992.0 && value == std::floor(value)) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    else std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}
bool scalar(const Value& v) { return !v.isArray() && !v.isObject(); }
void write(std::string& out, const Value& value, bool pretty, int indent) {
    const auto newline = [&](int level) { if (pretty) { out += "\r\n"; out.append(size_t(level) * 2, ' '); } };
    switch (value.type()) {
    case Type::Null: out += "null"; return;
    case Type::Bool: out += value.boolean(false) ? "true" : "false"; return;
    case Type::Number: writeNumber(out, value.number(0)); return;
    case Type::String: writeString(out, value.str()); return;
    case Type::Array: {
        const auto& items = value.items();
        if (items.empty()) { out += "[]"; return; }
        bool inlineArray = !pretty;
        if (pretty) {
            // Short lists of keys or names read best on one line.
            size_t length = 0; inlineArray = true;
            for (const auto& item : items) { if (!scalar(item)) { inlineArray = false; break; } length += item.isString() ? item.str().size() + 4 : 8; }
            if (length > 100) inlineArray = false;
        }
        out += '[';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += inlineArray && pretty ? ", " : ",";
            if (!inlineArray) newline(indent + 1);
            write(out, items[i], pretty, indent + 1);
        }
        if (!inlineArray) newline(indent);
        out += ']';
        return;
    }
    case Type::Object: {
        const auto& members = value.members();
        if (members.empty()) { out += "{}"; return; }
        // Small records of scalars (a combo step, four direction keys) stay on one line.
        bool inlineObject = pretty && indent > 0;
        if (inlineObject) {
            size_t length = 0;
            for (const auto& m : members) {
                if (!scalar(m.value)) { inlineObject = false; break; }
                length += m.key.size() + (m.value.isString() ? m.value.str().size() : 6) + 8;
            }
            if (length > 64) inlineObject = false;
        }
        out += inlineObject ? "{ " : "{";
        for (size_t i = 0; i < members.size(); ++i) {
            if (i) out += inlineObject ? ", " : ",";
            if (!inlineObject) newline(indent + 1);
            writeString(out, members[i].key);
            out += pretty ? ": " : ":";
            write(out, members[i].value, pretty, indent + 1);
        }
        if (inlineObject) out += ' ';
        else newline(indent);
        out += '}';
        return;
    }
    }
}
} // namespace

Value Value::strings(const std::vector<std::wstring>& items) {
    Value result = array();
    for (const auto& item : items) result.array_.emplace_back(item);
    return result;
}
unsigned Value::integer(unsigned fallback, unsigned low, unsigned high) const {
    if (type_ != Type::Number || !std::isfinite(number_)) return fallback;
    const double n = std::trunc(number_);
    if (n < double(low)) return low;
    if (n > double(high)) return high;
    return unsigned(n);
}
std::vector<std::wstring> Value::stringList() const {
    std::vector<std::wstring> result;
    if (type_ == Type::Array) for (const auto& item : array_) if (item.isString()) result.push_back(item.str());
    return result;
}
void Value::push(Value value) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) throw std::logic_error("JSON value is not an array");
    array_.push_back(std::move(value));
}
const Value* Value::find(const std::wstring& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_) if (member.key == key) return &member.value;
    return nullptr;
}
Value* Value::find(const std::wstring& key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& member : object_) if (member.key == key) return &member.value;
    return nullptr;
}
const Value& Value::at(const std::wstring& key) const {
    const Value* found = find(key);
    return found ? *found : nullValue();
}
Value& Value::operator[](const std::wstring& key) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (type_ != Type::Object) throw std::logic_error("JSON value is not an object");
    if (Value* found = find(key)) return *found;
    object_.push_back({key, Value()});
    return object_.back().value;
}
bool Value::erase(const std::wstring& key) {
    for (auto it = object_.begin(); it != object_.end(); ++it)
        if (it->key == key) { object_.erase(it); return true; }
    return false;
}
bool Value::operator==(const Value& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == other.bool_;
    case Type::Number: return number_ == other.number_;
    case Type::String: return string_ == other.string_;
    case Type::Array: return array_ == other.array_;
    case Type::Object:
        if (object_.size() != other.object_.size()) return false;
        for (size_t i = 0; i < object_.size(); ++i)
            if (object_[i].key != other.object_[i].key || object_[i].value != other.object_[i].value) return false;
        return true;
    }
    return false;
}

Value parse(const std::string& utf8, const ParseOptions& options) { return Parser(utf8, options).document(); }
std::string serialize(const Value& value, bool pretty) {
    std::string out;
    write(out, value, pretty, 0);
    if (pretty) out += "\r\n";
    return out;
}
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("invalid UTF-8");
    std::wstring text(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), text.data(), count);
    return text;
}
std::string narrow(const std::wstring& text) {
    if (text.empty()) return "";
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("cannot encode UTF-8");
    std::string bytes(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), count, nullptr, nullptr);
    return bytes;
}

} // namespace dafclient::json
