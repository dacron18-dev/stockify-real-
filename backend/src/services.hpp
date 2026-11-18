// Concrete services: HTTPS client, Yahoo Finance, Supabase Auth and Supabase database.
#pragma once
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <cstdlib>
#include <httplib.h>
#include "interfaces.hpp"

inline std::string env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
inline std::string hostOf(std::string u) {
    auto p = u.find("://");
    if (p != std::string::npos) u = u.substr(p + 3);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}
inline bool isJwt(const std::string& key) { return key.rfind("eyJ", 0) == 0; }   // legacy anon/service keys

struct HttpResult {
    int status; std::string body;
    json asJson() const { return json::parse(body.empty() ? "null" : body, nullptr, false); }
};

class HttpsClient {
    std::string host_;
public:
    explicit HttpsClient(std::string host) : host_(std::move(host)) {}
    HttpResult send(const std::string& m, const std::string& path,
                    const httplib::Headers& h, const std::string& body = "") const {
        httplib::SSLClient c(host_);
        c.set_follow_location(true);
        c.set_connection_timeout(8);
        c.set_read_timeout(10);
        httplib::Result r = m == "GET"   ? c.Get(path, h)
                          : m == "POST"  ? c.Post(path, h, body, "application/json")
                          : m == "PATCH" ? c.Patch(path, h, body, "application/json")
                                         : c.Delete(path, h);
        if (!r) throw std::runtime_error("Could not reach " + host_);
        return {r->status, r->body};
    }
};

inline std::string errText(const json& j, const std::string& fallback) {
    if (j.is_object())
        for (const char* k : {"error_description", "msg", "message"})
            if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
    return fallback;
}

// ================= Yahoo Finance =================
class YahooMarketData : public IMarketData {
    HttpsClient http_{"query1.finance.yahoo.com"};
    TtlCache<std::string, Quote> cache_{std::chrono::seconds(10)};   // generic class template
    TtlCache<std::string, std::vector<Candle>> histCache_{std::chrono::seconds(60)};
public:
    Quote getQuote(const std::string& s) override {
        if (auto hit = cache_.get(s)) return *hit;
        std::string path = s;                                       // '&' must be encoded in the URL (M&M.NS)
        for (size_t p; (p = path.find('&')) != std::string::npos;) path.replace(p, 1, "%26");
        auto r = http_.send("GET", "/v8/finance/chart/" + path + "?interval=1d&range=1d", {{"User-Agent", "Mozilla/5.0"}});
        json j = r.asJson();
        if (r.status != 200 || j.is_discarded() || !j.contains("chart") ||
            !j["chart"].contains("result") || j["chart"]["result"].is_null())
            throw UnknownSymbol("Unknown symbol: " + s);
        const json& m = j["chart"]["result"][0]["meta"];
        Quote q;
        q.symbol = s;
        q.name = m.value("shortName", m.value("longName", s));
        q.currency = m.value("currency", "INR");
        if (q.currency != "INR")                                    // Indian market only: reject US / foreign listings
            throw UnknownSymbol("Only Indian (NSE/BSE) stocks are supported: " + s);
        q.price = m.value("regularMarketPrice", 0.0);
        q.prevClose = m.value("chartPreviousClose", 0.0);
        if (q.price <= 0) throw TradeError("No live price for " + s);
        cache_.put(s, q);
        return q;
    }
    // Price history for the chart: range is 1d, 5d, 1mo, 6mo or 1y.
    std::vector<Candle> getHistory(const std::string& s, const std::string& range) override {
        static const std::map<std::string, std::string> interval{
            {"1d", "5m"}, {"5d", "15m"}, {"1mo", "1d"}, {"6mo", "1d"}, {"1y", "1d"}};
        auto iv = interval.find(range);
        if (iv == interval.end()) throw ValidationError("Range must be 1d, 5d, 1mo, 6mo or 1y");
        const std::string key = s + "|" + range;
        if (auto hit = histCache_.get(key)) return *hit;
        std::string path = s;
        for (size_t p; (p = path.find('&')) != std::string::npos;) path.replace(p, 1, "%26");
        auto r = http_.send("GET", "/v8/finance/chart/" + path + "?interval=" + iv->second + "&range=" + range,
                            {{"User-Agent", "Mozilla/5.0"}});
        json j = r.asJson();
        if (r.status != 200 || j.is_discarded() || !j.contains("chart") ||
            !j["chart"].contains("result") || j["chart"]["result"].is_null())
            throw UnknownSymbol("No chart data for " + s);
        const json& res = j["chart"]["result"][0];
        std::vector<Candle> out;
        if (res.contains("timestamp") && res["indicators"]["quote"][0].contains("close")) {
            const json& ts = res["timestamp"];
            const json& cl = res["indicators"]["quote"][0]["close"];
            for (size_t i = 0; i < ts.size() && i < cl.size(); ++i)
                if (!cl[i].is_null()) out.push_back({ts[i].get<long long>(), cl[i].get<double>()});   // skip gaps
        }
        if (out.empty()) throw TradeError("No chart data for " + s);
        histCache_.put(key, out);
        return out;
    }
};

