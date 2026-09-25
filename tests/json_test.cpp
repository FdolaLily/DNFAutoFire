#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/json.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
bool rejects(const std::string& text, const dafclient::json::ParseOptions& options = {}) {
    try { dafclient::json::parse(text, options); return false; } catch (const dafclient::json::ParseError&) { return true; }
}
}
int main() {
    using namespace dafclient::json;
    try {
        const Value v = parse(u8"\xef\xbb\xbf{\"a\": [1, -2.5, 3e2, true, false, null], \"名\": \"值\\n\\u00e9\\ud83d\\ude00\", \"o\": {}}");
        check(v.isObject() && v.members().size() == 3, "object with BOM parsed");
        check(v.at(L"a").items().size() == 6 && v.at(L"a").items()[2].number(0) == 300, "array numbers parsed");
        check(v.at(L"名").str() == L"值\n\u00e9\U0001F600", "UTF-8 keys, escapes and surrogate pairs");
        check(v.at(L"missing").isNull() && v.at(L"a").at(L"x").isNull(), "missing members are null");
        check(v.at(L"a").items()[1].integer(9, 0, 10) == 0 && Value(12.9).integer(0, 0, 100) == 12, "integer clamps and truncates");
        check(Value(L"x").integer(7, 0, 10) == 7 && Value(5).boolean(true), "mistyped reads use fallback");
        const Value again = parse(serialize(v));
        check(again == v, "pretty round trip");
        check(parse(serialize(v, false)) == v, "compact round trip");
        check(serialize(Value::strings({L"X", L"Z"})) == "[\"X\", \"Z\"]\r\n", "short scalar arrays stay inline");
        Value order = Value::object(); order.set(L"z", 1); order.set(L"a", 2); order.set(L"z", 3);
        check(order.members()[0].key == L"z" && order.members()[0].value.number(0) == 3 && order.members().size() == 2, "insertion order and in-place update");
        check(order.erase(L"z") && !order.find(L"z"), "erase member");
        check(serialize(Value(L"\x01\"\\")) == "\"\\u0001\\\"\\\\\"\r\n", "control and quote escaping");
        check(serialize(Value(1234567.0)) == "1234567\r\n" && serialize(Value(0.5)) == "0.5\r\n", "number formatting");
        check(rejects("{\"a\":1,}") && rejects("[1,]") && rejects("{a:1}") && rejects("[01]") && rejects("\"\\x\""), "strict syntax");
        check(rejects("{\"a\":1,\"a\":2}"), "duplicate member rejected");
        check(rejects("\"\\ud800\"") && rejects("\"\xff\"") && rejects("\"a\nb\""), "invalid strings rejected");
        check(rejects(std::string(100, '[') + std::string(100, ']')), "nesting depth bounded");
        check(rejects("{} x") && rejects(""), "trailing content and empty input rejected");
        ParseOptions tolerant; tolerant.comments = true; tolerant.trailingCommas = true;
        const Value relaxed = parse("// head\n{\"a\": [1, 2,], /* inline */ \"b\": 2,}\n", tolerant);
        check(relaxed.at(L"a").items().size() == 2 && relaxed.at(L"b").number(0) == 2, ".NET-style comments and trailing commas");
        try { parse("{\n  \"a\": tru\n}"); check(false, "error expected"); }
        catch (const ParseError& e) { check(e.line == 2 && e.column == 8, "error position reported"); }
        check(widen(narrow(L"混合 ASCII")) == L"混合 ASCII", "UTF-8 conversion round trip");
        std::cout << "PASS: " << checks << " JSON checks.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
