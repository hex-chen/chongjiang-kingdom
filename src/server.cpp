// ============================================================
//  冲奖王国 · 局域网多人推理游戏 —— 服务端（法官）
//  用法:
//    ./server [端口] [--bots N]     开房间, 房主输入 start 开局(不足人数用机器人补齐)
//    ./server --selftest N          N 个机器人自动跑完一整局(用于测试)
//  平台: macOS / Linux (POSIX socket)
// ============================================================
#include "common.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <ifaddrs.h>
#include <signal.h>

using std::string;
using std::vector;
using namespace std::chrono;

// ---------------- 角色与阵营 ----------------
enum Team { GOOD = 0, BAD = 1, THIRD = 2 };

enum Role {
    // 好人阵营
    R_CITIZEN, R_DETECTIVE, R_PHARM, R_USURPER, R_GUNNER, R_OYSTER, R_IMMORTAL,
    R_PUNISHER, R_JOKER, R_SPECULATOR, R_SONIC, R_BOMBER, R_CORRECTOR, R_DECODER,
    // 坏人阵营
    R_DISMANTLER, R_EATER, R_CURSER, R_DORM, R_LOVER, R_STEALER, R_RIPPER,
    R_THUNDER, R_SPACE, R_MELON,
    // 第三方
    R_LYINGH, R_LURKER, R_PUPPETEER
};

static const char *roleName(Role r) {
    switch (r) {
        case R_CITIZEN:    return "冲奖王国居民";
        case R_DETECTIVE:  return "推理大师";
        case R_PHARM:      return "药师";
        case R_USURPER:    return "夺权者";
        case R_GUNNER:     return "炮手";
        case R_OYSTER:     return "千年生蚝精";
        case R_IMMORTAL:   return "不灭之神";
        case R_PUNISHER:   return "天罚者";
        case R_JOKER:      return "Joker";
        case R_SPECULATOR: return "投机者";
        case R_SONIC:      return "音波释放者";
        case R_BOMBER:     return "爆破者";
        case R_CORRECTOR:  return "修正者";
        case R_DECODER:    return "破译者";
        case R_DISMANTLER: return "人体拆卸者";
        case R_EATER:      return "电脑吞食者";
        case R_CURSER:     return "天谴者";
        case R_DORM:       return "宿管老师";
        case R_LOVER:      return "相思者";
        case R_STEALER:    return "窃取者";
        case R_RIPPER:     return "撕裂者";
        case R_THUNDER:    return "雷电法王";
        case R_SPACE:      return "空间拆卸者";
        case R_MELON:      return "千年大瓜球";
        case R_LYINGH:     return "躺尸氢";
        case R_LURKER:     return "潜伏者";
        case R_PUPPETEER:  return "木偶师";
    }
    return "?";
}

static Team roleTeam(Role r) {
    if (r <= R_DECODER) return GOOD;
    if (r <= R_MELON) return BAD;
    return THIRD;
}

static const char *teamName(Team t) {
    return t == GOOD ? "好人阵营" : t == BAD ? "坏人阵营" : "第三方";
}

static const char *roleDesc(Role r) {
    switch (r) {
        case R_CITIZEN:    return "无技能, 靠推理和投票取胜";
        case R_DETECTIVE:  return "每晚推理一人身份(好人/坏人/第三方)";
        case R_PHARM:      return "一瓶毒药可杀人, 一瓶解药可救人";
        case R_USURPER:    return "第一夜选人: 好人则复制其技能, 否则堕落为人体拆卸者";
        case R_GUNNER:     return "一次机会, 白天发言前开炮杀人, 误杀好人则同死";
        case R_OYSTER:     return "坚硬的壳可挡一次夜晚物理伤害(自己无察觉)";
        case R_IMMORTAL:   return "两张长存符, 夜晚首先睁眼选择是否使用, 用后当晚+次日白天除放逐外不死";
        case R_PUNISHER:   return "第三夜自动死亡, 当晚可看到所有坏人/第三方并杀死一人, 无遗言";
        case R_JOKER:      return "无技能, 但被推理大师推理时显示为坏人";
        case R_SPECULATOR: return "好人占比>50%时可跳槽坏人阵营, 变为人体拆卸者";
        case R_SONIC:      return "一次机会(非第一夜)释放音波, 当晚坏人和第三方不可睁眼";
        case R_BOMBER:     return "一次机会, 白天发言前选两名玩家同归于尽";
        case R_CORRECTOR:  return "一次机会: 选一人, 若为好人则自己死, 否则对方明牌并变为居民";
        case R_DECODER:    return "一次机会: 投票后使本次投票无效, 自己重新选一人放逐";
        case R_DISMANTLER: return "每晚拆卸(杀死)一名玩家";
        case R_EATER:      return "投票后可自暴: 自己死亡, 直接杀死一人并立刻进入黑夜";
        case R_CURSER:     return "第一夜选一名好人, 使其获得Joker效果(被推理显示坏人)";
        case R_DORM:       return "一次机会(非第一夜)释放威压, 当晚好人不可睁眼";
        case R_LOVER:      return "第一夜绑定一人, 自己死亡时对方陪葬";
        case R_STEALER:    return "一次机会: 夜晚选一人, 将其变为无技能的居民";
        case R_RIPPER:     return "一次机会: 用小说切断电脑, 当天无法投票";
        case R_THUNDER:    return "一次机会: 白天发动, 当日投票给自己的居民全部死亡";
        case R_SPACE:      return "一次机会: 夜晚选择相邻两人一起杀死";
        case R_MELON:      return "一次机会: 有人被放逐后用瓜皮罩住其角色卡, 使其无法明牌";
        case R_LYINGH:     return "每晚选两人: 直接杀死一人, 另一人被标记, 下晚死亡";
        case R_LURKER:     return "坏人全灭后解锁每晚杀人; 被推理时永远显示好人";
        case R_PUPPETEER:  return "每晚杀一人; 第一晚选一名木偶, 自己死时木偶继承成为木偶师";
    }
    return "";
}

