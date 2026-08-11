/**
 * json.hpp —— 极简 JSON 解析/序列化（单头文件，无外部依赖）
 * ================================================================
 * 仅覆盖 Aerofly Link DLL 所需的 JSON 子集：
 *   - 对象 {key: value}
 *   - 字符串、整数、浮点、布尔
 *   - 嵌套对象
 *
 * 不依赖任何第三方库，保证 DLL 可用 MSVC 直接编译。
 */
#pragma once

#include <string>
#include <string_view>
#include <map>
#include <variant>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cmath>

namespace json {

// ── JSON 值类型 ──────────────────────────────────────────────────
class Value;
using Object = std::map<std::string, Value>;
using Null = std::monostate;

class Value {
public:
    using Variant = std::variant<Null, bool, double, std::string, Object>;

    Value() : data_(Null{}) {}
    Value(bool b) : data_(b) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(int64_t i) : data_(static_cast<double>(i)) {}
    Value(double d) : data_(d) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Object o) : data_(std::move(o)) {}

    bool is_null()    const { return std::holds_alternative<Null>(data_); }
    bool is_bool()    const { return std::holds_alternative<bool>(data_); }
    bool is_number()  const { return std::holds_alternative<double>(data_); }
    bool is_string()  const { return std::holds_alternative<std::string>(data_); }
    bool is_object()  const { return std::holds_alternative<Object>(data_); }

    bool         as_bool()   const { return std::get<bool>(data_); }
    double       as_number() const { return std::get<double>(data_); }
    int          as_int()    const { return static_cast<int>(std::get<double>(data_)); }
    std::string  as_string() const { return std::get<std::string>(data_); }
    const Object& as_object() const { return std::get<Object>(data_); }

    // 对象下标访问（不存在则返回 null Value）
    const Value& operator[](std::string_view key) const {
        static const Value null_val;
        if (!is_object()) return null_val;
        auto it = as_object().find(std::string(key));
        return (it != as_object().end()) ? it->second : null_val;
    }

    // ── 序列化 ──
    std::string dump() const {
        std::ostringstream ss;
        write(ss);
        return ss.str();
    }

    void write(std::ostringstream& ss) const {
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Null>) {
                ss << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                ss << (v ? "true" : "false");
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::floor(v) == v && std::abs(v) < 1e15) {
                    ss << static_cast<int64_t>(v);  // 整数输出无小数点
                } else {
                    ss << v;
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                ss << '"';
                for (char c : v) {
                    switch (c) {
                        case '"':  ss << "\\\""; break;
                        case '\\': ss << "\\\\"; break;
                        case '\n': ss << "\\n";  break;
                        case '\r': ss << "\\r";  break;
                        case '\t': ss << "\\t";  break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                ss << "\\u" << std::hex << static_cast<int>(c);
                            } else {
                                ss << c;
                            }
                    }
                }
                ss << '"';
            } else if constexpr (std::is_same_v<T, Object>) {
                ss << '{';
                bool first = true;
                for (const auto& [k, val] : v) {
                    if (!first) ss << ',';
                    first = false;
                    ss << '"' << k << "\":";
                    val.write(ss);
                }
                ss << '}';
            }
        }, data_);
    }

private:
    Variant data_;
};

// ── JSON 解析器 ──────────────────────────────────────────────────

class Parser {
public:
    Parser(std::string_view text) : text_(text), pos_(0) {}

    Value parse() {
        skip_ws();
        return parse_value();
    }

private:
    std::string_view text_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    char peek() {
        if (pos_ >= text_.size())
            throw std::runtime_error("JSON: unexpected end of input");
        return text_[pos_];
    }

    char get() {
        char c = peek();
        ++pos_;
        return c;
    }

    Value parse_value() {
        skip_ws();
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '"') return Value(parse_string());
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') { parse_null(); return Value(); }
        return parse_number();
    }

    Value parse_object() {
        Object obj;
        get();  // consume '{'
        skip_ws();
        if (peek() == '}') { get(); return Value(std::move(obj)); }

        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (get() != ':')
                throw std::runtime_error("JSON: expected ':' in object");
            obj[key] = parse_value();
            skip_ws();
            char c = get();
            if (c == ',') continue;
            if (c == '}') break;
            throw std::runtime_error("JSON: expected ',' or '}' in object");
        }
        return Value(std::move(obj));
    }

    std::string parse_string() {
        if (get() != '"')
            throw std::runtime_error("JSON: expected '\"'");
        std::string s;
        while (true) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        // 简单处理 \uXXXX（仅 BMP）
                        std::string hex(text_.substr(pos_, 4));
                        pos_ += 4;
                        unsigned int code = std::stoul(hex, nullptr, 16);
                        if (code < 0x80) {
                            s += static_cast<char>(code);
                        } else if (code < 0x800) {
                            s += static_cast<char>(0xC0 | (code >> 6));
                            s += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            s += static_cast<char>(0xE0 | (code >> 12));
                            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            s += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: s += esc;
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    Value parse_bool() {
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return Value(true);
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return Value(false);
        }
        throw std::runtime_error("JSON: invalid literal");
    }

    void parse_null() {
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4;
        } else {
            throw std::runtime_error("JSON: invalid literal");
        }
    }

    Value parse_number() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
                || c == '+' || c == '-')
                ++pos_;
            else
                break;
        }
        std::string num_str(text_.substr(start, pos_ - start));
        return Value(std::stod(num_str));
    }
};

/// 便捷解析函数
inline Value parse(std::string_view text) {
    Parser p(text);
    return p.parse();
}

}  // namespace json
