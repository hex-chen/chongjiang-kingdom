// 冲奖王国 —— AI 机器人发言 (通过系统自带的 curl 调用, 零第三方依赖)
//
// 支持两种后端, 按优先级:
//   1. Ollama  — 设置 OLLAMA_MODEL (如 qwen3:8b / gemma4) 即启用, 完全本地部署,
//                默认连 http://127.0.0.1:11434, 可用 OLLAMA_HOST 覆盖
//   2. Claude  — 设置 ANTHROPIC_API_KEY 即启用 (claude-haiku-4-5)
// 都没设置或调用失败时, 游戏自动退回本地固定台词。
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

inline const char *aiEnv(const char *name) {
    const char *v = getenv(name);
    return (v && *v) ? v : nullptr;
}
inline const char *aiOllamaModel() { return aiEnv("OLLAMA_MODEL"); }
inline const char *aiClaudeKey() { return aiEnv("ANTHROPIC_API_KEY"); }
inline bool aiAvailable() { return aiOllamaModel() || aiClaudeKey(); }

inline std::string aiOllamaHost() {
    const char *h = aiEnv("OLLAMA_HOST");
    std::string s = h ? h : "http://127.0.0.1:11434";
    if (s.find("://") == std::string::npos) s = "http://" + s;
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

inline std::string aiBackendDesc() {
    if (aiOllamaModel())
        return "Ollama 本地模型 " + std::string(aiOllamaModel()) + " @ " + aiOllamaHost();
    if (aiClaudeKey()) return "Claude API (claude-haiku-4-5)";
    return "本地台词 (设置 OLLAMA_MODEL 或 ANTHROPIC_API_KEY 可启用AI发言)";
}

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

// 从 body[p] (开引号之后) 开始解码一个 JSON 字符串(含转义)
inline std::string aiDecodeJsonStringAt(const std::string &body, size_t p) {
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

// 在 anchor 之后找 "key":"..." 并解码
inline std::string aiExtractField(const std::string &body, const std::string &anchor,
                                  const std::string &key) {
    size_t base = anchor.empty() ? 0 : body.find(anchor);
    if (base == std::string::npos) return "";
    std::string pat = "\"" + key + "\":\"";
    size_t p = body.find(pat, base);
    if (p == std::string::npos) {
        pat = "\"" + key + "\": \"";  // 容忍冒号后有空格
        p = body.find(pat, base);
        if (p == std::string::npos) return "";
    }
    return aiDecodeJsonStringAt(body, p + pat.size());
}

// 清理输出: 去掉 <think>...</think> 推理块、首尾空白和包裹引号
inline std::string aiCleanup(std::string s) {
    for (;;) {
        size_t a = s.find("<think>");
        if (a == std::string::npos) break;
        size_t b = s.find("</think>", a);
        if (b == std::string::npos) { s.erase(a); break; }
        s.erase(a, b + 8 - a);
    }
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == '\n' || s[i] == ' ' || s[i] == '\r')) i++;
    s.erase(0, i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

// 执行 curl 命令, stdin 无输入, 返回 stdout
inline std::string aiRunCurl(const std::string &cmd) {
    std::string resp;
    FILE *pipe = AI_POPEN(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) resp.append(buf, n);
    AI_PCLOSE(pipe);
    return resp;
}

// 发送一次请求 (把 body 写入临时文件, curl 读取), 返回响应原文
inline std::string aiPost(const std::string &url, const std::string &headers,
                          const std::string &body, int seq, int timeoutSec) {
    std::string tmp = "ai_req_" + std::to_string(seq) + ".json";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return "";
        f << body;
    }
    std::string cmd = "curl -s --max-time " + std::to_string(timeoutSec) + " " + url +
                      " -H \"content-type: application/json\"" + headers + " -d @" + tmp;
    std::string resp = aiRunCurl(cmd);
    remove(tmp.c_str());
    return resp;
}

// 调用 AI, 返回一句台词; 失败返回 ""
inline std::string aiChat(const std::string &systemPrompt, const std::string &userPrompt,
                          int seq) {
    if (!aiAvailable()) return "";
    if (aiOllamaModel()) {
        // Ollama 原生 /api/chat, 非流式。思考型模型 (qwen3/gemma4等) 会把预算全部
        // 花在 thinking 上导致 content 为空, 所以先带 "think":false 请求;
        // 若模型不支持该参数而报错, 去掉后重试一次。
        auto makeBody = [&](bool withThink) {
            return std::string("{\"model\":\"") + aiJsonEscape(aiOllamaModel()) +
                   "\",\"stream\":false," + (withThink ? "\"think\":false," : "") +
                   "\"options\":{\"num_predict\":300},"
                   "\"messages\":["
                   "{\"role\":\"system\",\"content\":\"" + aiJsonEscape(systemPrompt) + "\"},"
                   "{\"role\":\"user\",\"content\":\"" + aiJsonEscape(userPrompt) + "\"}]}";
        };
        std::string url = aiOllamaHost() + "/api/chat";
        std::string resp = aiPost(url, "", makeBody(true), seq, 90);
        if (resp.find("\"error\"") != std::string::npos &&
            resp.find("\"message\"") == std::string::npos)
            resp = aiPost(url, "", makeBody(false), seq, 90);
        return aiCleanup(aiExtractField(resp, "\"message\"", "content"));
    }
    // Claude API
    std::string body =
        "{\"model\":\"claude-haiku-4-5\",\"max_tokens\":150,"
        "\"system\":\"" + aiJsonEscape(systemPrompt) + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"" +
        aiJsonEscape(userPrompt) + "\"}]}";
    std::string headers =
        " -H \"x-api-key: " + std::string(aiClaudeKey()) + "\""
        " -H \"anthropic-version: 2023-06-01\"";
    std::string resp = aiPost("https://api.anthropic.com/v1/messages", headers, body, seq, 15);
    if (resp.find("\"type\":\"error\"") != std::string::npos) return "";
    return aiCleanup(aiExtractField(resp, "", "text"));
}