// ---------------- 玩家 ----------------
struct Player {
    int id = 0;                  // 座位号, 1 起
    string name;
    int fd = -1;
    bool bot = false;
    std::atomic<bool> connected{false};
    Role role = R_CITIZEN;
    bool alive = true;
    // 状态
    bool shellIntact = true;     // 生蚝壳
    int charms = 2;              // 长存符
    bool charmed = false;        // 长存符生效中(当晚+次日白天)
    bool hasSave = true, hasPoison = true; // 药师
    bool cursedJoker = false;    // Joker本体或被天谴
    int loverTarget = -1;        // 相思者绑定
    int puppet = -1;             // 木偶师的木偶
    bool marked = false;         // 被躺尸氢标记, 下晚死
    bool usedOneShot = false;    // 各一次性主动技能
    // 通信
    std::mutex mx;
    std::condition_variable cv;
    std::deque<string> inbox;
    bool awaiting = false;
};

// ---------------- 游戏主体 ----------------
struct GameOver { int winner; }; // 0好人 1坏人 2第三方 3平局

struct Game {
    vector<std::unique_ptr<Player>> ps; // 含大厅内所有连接
    vector<Player *> seat;              // seat[1..n] 开局后的座位表
    std::mutex sendMx;
    std::mt19937 rng{std::random_device{}()};
    std::atomic<bool> started{false};
    std::atomic<bool> startRequested{false};
    int nightNo = 0, dayNo = 0;
    std::map<int, bool> dying;          // 今晚将死: id -> 是否可被解药救
    vector<int> nightDead;              // 今晚实际死亡(含连锁)
    bool sonicTonight = false, pressureTonight = false;

    Player &P(int id) { return *seat[id]; }
    int n() const { return (int)seat.size() - 1; }

    // ---------- 消息 ----------
    void sendTo(Player &p, const string &s) {
        if (p.bot || !p.connected || p.fd < 0) { if (p.bot) return; }
        printf("  [私->%s] %s\n", p.name.c_str(), s.c_str());
        if (!p.bot && p.connected && p.fd >= 0) {
            std::lock_guard<std::mutex> g(sendMx);
            sendLine(p.fd, s);
        }
    }
    void broadcast(const string &s) {
        printf("[公告] %s\n", s.c_str());
        for (auto &up : ps) {
            Player &p = *up;
            if (!p.bot && p.connected && p.fd >= 0) {
                std::lock_guard<std::mutex> g(sendMx);
                sendLine(p.fd, s);
            }
        }
    }

    // ---------- 工具 ----------
    double frand() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    int pick(const vector<int> &v) { return v[rng() % v.size()]; }

    vector<int> aliveIds() {
        vector<int> v;
        for (int i = 1; i <= n(); i++) if (P(i).alive) v.push_back(i);
        return v;
    }
    vector<int> aliveWithRole(Role r) {
        vector<int> v;
        for (int i = 1; i <= n(); i++) if (P(i).alive && P(i).role == r) v.push_back(i);
        return v;
    }
    vector<int> aliveBad() {
        vector<int> v;
        for (int i = 1; i <= n(); i++) if (P(i).alive && roleTeam(P(i).role) == BAD) v.push_back(i);
        return v;
    }
    vector<int> aliveNonBadExcept(int self) {
        vector<int> v;
        for (int i = 1; i <= n(); i++)
            if (P(i).alive && i != self && roleTeam(P(i).role) != BAD) v.push_back(i);
        return v;
    }
    vector<int> aliveExcept(int self) {
        vector<int> v;
        for (int i = 1; i <= n(); i++) if (P(i).alive && i != self) v.push_back(i);
        return v;
    }
    static bool contains(const vector<int> &v, int x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    }
    string listNames(const vector<int> &ids) {
        string s;
        for (int id : ids) s += std::to_string(id) + "." + P(id).name + " ";
        return s;
    }
    string plainNames(const vector<int> &ids) {
        string s;
        for (size_t i = 0; i < ids.size(); i++) s += (i ? "、" : "") + P(ids[i]).name;
        return s;
    }
    static int parseInt(const string &s) {
        size_t i = s.find_first_of("0123456789");
        if (i == string::npos) return INT_MIN;
        try { return std::stoi(s.substr(i)); } catch (...) { return INT_MIN; }
    }

    // ---------- 询问 ----------
    std::optional<string> askRaw(Player &p, const string &prompt, int tmo) {
        if (p.bot || !p.connected) return std::nullopt;
        {
            std::lock_guard<std::mutex> lk(p.mx);
            p.inbox.clear();
            p.awaiting = true;
        }
        sendTo(p, "[ASK] " + prompt);
        std::unique_lock<std::mutex> lk(p.mx);
        bool got = p.cv.wait_for(lk, seconds(tmo),
                                 [&] { return !p.inbox.empty() || !p.connected; });
        p.awaiting = false;
        if (!got || p.inbox.empty()) {
            lk.unlock();
            sendTo(p, "(超时, 自动处理)");
            return std::nullopt;
        }
        string s = p.inbox.front();
        p.inbox.pop_front();
        return s;
    }

    // 让 p 从 cand 中选一个座位号; allowSkip 时可输 0 放弃(返回 -1)
    int askChoice(Player &p, const string &prompt, const vector<int> &cand,
                  bool allowSkip, int tmo = 45, double botSkipP = 0.0) {
        if (cand.empty()) return -1;
        if (p.bot || !p.connected) {
            if (allowSkip && frand() < botSkipP) return -1;
            return pick(cand);
        }
        string full = prompt + " 可选: " + listNames(cand) +
                      (allowSkip ? "(输入座位号, 0=放弃)" : "(输入座位号)");
        for (int t = 0; t < 3; t++) {
            auto a = askRaw(p, full, tmo);
            if (!a) break;
            int v = parseInt(*a);
            if (allowSkip && v == 0) return -1;
            if (v != INT_MIN && contains(cand, v)) return v;
            sendTo(p, "无效选择, 请重新输入。");
        }
        if (allowSkip) return -1;
        return pick(cand);
    }