// ================= Supabase Auth =================
class SupabaseAuth : public IAuthService {
    HttpsClient http_;
    std::string anon_;
    httplib::Headers hdr(const std::string& bearer = "") const {
        httplib::Headers h{{"apikey", anon_}};
        if (!bearer.empty()) h.emplace("Authorization", "Bearer " + bearer);
        else if (isJwt(anon_)) h.emplace("Authorization", "Bearer " + anon_);
        return h;
    }
public:
    SupabaseAuth(const std::string& url, std::string anon) : http_(hostOf(url)), anon_(std::move(anon)) {}
    std::string signUp(const std::string& e, const std::string& p) override {
        auto r = http_.send("POST", "/auth/v1/signup", hdr(), json{{"email", e}, {"password", p}}.dump());
        json j = r.asJson();
        if (r.status >= 400) throw AuthError(errText(j, "Sign-up failed"));
        if (j.contains("user") && j["user"].contains("id")) return j["user"]["id"].get<std::string>();
        if (j.contains("id")) return j["id"].get<std::string>();
        throw AuthError("Sign-up failed");
    }
    json signIn(const std::string& e, const std::string& p) override {
        auto r = http_.send("POST", "/auth/v1/token?grant_type=password", hdr(), json{{"email", e}, {"password", p}}.dump());
        json j = r.asJson();
        if (r.status >= 400) throw AuthError(errText(j, "Login failed"));   // real reason from Supabase
        return {{"token", j["access_token"]}, {"id", j["user"]["id"]}};
    }
    std::string verify(const std::string& token) override {
        auto r = http_.send("GET", "/auth/v1/user", hdr(token));
        if (r.status != 200) throw AuthError("Session expired. Please log in again.");
        return r.asJson()["id"].get<std::string>();
    }
};

// ================= Supabase Database (PostgREST) =================
class SupabaseRepository : public IRepository {
    HttpsClient http_;
    std::string key_;
    httplib::Headers hdr(const std::string& prefer = "") const {
        httplib::Headers h{{"apikey", key_}};
        if (isJwt(key_)) h.emplace("Authorization", "Bearer " + key_);
        if (!prefer.empty()) h.emplace("Prefer", prefer);
        return h;
    }
    json call(const std::string& m, const std::string& path, const std::string& body = "",
              const std::string& prefer = "") const {
        auto r = http_.send(m, "/rest/v1/" + path, hdr(prefer), body);
        if (r.status >= 400) throw std::runtime_error("Database error: " + r.body);
        return r.body.empty() ? json::array() : r.asJson();
    }
    static std::vector<PriceAlert> toAlerts(const json& rows) {
        std::vector<PriceAlert> out;
        for (auto& a : rows)
            out.emplace_back(a["id"].get<int>(), a["user_id"].get<std::string>(), a["symbol"].get<std::string>(),
                             a["target_price"].get<double>(), a["above"].get<bool>(), a["triggered"].get<bool>());
        return out;
    }
public:
    SupabaseRepository(const std::string& url, std::string serviceKey)
        : http_(hostOf(url)), key_(std::move(serviceKey)) {}

