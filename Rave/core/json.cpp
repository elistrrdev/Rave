#include "json.hpp"
#include <cstdlib>
#include <cstring>

namespace
{
    const json::Value& nullValue()
    {
        static const json::Value v;
        return v;
    }

    struct Parser
    {
        const char* p;
        const char* end;
        int depth = 0;

        void ws()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                ++p;
        }

        bool literal(const char* word)
        {
            size_t n = strlen(word);
            if ((size_t)(end - p) < n || memcmp(p, word, n) != 0)
                return false;
            p += n;
            return true;
        }

        static void utf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80)
                out += (char)cp;
            else if (cp < 0x800)
            {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        bool hex4(unsigned& out)
        {
            if (end - p < 4)
                return false;
            out = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = *p++;
                out <<= 4;
                if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
                else return false;
            }
            return true;
        }

        bool string(std::string& out)
        {
            if (p >= end || *p != '"')
                return false;
            ++p;
            while (p < end)
            {
                char c = *p++;
                if (c == '"')
                    return true;
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (p >= end)
                    return false;
                char e = *p++;
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned cp;
                    if (!hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                    {
                        p += 2;
                        unsigned lo;
                        if (!hex4(lo))
                            return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default:
                    return false;
                }
            }
            return false;
        }

        bool value(json::Value& v)
        {
            if (++depth > 64)
                return false;
            ws();
            if (p >= end)
                return false;

            bool ok = false;
            char c = *p;
            if (c == '{')
            {
                v.type = json::Value::Type::Object;
                ++p;
                ws();
                if (p < end && *p == '}')
                {
                    ++p;
                    ok = true;
                }
                else
                {
                    for (;;)
                    {
                        ws();
                        std::string key;
                        if (!string(key))
                            break;
                        ws();
                        if (p >= end || *p != ':')
                            break;
                        ++p;
                        json::Value child;
                        if (!value(child))
                            break;
                        v.keys.push_back(std::move(key));
                        v.items.push_back(std::move(child));
                        ws();
                        if (p < end && *p == ',')
                        {
                            ++p;
                            continue;
                        }
                        if (p < end && *p == '}')
                        {
                            ++p;
                            ok = true;
                        }
                        break;
                    }
                }
            }
            else if (c == '[')
            {
                v.type = json::Value::Type::Array;
                ++p;
                ws();
                if (p < end && *p == ']')
                {
                    ++p;
                    ok = true;
                }
                else
                {
                    for (;;)
                    {
                        json::Value child;
                        if (!value(child))
                            break;
                        v.items.push_back(std::move(child));
                        ws();
                        if (p < end && *p == ',')
                        {
                            ++p;
                            continue;
                        }
                        if (p < end && *p == ']')
                        {
                            ++p;
                            ok = true;
                        }
                        break;
                    }
                }
            }
            else if (c == '"')
            {
                v.type = json::Value::Type::String;
                ok = string(v.string);
            }
            else if (c == 't' || c == 'f')
            {
                v.type = json::Value::Type::Bool;
                v.boolean = c == 't';
                ok = literal(c == 't' ? "true" : "false");
            }
            else if (c == 'n')
            {
                ok = literal("null");
            }
            else
            {
                std::string num;
                while (p < end && (*p == '+' || *p == '-' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9')))
                    num += *p++;
                if (!num.empty())
                {
                    char* stop = nullptr;
                    v.type = json::Value::Type::Number;
                    v.number = strtod(num.c_str(), &stop);
                    ok = stop && *stop == '\0';
                }
            }
            --depth;
            return ok;
        }
    };
}

const json::Value& json::Value::operator[](const std::string& key) const
{
    if (type != Type::Object)
        return nullValue();
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
            return items[i];
    return nullValue();
}

const json::Value& json::Value::operator[](size_t index) const
{
    if (type != Type::Array || index >= items.size())
        return nullValue();
    return items[index];
}

const std::string& json::Value::str() const
{
    return string;
}

bool json::parse(const std::string& text, Value& out)
{
    out = {};
    Parser parser{ text.data(), text.data() + text.size() };
    if (!parser.value(out))
        return false;
    parser.ws();
    return parser.p == parser.end;
}