    bool askYes(Player &p, const string &prompt, int tmo = 30, double botP = 0.3) {
        if (p.bot || !p.connected) return frand() < botP;
        auto a = askRaw(p, prompt + " (y=是 / n=否)", tmo);
        if (!a) return false;
        string s = *a;
        for (auto &c : s) c = (char)tolower((unsigned char)c);
        return s == "y" || s == "1" || s == "yes" || s == "是";
    }

    // ---------- 死亡结算 ----------
    // 夜晚伤害入口: 结算长存符和生蚝壳
    void applyHit(Player &t, bool physical, bool savable, bool unstoppable) {
        if (!t.alive) return;
        if (!unstoppable) {
            if (t.charmed) return; // 长存符: 除放逐外不死
            if (physical && t.role == R_OYSTER && t.shellIntact) {
                t.shellIntact = false; // 壳碎了, 本人无察觉
                return;
            }
        }
        auto it = dying.find(t.id);
        if (it == dying.end()) dying[t.id] = savable;
        else it->second = it->second || savable;
    }

    // 真正杀死(处理连锁: 相思者陪葬 / 木偶继承)
    void kill(Player &p, bool silent) {
        if (!p.alive) return;
        p.alive = false;
        if (silent) nightDead.push_back(p.id);
        else broadcast("💀 " + p.name + " (" + std::to_string(p.id) + "号) 死亡");
        // 相思者死亡 → 绑定者陪葬
        if (p.role == R_LOVER && p.loverTarget > 0 && P(p.loverTarget).alive) {
            if (!silent) broadcast("💔 相思之链发动!");
            kill(P(p.loverTarget), silent);
        }
        // 木偶师死亡 → 木偶继承
        if (p.role == R_PUPPETEER && p.puppet > 0 && P(p.puppet).alive) {
            Player &q = P(p.puppet);
            q.role = R_PUPPETEER;
            q.puppet = -1;
            sendTo(q, "🎭 木偶师死亡, 丝线传到你手中——你成为新的木偶师(第三方)!");
        }
        // 有人的木偶死了 → 木偶师下晚可重选
        for (int i = 1; i <= n(); i++) {
            Player &m = P(i);
            if (m.alive && m.role == R_PUPPETEER && m.puppet == p.id) {
                m.puppet = -1;
                sendTo(m, "你的木偶死了, 下个夜晚可以重新选择木偶。");
            }
        }
    }

    // 白天杀伤(长存符可挡, 生蚝壳只挡夜晚不挡白天)
    bool killDay(Player &t) {
        if (!t.alive) return false;
        if (t.charmed) {
            broadcast("✨ 一股神秘力量保护了 " + t.name + ", TA毫发无伤!");
            return false;
        }
        kill(t, false);
        return true;
    }

    void checkWin() {
        int g = 0, b = 0, t = 0;
        for (int i = 1; i <= n(); i++) {
            if (!P(i).alive) continue;
            Team tm = roleTeam(P(i).role);
            if (tm == GOOD) g++; else if (tm == BAD) b++; else t++;
        }
        if (b == 0 && t == 0) throw GameOver{GOOD};
        if (g == 0) throw GameOver{b > 0 ? BAD : THIRD};
    }

    // ---------- 配置发牌 ----------
    void assignRoles() {
        int N = n();
        int nb = std::max(1, N / 3);
        int nt = N >= 8 ? 1 : 0;
        int ng = N - nb - nt;
        vector<Role> roles;
        vector<Role> gp = {R_GUNNER, R_OYSTER, R_IMMORTAL, R_USURPER, R_SONIC,
                           R_BOMBER, R_CORRECTOR, R_DECODER, R_PUNISHER, R_JOKER, R_SPECULATOR};
        std::shuffle(gp.begin(), gp.end(), rng);
        roles.push_back(R_DETECTIVE);
        roles.push_back(R_PHARM);
        for (int i = 0; i < ng - 2; i++)
            roles.push_back(i < (int)gp.size() ? gp[i] : R_CITIZEN);
        vector<Role> bp = {R_EATER, R_CURSER, R_DORM, R_LOVER, R_STEALER,
                           R_RIPPER, R_THUNDER, R_SPACE, R_MELON};
        std::shuffle(bp.begin(), bp.end(), rng);
        roles.push_back(R_DISMANTLER);
        for (int i = 0; i < nb - 1; i++)
            roles.push_back(i < (int)bp.size() ? bp[i] : R_DISMANTLER);
        vector<Role> tp = {R_LYINGH, R_LURKER, R_PUPPETEER};
        std::shuffle(tp.begin(), tp.end(), rng);
        for (int i = 0; i < nt; i++) roles.push_back(tp[i]);

        std::shuffle(roles.begin(), roles.end(), rng);
        for (int i = 1; i <= N; i++) {
            P(i).role = roles[i - 1];
            if (roles[i - 1] == R_JOKER) P(i).cursedJoker = true;
        }

        // 公布板子
        vector<string> names;
        for (auto r : roles) names.push_back(roleName(r));
        std::sort(names.begin(), names.end());
        string board;
        for (auto &s : names) board += s + " ";
        broadcast("📜 本局配置(" + std::to_string(N) + "人): " + board);
        broadcast("👥 座位表: " + listNames(aliveIds()));
        for (int i = 1; i <= N; i++) {
            Player &p = P(i);
            sendTo(p, "🎴 你的身份: " + string(roleName(p.role)) + " [" +
                          teamName(roleTeam(p.role)) + "] —— " + roleDesc(p.role));
        }
    }

    void informBadTeam(const string &why) {
        auto bad = aliveBad();
        for (int id : bad)
            sendTo(P(id), "😈 " + why + " 坏人阵营成员: " + listNames(bad));
    }

