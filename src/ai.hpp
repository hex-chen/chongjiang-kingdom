// 冲奖王国 —— AI 机器人发言 (Claude API, 通过系统自带的 curl 调用, 零依赖)
// 设置环境变量 ANTHROPIC_API_KEY 后自动启用; 未设置则游戏退回本地固定台词。
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#define AI_POPEN _popen
#define AI_PCLOSE _pclose
#else
#define AI_POPEN popen
#define AI_PCLOSE pclose
#endif

inline const char *aiKey() {
    const char *k = getenv("ANTHROPIC_API_KEY");
    return (k && *k) ? k : nullptr;
}
inline bool aiAvailable() { return aiKey() != nullptr; }

// JSON 字符串转义
inline std::string aiJsonEscape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else o += (char)c;
        }
    }
    return o;
}

// 从响应 JSON 中提取第一个 "text":"..." 字段(含转义解码)
inline std::string aiExtractText(const std::string &body) {
    if (body.find("\"type\":\"error\"") != std::string::npos) return "";
    size_t p = body.find("\"text\":\"");
    if (p == std::string::npos) return "";
    p += 8;
    std::string out;
    auto hex4 = [&](size_t i) -> unsigned {
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            char c = body[i + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        }
        return v;
    };
    auto pushUtf8 = [&](unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    };
    while (p < body.size()) {
        char c = body[p];
        if (c == '"') break;
        if (c == '\\' && p + 1 < body.size()) {
            char e = body[p + 1];
            p += 2;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'u':
                    if (p + 4 <= body.size()) {
                        unsigned cp = hex4(p);
                        p += 4;
                        // 代理对 (emoji 等)
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= body.size() &&
                            body[p] == '\\' && body[p + 1] == 'u') {
                            unsigned lo = hex4(p + 2);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p += 6;
                            }
                        }
                        pushUtf8(cp);
                    }
                    break;
                default: break;
            }
            continue;
        }
        out += c;
        p++;
    }
    return out;
}

// 调用 Claude Messages API, 返回回复文本; 失败返回 ""
// 用最便宜快速的 claude-haiku-4-5, 短中文台词足够
inline std::string aiChat(const std::string &systemPrompt, const std::string &userPrompt,
                          int seq) {
    const char *key = aiKey();
    if (!key) return "";
    std::string body =
        "{\"model\":\"claude-haiku-4-5\",\"max_tokens\":150,"
        "\"system\":\"" + aiJsonEscape(systemPrompt) + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"" +
        aiJsonEscape(userPrompt) + "\"}]}";

    std::string tmp = "ai_req_" + std::to_string(seq) + ".json";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return "";
        f << body;
    }
    std::string cmd =
        "curl -s --max-time 15 https://api.anthropic.com/v1/messages"
        " -H \"content-type: application/json\""
        " -H \"x-api-key: " + std::string(key) + "\""
        " -H \"anthropic-version: 2023-06-01\""
        " -d @" + tmp;
    std::string resp;
    FILE *pipe = AI_POPEN(cmd.c_str(), "r");
    if (pipe) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) resp.append(buf, n);
        AI_PCLOSE(pipe);
    }
    remove(tmp.c_str());
    return aiExtractText(resp);
}
