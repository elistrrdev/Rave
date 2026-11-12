#pragma once
#include <string>
#include <vector>

namespace json
{
    struct Value
    {
        enum class Type { Null, Bool, Number, String, Array, Object };

        Type type = Type::Null;
        bool boolean = false;
        double number = 0;
        std::string string;
        std::vector<Value> items;
        std::vector<std::string> keys;

        const Value& operator[](const std::string& key) const;
        const Value& operator[](size_t index) const;
        const std::string& str() const;
        bool isString() const { return type == Type::String; }
        bool isObject() const { return type == Type::Object; }
    };

    bool parse(const std::string& text, Value& out);
}