    // 从 from 顺时针找下一个活人(跳过 from 本身)
    int nextAlive(int from) {
        for (int k = 1; k <= n(); k++) {
            int i = (from - 1 + k) % n() + 1;
            if (i != from && P(i).alive) return i;
        }
        return -1;
    }

    // ---------- 夜晚 ----------
    void nightPhase() {
        nightNo++;
        broadcast("");
        broadcast("🌙 ========== 第 " + std::to_string(nightNo) + " 夜, 天黑请闭眼 ==========");
        dying.clear();
        nightDead.clear();
        sonicTonight = pressureTonight = false;

        // 1. 不灭之神首先睁眼
        for (int id : aliveWithRole(R_IMMORTAL)) {
            Player &p = P(id);
            if (p.charms > 0 &&
                askYes(p, "是否使用长存符? (剩余 " + std::to_string(p.charms) +
                              " 张, 生效至明日白天结束, 除放逐外不死)", 30, 0.25)) {
                p.charms--;
                p.charmed = true;
                sendTo(p, "🛡️ 长存符生效。");
            }
        }

        // 2. 躺尸氢的标记发作
        for (int id : aliveIds()) {
            Player &p = P(id);
            if (p.marked) { p.marked = false; applyHit(p, true, true, false); }
        }

        // 3. 第三夜: 天罚者
        if (nightNo == 3) {
            for (int id : aliveWithRole(R_PUNISHER)) {
                Player &p = P(id);
                vector<int> evil;
                for (int i : aliveIds())
                    if (roleTeam(P(i).role) != GOOD) evil.push_back(i);
                sendTo(p, "⚡ 天罚时刻! 场上坏人与第三方: " +
                              (evil.empty() ? string("(无)") : listNames(evil)));
                if (!evil.empty()) {
                    int t = askChoice(p, "选择降下天罚的目标", evil, false, 45);
                    applyHit(P(t), false, false, false);
                }
                applyHit(p, false, false, true); // 自身必死, 无遗言
                sendTo(p, "你已完成使命, 今夜死去(不可留遗言)。");
            }
        }

        // 4. 音波释放者(非第一夜)
        if (nightNo > 1)
            for (int id : aliveWithRole(R_SONIC)) {
                Player &p = P(id);
                if (!p.usedOneShot &&
                    askYes(p, "是否释放音波? (今晚坏人和第三方全部无法行动)", 30, 0.2)) {
                    p.usedOneShot = true;
                    sonicTonight = true;
                    sendTo(p, "🔊 音波已释放。");
                }
            }

        // 5. 宿管老师(非第一夜, 被音波压制则无法行动)
        if (nightNo > 1 && !sonicTonight)
            for (int id : aliveWithRole(R_DORM)) {
                Player &p = P(id);
                if (!p.usedOneShot &&
                    askYes(p, "是否释放威压? (今晚好人全部无法行动)", 30, 0.2)) {
                    p.usedOneShot = true;
                    pressureTonight = true;
                    sendTo(p, "🏫 威压已释放。");
                }
            }

        // 6. 第一夜的开局技能
        if (nightNo == 1) {
            for (int id : aliveWithRole(R_USURPER)) {
                Player &p = P(id);
                int t = askChoice(p, "【夺权】选择一名玩家", aliveExcept(id), false, 45);
                Player &q = P(t);
                if (roleTeam(q.role) == GOOD) {
                    p.role = q.role;
                    if (p.role == R_JOKER) p.cursedJoker = true;
                    sendTo(p, "对方是好人! 你复制了TA的技能, 现在你是: " +
                                  string(roleName(p.role)) + " —— " + roleDesc(p.role));
                } else {
                    p.role = R_DISMANTLER;
                    sendTo(p, "对方不是好人! 你堕落为【人体拆卸者】, 加入坏人阵营!");
                }
            }
            // 坏人互认(在夺权结算之后)
            informBadTeam("天黑了, 请互相认识。");
            for (int id : aliveWithRole(R_CURSER)) {
                Player &p = P(id);
                int t = askChoice(p, "【天谴】选择一名好人施加Joker诅咒",
                                  aliveNonBadExcept(id), false, 45);
                if (t > 0 && roleTeam(P(t).role) == GOOD) P(t).cursedJoker = true;
                sendTo(p, "诅咒已降下。");
            }
            for (int id : aliveWithRole(R_LOVER)) {
                Player &p = P(id);
                int t = askChoice(p, "【相思】绑定一名玩家(你死时TA陪葬)",
                                  aliveNonBadExcept(id), false, 45);
                p.loverTarget = t;
                sendTo(p, "你已与 " + P(t).name + " 结下相思之链。");
            }
        }

        // 7. 坏人拆卸(音波之夜无法行动)
        if (!sonicTonight) {
            auto bad = aliveBad();
            if (!bad.empty()) {
                int killer = -1;
                for (int id : bad) if (P(id).role == R_DISMANTLER) { killer = id; break; }
                if (killer < 0) killer = bad[0]; // 拆卸者全灭时由其余坏人代行
                vector<int> cand;
                for (int i : aliveIds())
                    if (roleTeam(P(i).role) != BAD) cand.push_back(i);
                if (!cand.empty()) {
                    int t = askChoice(P(killer), "【坏人】选择今晚的拆卸目标", cand, true, 45, 0.05);
                    if (t > 0) {
                        applyHit(P(t), true, true, false);
                        for (int id : bad)
                            if (id != killer) sendTo(P(id), "今晚坏人的目标: " + P(t).name);
                    }
                }
            }
        }

        // 8. 空间拆卸者(坏人, 一次性)
        if (!sonicTonight)
            for (int id : aliveWithRole(R_SPACE)) {
                Player &p = P(id);
                if (p.usedOneShot) continue;
                if (!askYes(p, "是否发动空间拆卸? (杀死相邻两人)", 30, 0.15)) continue;
                vector<int> cand;
                for (int i : aliveIds())
                    if (i != id && nextAlive(i) != id && nextAlive(i) > 0) cand.push_back(i);
                if (cand.empty()) continue;
                p.usedOneShot = true;
                int a = askChoice(p, "选择第一个目标(其顺时针相邻者一同被杀)", cand, false, 45);
                int b = nextAlive(a);
                applyHit(P(a), true, true, false);
                if (b > 0) applyHit(P(b), true, true, false);
                sendTo(p, "你拆掉了 " + P(a).name + " 和 " + (b > 0 ? P(b).name : "??"));
            }

        // 9. 窃取者(坏人, 一次性)
        if (!sonicTonight)
            for (int id : aliveWithRole(R_STEALER)) {
                Player &p = P(id);
                if (p.usedOneShot) continue;
                if (!askYes(p, "是否发动窃取? (把一人变为无技能居民)", 30, 0.2)) continue;
                auto cand = aliveNonBadExcept(id);
                if (cand.empty()) continue;
                p.usedOneShot = true;
                int t = askChoice(p, "选择窃取目标", cand, false, 45);
                Player &q = P(t);
                q.role = R_CITIZEN;
                q.cursedJoker = false;
                sendTo(p, "窃取成功, " + q.name + " 已沦为普通居民。");
                sendTo(q, "⚠️ 你的技能被神秘力量窃走, 你现在是【冲奖王国居民】(好人阵营, 无技能)。");
            }

        // 10. 躺尸氢(第三方)
        if (!sonicTonight)
            for (int id : aliveWithRole(R_LYINGH)) {
                Player &p = P(id);
                auto cand = aliveExcept(id);
                if (cand.empty()) continue;
                int k = askChoice(p, "【躺尸氢】选择直接杀死的目标", cand, false, 45);
                applyHit(P(k), true, true, false);
                vector<int> cand2;
                for (int i : cand) if (i != k) cand2.push_back(i);
                if (!cand2.empty()) {
                    int m = askChoice(p, "选择打标记的目标(下个夜晚死亡)", cand2, true, 45, 0.1);
                    if (m > 0) { P(m).marked = true; sendTo(p, "已标记 " + P(m).name); }
                }
            }

        // 11. 木偶师(第三方)
        if (!sonicTonight)
            for (int id : aliveWithRole(R_PUPPETEER)) {
                Player &p = P(id);
                if (p.puppet <= 0 || !P(p.puppet).alive) {
                    auto cand = aliveExcept(id);
                    if (!cand.empty()) {
                        int c = askChoice(p, "【木偶师】选择你的木偶(你死时TA继承你的身份)",
                                          cand, false, 45);
                        p.puppet = c;
                        sendTo(p, "你的木偶: " + P(c).name);
                    }
                }
                auto cand = aliveExcept(id);
                if (!cand.empty()) {
                    int t = askChoice(p, "选择今晚的击杀目标", cand, true, 45, 0.1);
                    if (t > 0) applyHit(P(t), true, true, false);
                }
            }

        // 12. 潜伏者(第三方, 坏人全灭后解锁杀人)
        if (!sonicTonight)
            for (int id : aliveWithRole(R_LURKER)) {
                Player &p = P(id);
                if (!aliveBad().empty()) {
                    sendTo(p, "坏人尚存, 你继续潜伏(无行动)。");
                    continue;
                }
                auto cand = aliveExcept(id);
                if (!cand.empty()) {
                    int t = askChoice(p, "【潜伏者】杀戮时刻! 选择今晚的目标", cand, true, 45, 0.05);
                    if (t > 0) applyHit(P(t), true, true, false);
                }
            }

        // 13. 药师(好人, 被威压则无法行动)
        if (!pressureTonight)
            for (int id : aliveWithRole(R_PHARM)) {
                Player &p = P(id);
                vector<int> allDying, savableIds;
                for (auto &kv : dying) {
                    allDying.push_back(kv.first);
                    if (kv.second && kv.first != id) savableIds.push_back(kv.first);
                }
                sendTo(p, allDying.empty() ? "今晚(目前)无人倒下。"
                                           : "今晚倒下的人: " + listNames(allDying));
                if (p.hasSave && !savableIds.empty()) {
                    int s = askChoice(p, "是否使用解药救人?", savableIds, true, 40, 0.5);
                    if (s > 0) {
                        dying.erase(s);
                        p.hasSave = false;
                        sendTo(p, "💊 你救下了 " + P(s).name);
                    }
                }
                if (p.hasPoison) {
                    auto cand = aliveExcept(id);
                    int t = askChoice(p, "是否使用毒药杀一人?", cand, true, 40, 0.2);
                    if (t > 0) {
                        p.hasPoison = false;
                        applyHit(P(t), false, false, false); // 毒药非物理, 生蚝壳挡不住
                        sendTo(p, "☠️ 你毒了 " + P(t).name);
                    }
                }
            }

        // 14. 推理大师(好人)
        if (!pressureTonight)
            for (int id : aliveWithRole(R_DETECTIVE)) {
                Player &p = P(id);
                auto cand = aliveExcept(id);
                if (cand.empty()) continue;
                int t = askChoice(p, "【推理】选择要推理身份的玩家", cand, true, 45, 0.0);
                if (t < 0) continue;
                Player &q = P(t);
                string res;
                if (q.role == R_LURKER) res = "好人";           // 潜伏者永远显示好人
                else if (q.cursedJoker) res = "坏人";           // Joker/被天谴
                else res = roleTeam(q.role) == GOOD ? "好人"
                         : roleTeam(q.role) == BAD ? "坏人" : "第三方";
                sendTo(p, "🔍 推理结果: " + q.name + " 是【" + res + "】");
            }

        // 15. 投机者(好人, 好人占比>50%时可跳槽)
        if (!pressureTonight)
            for (int id : aliveWithRole(R_SPECULATOR)) {
                Player &p = P(id);
                int g = 0, total = 0;
                for (int i : aliveIds()) {
                    total++;
                    if (roleTeam(P(i).role) == GOOD) g++;
                }
                if (2 * g > total &&
                    askYes(p, "好人仍占多数(" + std::to_string(g) + "/" + std::to_string(total) +
                                  "), 是否跳槽加入坏人阵营?", 30, 0.15)) {
                    p.role = R_DISMANTLER;
                    p.cursedJoker = false;
                    sendTo(p, "你已叛变, 成为【人体拆卸者】!");
                    informBadTeam("有人跳槽加入了坏人阵营!");
                }
            }

        // 结算
        vector<int> toKill;
        for (auto &kv : dying) toKill.push_back(kv.first);
        for (int id : toKill) kill(P(id), true);
    }

