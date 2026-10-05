#pragma once

#include "utf8.hpp"

#include <charconv>
#include <map>

namespace outloud::detail::voice_json {

// Bounded Safetensors header parser. Kept private to the importer; no runtime JSON dependency.
struct Value {
    enum class Kind { object, array, string, number, literal } kind = Kind::literal;
    std::map<std::string, Value> object;
    std::vector<Value> array;
    std::string text;
};
class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}
    Value parse() {
        if (!valid_utf8(input_)) { fail("Invalid UTF-8 in Safetensors header"); }
        auto result = value(0); whitespace();
        if (at_ != input_.size()) { fail("Trailing JSON data"); }
        return result;
    }
private:
    [[noreturn]] static void fail(const char* message) { throw std::invalid_argument(message); }
    void whitespace() {
        while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\t' ||
               input_[at_] == '\n' || input_[at_] == '\r')) { ++at_; }
    }
    bool take(char ch) {
        whitespace();
        if (at_ < input_.size() && input_[at_] == ch) { ++at_; return true; }
        return false;
    }
    void expect(char ch) { if (!take(ch)) { fail("Malformed JSON delimiter"); } }
    unsigned hex4() {
        unsigned result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (at_ == input_.size()) { fail("Truncated JSON Unicode escape"); }
            const char ch = input_[at_++];
            unsigned digit = 0;
            if (ch >= '0' && ch <= '9') { digit = static_cast<unsigned>(ch - '0'); }
            else if (ch >= 'a' && ch <= 'f') { digit = static_cast<unsigned>(ch - 'a') + 10; }
            else if (ch >= 'A' && ch <= 'F') { digit = static_cast<unsigned>(ch - 'A') + 10; }
            else { fail("Invalid JSON Unicode escape"); }
            result = result * 16 + digit;
        }
        return result;
    }
    std::string string() {
        expect('"'); std::string result;
        while (at_ < input_.size()) {
            const auto ch = static_cast<unsigned char>(input_[at_++]);
            if (ch == '"') { return result; }
            if (ch < 32) { fail("Unescaped JSON control character"); }
            if (ch != '\\') { result += static_cast<char>(ch); continue; }
            if (at_ == input_.size()) { fail("Truncated JSON escape"); }
            switch (input_[at_++]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                auto cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (at_ + 2 > input_.size() || input_.substr(at_, 2) != "\\u") { fail("Unpaired JSON surrogate"); }
                    at_ += 2; const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) { fail("Unpaired JSON surrogate"); }
                    cp = 0x10000 + (cp - 0xd800) * 1024 + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) { fail("Unpaired JSON surrogate"); }
                result += from_scalar(cp); break;
            }
            default: fail("Invalid JSON escape");
            }
        }
        fail("Unterminated JSON string");
    }
    bool digit() const { return at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9'; }
    void digits() {
        if (!digit()) { fail("Invalid JSON number"); }
        do { ++at_; } while (digit());
    }
    Value value(unsigned depth) {
        if (depth > 8 || ++nodes_ > 1024) { fail("Safetensors JSON nesting/member limit exceeded"); }
        whitespace(); if (at_ == input_.size()) { fail("Truncated JSON value"); }
        Value result;
        if (take('{')) {
            result.kind = Value::Kind::object;
            if (take('}')) { return result; }
            do {
                auto key = string(); expect(':'); auto child = value(depth + 1);
                if (!result.object.emplace(std::move(key), std::move(child)).second) { fail("Duplicate JSON key"); }
                if (result.object.size() > 128) { fail("Safetensors JSON object limit exceeded"); }
            } while (take(','));
            expect('}'); return result;
        }
        if (take('[')) {
            result.kind = Value::Kind::array;
            if (take(']')) { return result; }
            do {
                result.array.push_back(value(depth + 1));
                if (result.array.size() > 32) { fail("Safetensors JSON array limit exceeded"); }
            } while (take(','));
            expect(']'); return result;
        }
        if (input_[at_] == '"') { result.kind = Value::Kind::string; result.text = string(); return result; }
        for (auto literal : {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
            if (input_.substr(at_, literal.size()) == literal) {
                result.text = literal; at_ += literal.size(); return result;
            }
        }
        const auto start = at_; result.kind = Value::Kind::number;
        (void)take('-');
        // No whitespace is permitted inside a number.
        if (at_ < input_.size() && input_[at_] == '0') { ++at_; }
        else { digits(); }
        if (at_ < input_.size() && input_[at_] == '.') { ++at_; digits(); }
        if (at_ < input_.size() && (input_[at_] == 'e' || input_[at_] == 'E')) {
            ++at_;
            if (at_ < input_.size() && (input_[at_] == '+' || input_[at_] == '-')) { ++at_; }
            digits();
        }
        result.text = input_.substr(start, at_ - start); return result;
    }
    std::string_view input_;
    std::size_t at_ = 0, nodes_ = 0;
};

inline bool unsigned_integer(const Value& value, std::uint64_t& result) {
    if (value.kind != Value::Kind::number) { return false; }
    const auto begin = value.text.data(), end = begin + value.text.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc() && parsed.ptr == end;
}
} // namespace outloud::detail::voice_json