    void createProfile(const std::string& id, const std::string& email) override {
        call("POST", "profiles?on_conflict=id", json{{"id", id}, {"email", email}}.dump(),
             "resolution=ignore-duplicates,return=minimal");   // safe to call repeatedly
    }
    std::unique_ptr<User> loadUser(const std::string& id) override {
        json p = call("GET", "profiles?id=eq." + id + "&select=*");
        if (!p.is_array() || p.empty()) throw AuthError("Profile not found");
        const json& row = p[0];
        if (row["role"] == "admin") return std::make_unique<Admin>(id, row["email"]);
        auto t = std::make_unique<Trader>(id, row["email"], row["balance"].get<double>());
        for (auto& h : call("GET", "holdings?user_id=eq." + id + "&select=*"))
            t->portfolio().addHolding(Holding(h["symbol"], h["qty"], h["avg_price"].get<double>()));
        return t;
    }
    void saveTrader(const Trader& t) override {
        call("PATCH", "profiles?id=eq." + t.id(), json{{"balance", t.balance()}}.dump(), "return=minimal");
        call("DELETE", "holdings?user_id=eq." + t.id());
        json rows = json::array();
        for (auto& [sym, h] : t.portfolio().holdings())
            rows.push_back({{"user_id", t.id()}, {"symbol", sym}, {"qty", h.quantity()}, {"avg_price", h.averagePrice()}});
        if (!rows.empty()) call("POST", "holdings", rows.dump(), "return=minimal");
    }
    void recordTransaction(const std::string& uid, const Transaction& tx) override {
        call("POST", "trades", json{{"user_id", uid}, {"symbol", tx.symbol()}, {"side", toString(tx.type())},
                                    {"qty", tx.quantity()}, {"price", tx.executionPrice()}}.dump(), "return=minimal");
    }
    json listUsers() override { return call("GET", "profiles?select=id,email,role,balance&order=created_at.desc"); }
    json listTransactions(const std::string& uid = "") override {
        std::string f = uid.empty() ? "" : "&user_id=eq." + uid;
        return call("GET", "trades?select=*&order=created_at.desc&limit=100" + f);
    }
    void setBalance(const std::string& id, double b) override {
        call("PATCH", "profiles?id=eq." + id, json{{"balance", b}}.dump(), "return=minimal");
    }
    int saveAlert(const PriceAlert& a) override {
        json r = call("POST", "price_alerts", json{{"user_id", a.ownerId()}, {"symbol", a.symbol()},
                      {"target_price", a.target()}, {"above", a.above()}}.dump(), "return=representation");
        return r[0]["id"].get<int>();
    }
    std::vector<PriceAlert> activeAlerts() override { return toAlerts(call("GET", "price_alerts?triggered=eq.false&select=*")); }
    std::vector<PriceAlert> alertsFor(const std::string& uid) override {
        return toAlerts(call("GET", "price_alerts?user_id=eq." + uid + "&select=*&order=created_at.desc&limit=50"));
    }
    void markTriggered(int id) override {
        call("PATCH", "price_alerts?id=eq." + std::to_string(id), json{{"triggered", true}}.dump(), "return=minimal");
    }
    void addNotification(const Notification& n) override {
        call("POST", "notifications", json{{"user_id", n.userId()}, {"message", n.message()}}.dump(), "return=minimal");
    }
    std::vector<Notification> notifications(const std::string& uid) override {
        std::vector<Notification> out;
        for (auto& n : call("GET", "notifications?user_id=eq." + uid + "&select=*&order=created_at.desc&limit=30"))
            out.emplace_back(n["id"].get<int>(), uid, n["message"].get<std::string>(), n["is_read"].get<bool>(),
                             n["created_at"].get<std::string>());
        return out;
    }
    void markNotificationRead(int id, const std::string& uid) override {
        call("PATCH", "notifications?id=eq." + std::to_string(id) + "&user_id=eq." + uid,
             json{{"is_read", true}}.dump(), "return=minimal");
    }
};