    // ---------- 白天 ----------
    void dayPhase() {
        dayNo++;
        broadcast("");
        broadcast("☀️ ========== 第 " + std::to_string(dayNo) + " 天, 天亮请睁眼 ==========");
        if (sonicTonight) broadcast("🔊 昨夜一阵刺耳音波回荡, 坏人与第三方被震得无法行动!");
        if (pressureTonight) broadcast("🏫 昨夜宿管的威压笼罩宿舍, 好人无法行动!");
        if (nightDead.empty()) broadcast("昨夜是平安夜, 无人死亡。");
        else {
            std::shuffle(nightDead.begin(), nightDead.end(), rng);
            vector<string> names;
            string s;
            for (int id : nightDead) s += P(id).name + "(" + std::to_string(id) + "号) ";
            broadcast("💀 昨夜死亡: " + s + "(夜晚死亡无遗言)");
        }
        checkWin();

        // 炮手(发言前)
        for (int id : aliveWithRole(R_GUNNER)) {
            Player &p = P(id);
            if (p.usedOneShot) continue;
            if (!askYes(p, "是否在发言前开炮?", 25, 0.12)) continue;
            auto cand = aliveExcept(id);
            if (cand.empty()) continue;
            p.usedOneShot = true;
            int t = askChoice(p, "选择开炮目标", cand, false, 40);
            broadcast("💥 炮手 " + p.name + " 掏出大炮, 轰向 " + P(t).name + "!");
            bool wasGood = roleTeam(P(t).role) == GOOD;
            killDay(P(t));
            if (wasGood && p.alive) {
                broadcast("😱 误杀好人! 炮手随之殉爆!");
                killDay(p);
            }
            checkWin();
        }

        // 爆破者(发言前)
        for (int id : aliveWithRole(R_BOMBER)) {
            Player &p = P(id);
            if (!p.alive || p.usedOneShot) continue;
            if (!askYes(p, "是否发动爆破(选两人与你同归于尽)?", 25, 0.1)) continue;
            auto cand = aliveExcept(id);
            if ((int)cand.size() < 2) continue;
            p.usedOneShot = true;
            int a = askChoice(p, "选择第一个目标", cand, false, 40);
            vector<int> cand2;
            for (int i : cand) if (i != a) cand2.push_back(i);
            int b = askChoice(p, "选择第二个目标", cand2, false, 40);
            broadcast("🧨 爆破者 " + p.name + " 抱着炸药冲向 " + P(a).name + " 和 " + P(b).name + "!");
            killDay(P(a));
            killDay(P(b));
            if (p.alive) kill(p, false);
            checkWin();
        }

        // 发言
        broadcast("🗣️ —— 依次发言 ——");
        static const char *botTalks[] = {
            "我是好人, 真的。", "昨晚睡得很香, 什么都不知道。", "过。",
            "我觉得刚才有人发言很可疑…", "保平安, 别投我。", "我先听大家的。",
            "推理大师快出来带带节奏。", "我身份很硬, 别查我浪费夜晚。"};
        for (int id : aliveIds()) {
            Player &p = P(id);
            if (!p.alive) continue;
            string talk;
            if (p.bot || !p.connected) talk = botTalks[rng() % 8];
            else {
                broadcast("👉 请 " + std::to_string(id) + "号 " + p.name + " 发言…");
                auto a = askRaw(p, "轮到你发言(输入一行, 直接回车=过):", 60);
                talk = a ? *a : "(沉默)";
                if (talk.empty()) talk = "(过)";
            }
            broadcast("💬 " + p.name + ": " + talk);
        }

        // 修正者
        for (int id : aliveWithRole(R_CORRECTOR)) {
            Player &p = P(id);
            if (!p.alive || p.usedOneShot) continue;
            if (!askYes(p, "是否发动修正? (选一人: 好人则你死, 否则TA明牌变居民)", 25, 0.12)) continue;
            auto cand = aliveExcept(id);
            if (cand.empty()) continue;
            p.usedOneShot = true;
            int t = askChoice(p, "选择修正目标", cand, false, 40);
            Player &q = P(t);
            broadcast("🔧 修正者 " + p.name + " 对 " + q.name + " 发动修正!");
            if (roleTeam(q.role) == GOOD) {
                broadcast("对方是好人, 修正失败, 修正者以身殉道!");
                killDay(p);
            } else {
                broadcast("修正成功! " + q.name + " 的真实身份是【" + roleName(q.role) +
                          "】, 现被改造为冲奖王国居民(好人阵营)。");
                q.role = R_CITIZEN;
                q.cursedJoker = false;
            }
            checkWin();
        }

        // 撕裂者: 切断电脑, 今天无法投票
        bool voteCut = false;
        for (int id : aliveWithRole(R_RIPPER)) {
            Player &p = P(id);
            if (!p.alive || p.usedOneShot) continue;
            if (askYes(p, "是否用小说切断电脑? (今天无法投票)", 25, 0.15)) {
                p.usedOneShot = true;
                voteCut = true;
                broadcast("📕 一本小说划过, 电脑线被切断——今天无法投票!");
            }
        }

        int exiled = -1;
        if (!voteCut) {
            // 雷电法王先秘密宣告
            bool thunderOn = false;
            int thunderId = -1;
            for (int id : aliveWithRole(R_THUNDER)) {
                Player &p = P(id);
                if (p.usedOneShot) continue;
                if (askYes(p, "是否发动雷电审判? (今日投你的居民全部死亡)", 25, 0.15)) {
                    p.usedOneShot = true;
                    thunderOn = true;
                    thunderId = id;
                }
            }

            // 并行收集投票
            auto ids = aliveIds();
            broadcast("🗳️ —— 投票放逐 —— (60秒)");
            for (int id : ids) {
                Player &p = P(id);
                if (p.bot || !p.connected) continue;
                {
                    std::lock_guard<std::mutex> lk(p.mx);
                    p.inbox.clear();
                    p.awaiting = true;
                }
                sendTo(p, "[ASK] 投票放逐一人, 输入座位号(0=弃票): " + listNames(ids));
            }
            auto deadline = steady_clock::now() + seconds(60);
            vector<std::pair<int, int>> votes;
            for (int id : ids) {
                Player &p = P(id);
                int t = 0;
                if (p.bot || !p.connected) {
                    t = pick(ids);
                    if (t == id) t = 0;
                } else {
                    std::unique_lock<std::mutex> lk(p.mx);
                    p.cv.wait_until(lk, deadline,
                                    [&] { return !p.inbox.empty() || !p.connected; });
                    p.awaiting = false;
                    if (!p.inbox.empty()) {
                        int v = parseInt(p.inbox.front());
                        p.inbox.pop_front();
                        if (v != INT_MIN && v != 0 && contains(ids, v)) t = v;
                    }
                }
                votes.push_back({id, t});
            }
            std::map<int, int> tally;
            string voteLog;
            for (auto &[voter, tgt] : votes) {
                voteLog += P(voter).name + "→" + (tgt ? P(tgt).name : "弃") + "  ";
                if (tgt) tally[tgt]++;
            }
            broadcast("票型: " + voteLog);

            // 雷电审判
            if (thunderOn) {
                Player &th = P(thunderId);
                vector<int> victims;
                for (auto &[voter, tgt] : votes)
                    if (tgt == thunderId && P(voter).alive && P(voter).role == R_CITIZEN)
                        victims.push_back(voter);
                broadcast("⚡ 雷电法王 " + th.name + " 发动天雷审判!");
                if (victims.empty()) broadcast("……但没有居民投TA, 天雷劈了个寂寞。");
                for (int v : victims) killDay(P(v));
                checkWin();
            }

            int best = -1, bestCnt = 0;
            bool tie = false;
            for (auto &[tgt, c] : tally) {
                if (c > bestCnt) { best = tgt; bestCnt = c; tie = false; }
                else if (c == bestCnt) tie = true;
            }
            if (best > 0 && !tie && P(best).alive) exiled = best;
            if (exiled > 0) broadcast("📢 投票结果: " + P(exiled).name + " 得 " +
                                       std::to_string(bestCnt) + " 票, 将被放逐。");
            else broadcast("📢 平票或无有效票, 无人被放逐。");

            // 破译者
            for (int id : aliveWithRole(R_DECODER)) {
                Player &p = P(id);
                if (!p.alive || p.usedOneShot) continue;
                if (!askYes(p, "是否发动破译? (推翻本次投票, 由你选择放逐对象)", 25, 0.1)) continue;
                p.usedOneShot = true;
                broadcast("💻 破译者 " + p.name + " 破译了投票系统, 本次投票无效!");
                auto cand = aliveExcept(id);
                if (!cand.empty()) {
                    exiled = askChoice(p, "选择要放逐的玩家", cand, false, 40);
                    broadcast("📢 破译者宣布: 放逐 " + P(exiled).name + "!");
                }
            }
        }

        // 放逐执行
        if (exiled > 0 && P(exiled).alive) {
            Player &ex = P(exiled);
            broadcast("🚪 " + ex.name + " 被放逐出冲奖王国。");
            bool covered = false;
            for (int id : aliveWithRole(R_MELON)) {
                Player &p = P(id);
                if (p.usedOneShot || id == exiled) continue;
                if (askYes(p, "是否用瓜皮罩住 " + ex.name + " 的角色卡(使其无法明牌)?", 20, 0.35)) {
                    p.usedOneShot = true;
                    covered = true;
                    broadcast("🍉 一片巨大的瓜皮从天而降, 罩住了 " + ex.name + " 的角色卡, 无法明牌!");
                }
            }
            if (!covered) {
                bool reveal = ex.bot || !ex.connected
                                  ? frand() < 0.5
                                  : askYes(ex, "是否明牌(公开你的角色)?", 25, 0.5);
                if (reveal) broadcast("🎴 " + ex.name + " 明牌: 【" + roleName(ex.role) + "】");
            }
            string lw;
            if (ex.bot || !ex.connected) lw = "我看好你们……";
            else {
                auto a = askRaw(ex, "请留遗言(一行):", 40);
                lw = a ? *a : "(无言离场)";
            }
            broadcast("🕊️ " + ex.name + " 的遗言: " + lw);
            kill(ex, false); // 放逐无视长存符
            checkWin();
        }

        // 电脑吞食者: 投票后可自暴
        if (!voteCut)
            for (int id : aliveWithRole(R_EATER)) {
                Player &p = P(id);
                if (!p.alive) continue;
                if (!askYes(p, "是否自暴? (你死亡, 直接吃掉一人并立刻入夜)", 25, 0.1)) continue;
                broadcast("🔥 电脑吞食者 " + p.name + " 自暴了! 机箱轰然炸开!");
                auto cand = aliveExcept(id);
                if (!cand.empty()) {
                    int t = askChoice(p, "选择要吞掉的玩家", cand, false, 40);
                    killDay(P(t));
                }
                kill(p, false);
                checkWin();
                break; // 直接进入黑夜
            }

        // 长存符到期
        for (int i = 1; i <= n(); i++) P(i).charmed = false;
    }

