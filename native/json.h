#pragma once
// Minimal, dependency-free JSON document model used by the unified config.json,
// the one-time legacy migration and the DNF launcher checks. Strings are held as
// UTF-16 std::wstring (the Win32 native form); files are read and written as UTF-8.
// Objects keep insertion order so hand-edited files stay readable and diffs stay small.
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace dafclient::json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Member;

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : type_(Type::Bool), bool_(value) {}
    Value(int value) : type_(Type::Number), number_(value) {}
    Value(unsigned value) : type_(Type::Number), number_(value) {}
    Value(long long value) : type_(Type::Number), number_(double(value)) {}
    Value(double value) : type_(Type::Number), number_(value) {}
    Value(const wchar_t* value) : type_(Type::String), string_(value ? value : L"") {}
    Value(std::wstring value) : type_(Type::String), string_(std::move(value)) {}
    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }
    static Value strings(const std::vector<std::wstring>& items);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Typed reads never throw: a missing or mistyped value yields the fallback.
    bool boolean(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }
    double number(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
    // Integer read clamped to [low, high]; non-integral numbers are rounded toward zero.
    unsigned integer(unsigned fallback, unsigned low, unsigned high) const;
    std::wstring string(const std::wstring& fallback = L"") const { return type_ == Type::String ? string_ : fallback; }
    const std::wstring& str() const { return string_; }
    std::vector<std::wstring> stringList() const; // Non-string items are skipped.

    std::vector<Value>& items() { return array_; }
    const std::vector<Value>& items() const { return array_; }
    void push(Value value);

    std::vector<Member>& members() { return object_; }
    const std::vector<Member>& members() const { return object_; }
    const Value* find(const std::wstring& key) const;
    Value* find(const std::wstring& key);
    // Missing or null member of a const object: a shared null value.
    const Value& at(const std::wstring& key) const;
    // Inserts a null member (turning a null value into an object) when absent.
    Value& operator[](const std::wstring& key);
    void set(const std::wstring& key, Value value) { (*this)[key] = std::move(value); }
    bool erase(const std::wstring& key);

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::wstring string_;
    std::vector<Value> array_;
    std::vector<Member> object_;
};

struct Member {
    std::wstring key;
    Value value;
};

struct ParseOptions {
    bool comments = false;       // Accept // and /* */ (appsettings.json written for .NET).
    bool trailingCommas = false; // Accept [1,2,] and {"a":1,}.
};

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& what, size_t line, size_t column)
        : std::runtime_error(what), line(line), column(column) {}
    size_t line, column;
};

// Parses UTF-8 text (an optional BOM is skipped). Throws ParseError with a 1-based position.
Value parse(const std::string& utf8, const ParseOptions& options = {});
// Pretty output uses two-space indentation and CRLF; short scalar arrays stay on one line.
std::string serialize(const Value& value, bool pretty = true);

std::wstring widen(const std::string& utf8);  // Throws on invalid UTF-8.
std::string narrow(const std::wstring& text);

} // namespace dafclient::json