    // ---------- 主流程 ----------
    void run() {
        broadcast("");
        broadcast("🏰 ======= 冲奖王国 · 游戏开始 =======");
        assignRoles();
        try {
            while (true) {
                nightPhase();
                dayPhase();
                if (dayNo > 30) throw GameOver{3};
                checkWin();
            }
        } catch (GameOver over) {
            broadcast("");
            broadcast("🎉 ================ 游戏结束 ================");
            switch (over.winner) {
                case GOOD:  broadcast("🏆 好人阵营获胜! 冲奖王国重归和平!"); break;
                case BAD:   broadcast("🏆 坏人阵营获胜! 王国陷落!"); break;
                case THIRD: broadcast("🏆 第三方获胜! 螳螂捕蝉, 黄雀在后!"); break;
                default:    broadcast("⏳ 天数耗尽, 平局收场。"); break;
            }
            broadcast("—— 身份公开 ——");
            for (int i = 1; i <= n(); i++) {
                Player &p = P(i);
                broadcast("  " + std::to_string(i) + "号 " + p.name + ": " + roleName(p.role) +
                          " [" + teamName(roleTeam(p.role)) + "] " + (p.alive ? "存活" : "死亡"));
            }
            broadcast("GAME_OVER");
        }
    }

    // ---------- 大厅/网络 ----------
    void handleLine(Player *p, const string &line) {
        {
            std::unique_lock<std::mutex> lk(p->mx);
            if (p->awaiting) {
                p->inbox.push_back(line);
                p->cv.notify_all();
                return;
            }
        }
        if (!started) {
            if (line.rfind("JOIN ", 0) == 0) {
                p->name = line.substr(5);
                if (p->name.empty()) p->name = "玩家" + std::to_string((int)ps.size());
                broadcast("✅ " + p->name + " 加入房间 (当前 " +
                          std::to_string(countConnected()) + " 人)");
                if (p == ps.front().get())
                    sendTo(*p, "你是房主, 人齐后输入 start 开始游戏。");
                return;
            }
            if (line == "start" || line == "开始") {
                if (p == ps.front().get()) startRequested = true;
                else sendTo(*p, "只有房主能开始游戏。");
                return;
            }
        }
        // 场外聊天
        if (!p->name.empty())
            broadcast((p->alive ? "💬 " : "👻 ") + p->name + ": " + line);
    }

    int countConnected() {
        int c = 0;
        for (auto &up : ps) if (up->connected) c++;
        return c;
    }

    void onDisconnect(Player *p) {
        p->connected = false;
        if (p->fd >= 0) { close(p->fd); p->fd = -1; }
        {
            std::lock_guard<std::mutex> lk(p->mx);
            p->cv.notify_all();
        }
        if (started && p->alive)
            broadcast("📡 " + p->name + " 掉线, 由AI接管。");
        p->bot = true;
    }

    void readerLoop(Player *p) {
        string buf;
        char tmp[4096];
        while (true) {
            ssize_t nrd = recv(p->fd, tmp, sizeof tmp, 0);
            if (nrd <= 0) { onDisconnect(p); return; }
            buf.append(tmp, (size_t)nrd);
            size_t pos;
            while ((pos = buf.find('\n')) != string::npos) {
                string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                handleLine(p, line);
            }
        }
    }
};

static Game g;

static void printLanIPs(int port) {
    printf("局域网连接方式: ./client <本机IP> %d <昵称>\n本机IP:\n", port);
    struct ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (auto *i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            char buf[64];
            auto *sin = (sockaddr_in *)i->ifa_addr;
            inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
            if (string(buf) != "127.0.0.1") printf("  %s (%s)\n", buf, i->ifa_name);
        }
        freeifaddrs(ifs);
    }
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    int port = 5555, bots = 0, selftest = 0;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--bots" && i + 1 < argc) bots = atoi(argv[++i]);
        else if (a == "--selftest" && i + 1 < argc) selftest = atoi(argv[++i]);
        else if (atoi(a.c_str()) > 0) port = atoi(a.c_str());
    }

    if (selftest > 0) {
        // 全机器人自动对局(测试用)
        for (int i = 1; i <= selftest; i++) {
            auto p = std::make_unique<Player>();
            p->id = i;
            p->name = "机器人" + std::to_string(i);
            p->bot = true;
            g.ps.push_back(std::move(p));
        }
        g.seat.push_back(nullptr);
        for (auto &up : g.ps) g.seat.push_back(up.get());
        g.started = true;
        g.run();
        return 0;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    listen(lfd, 16);
    printf("🏰 冲奖王国服务端已启动, 端口 %d\n", port);
    printLanIPs(port);
    printf("等待玩家加入… (房主输入 start 开始, --bots %d 补齐机器人)\n", bots);

    // 等待玩家加入, 直到房主要求开始
    vector<std::thread> readers;
    while (!g.startRequested) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(lfd, &fds);
        timeval tv{1, 0};
        int r = select(lfd + 1, &fds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(lfd, &fds)) {
            int cfd = accept(lfd, nullptr, nullptr);
            if (cfd < 0) continue;
            auto p = std::make_unique<Player>();
            p->fd = cfd;
            p->connected = true;
            Player *raw = p.get();
            g.ps.push_back(std::move(p));
            readers.emplace_back([raw] { g.readerLoop(raw); });
        }
    }

    // 建座位表: 在线玩家 + 机器人
    vector<Player *> active;
    for (auto &up : g.ps) if (up->connected) active.push_back(up.get());
    for (int i = 0; i < bots; i++) {
        auto p = std::make_unique<Player>();
        p->name = "机器人" + std::to_string(i + 1);
        p->bot = true;
        g.ps.push_back(std::move(p));
        active.push_back(g.ps.back().get());
    }
    if ((int)active.size() < 4) {
        g.broadcast("至少需要4名玩家(含机器人), 当前不足, 服务器退出。");
        return 1;
    }
    g.seat.push_back(nullptr);
    int id = 0;
    for (auto *p : active) {
        p->id = ++id;
        g.seat.push_back(p);
    }
    g.started = true;
    g.run();

    std::this_thread::sleep_for(seconds(2));
    for (auto &up : g.ps) if (up->fd >= 0) close(up->fd);
    for (auto &t : readers) t.detach();
    return 0;
}
