// Stockify services: interfaces (abstraction) + Yahoo Finance / Supabase implementations.
#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <httplib.h>
#include "models.hpp"
#include "core.hpp"

inline std::string env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
inline std::string hostOf(std::string u) {
    auto p = u.find("://");
    if (p != std::string::npos) u = u.substr(p + 3);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

struct HttpResult {
    int status; std::string body;
    json asJson() const { return json::parse(body.empty() ? "null" : body, nullptr, false); }
};

// Small wrapper so the rest of the code never touches raw HTTP.
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

// ================= Interfaces =================
struct Quote {
    std::string symbol, name, currency;
    double price = 0, prevClose = 0;
    json toJson() const {
        double chg = prevClose > 0 ? (price - prevClose) / prevClose * 100 : 0;
        return {{"symbol", symbol}, {"name", name}, {"currency", currency},
                {"price", price}, {"prevClose", prevClose}, {"changePct", chg}};
    }
};

class IMarketData {
public:
    virtual ~IMarketData() = default;
    virtual Quote getQuote(const std::string& symbol) = 0;
};

class IAuthService {
public:
    virtual ~IAuthService() = default;
    virtual std::string signUp(const std::string& email, const std::string& pw) = 0;   // returns user id
    virtual json signIn(const std::string& email, const std::string& pw) = 0;          // {token, id}
    virtual std::string verify(const std::string& token) = 0;                          // returns user id
};

class IRepository {
public:
    virtual ~IRepository() = default;
    virtual void createProfile(const std::string& id, const std::string& email) = 0;
    virtual std::unique_ptr<User> loadUser(const std::string& id) = 0;
    virtual void saveTrader(const Trader& t) = 0;
    virtual void recordTrade(const std::string& userId, const Order& o) = 0;
    virtual json listUsers() = 0;
    virtual json listTrades(const std::string& userId = "") = 0;
    virtual void setBalance(const std::string& id, double balance) = 0;
};

// ================= Yahoo Finance =================
class YahooMarketData : public IMarketData {
    HttpsClient http_{"query1.finance.yahoo.com"};
    TtlCache<std::string, Quote> cache_{std::chrono::seconds(30)};   // generic class template
public:
    Quote getQuote(const std::string& s) override {
        if (auto hit = cache_.get(s)) return *hit;
        auto r = http_.send("GET", "/v8/finance/chart/" + s + "?interval=1d&range=1d",
                            {{"User-Agent", "Mozilla/5.0"}});
        json j = r.asJson();
        if (r.status != 200 || j.is_discarded() || !j.contains("chart") ||
            !j["chart"].contains("result") || j["chart"]["result"].is_null())
            throw UnknownSymbol("Unknown symbol: " + s);
        const json& m = j["chart"]["result"][0]["meta"];
        Quote q;
        q.symbol = s;
        q.name = m.value("shortName", m.value("longName", s));
        q.currency = m.value("currency", "USD");
        q.price = m.value("regularMarketPrice", 0.0);
        q.prevClose = m.value("chartPreviousClose", 0.0);
        if (q.price <= 0) throw TradeError("No live price for " + s);
        cache_.put(s, q);
        return q;
    }
};

// ================= Supabase Auth =================
class SupabaseAuth : public IAuthService {
    HttpsClient http_;
    std::string anon_;
    httplib::Headers hdr(const std::string& bearer = "") const {
        return {{"apikey", anon_}, {"Authorization", "Bearer " + (bearer.empty() ? anon_ : bearer)}};
    }
public:
    SupabaseAuth(const std::string& url, std::string anon) : http_(hostOf(url)), anon_(std::move(anon)) {}
    std::string signUp(const std::string& e, const std::string& p) override {
        auto r = http_.send("POST", "/auth/v1/signup", hdr(), json{{"email", e}, {"password", p}}.dump());
        json j = r.asJson();
        if (r.status >= 400) throw AuthError(j.value("msg", j.value("error_description", "Sign-up failed")));
        return j.contains("user") ? j["user"]["id"].get<std::string>() : j["id"].get<std::string>();
    }
    json signIn(const std::string& e, const std::string& p) override {
        auto r = http_.send("POST", "/auth/v1/token?grant_type=password", hdr(), json{{"email", e}, {"password", p}}.dump());
        json j = r.asJson();
        if (r.status >= 400) throw AuthError("Wrong email or password");
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
        httplib::Headers h{{"apikey", key_}, {"Authorization", "Bearer " + key_}};
        if (!prefer.empty()) h.emplace("Prefer", prefer);
        return h;
    }
    json call(const std::string& m, const std::string& path, const std::string& body = "",
              const std::string& prefer = "") const {
        auto r = http_.send(m, "/rest/v1/" + path, hdr(prefer), body);
        if (r.status >= 400) throw std::runtime_error("Database error: " + r.body);
        return r.body.empty() ? json::array() : r.asJson();
    }
public:
    SupabaseRepository(const std::string& url, std::string serviceKey)
        : http_(hostOf(url)), key_(std::move(serviceKey)) {}

    void createProfile(const std::string& id, const std::string& email) override {
        call("POST", "profiles", json{{"id", id}, {"email", email}}.dump(), "return=minimal");
    }
    std::unique_ptr<User> loadUser(const std::string& id) override {
        json p = call("GET", "profiles?id=eq." + id + "&select=*");
        if (!p.is_array() || p.empty()) throw AuthError("Profile not found");
        const json& row = p[0];
        if (row["role"] == "admin") return std::make_unique<Admin>(id, row["email"]);
        auto t = std::make_unique<Trader>(id, row["email"], row["balance"].get<double>());
        for (auto& h : call("GET", "holdings?user_id=eq." + id + "&select=*"))
            t->portfolio().set({h["symbol"], h["qty"], h["avg_price"].get<double>()});
        return t;
    }
    void saveTrader(const Trader& t) override {
        call("PATCH", "profiles?id=eq." + t.id(), json{{"balance", t.balance()}}.dump(), "return=minimal");
        call("DELETE", "holdings?user_id=eq." + t.id());
        json rows = json::array();
        for (auto& [sym, h] : t.portfolio().all())
            rows.push_back({{"user_id", t.id()}, {"symbol", sym}, {"qty", h.qty}, {"avg_price", h.avgPrice}});
        if (!rows.empty()) call("POST", "holdings", rows.dump(), "return=minimal");
    }
    void recordTrade(const std::string& uid, const Order& o) override {
        call("POST", "trades", json{{"user_id", uid}, {"symbol", o.symbol()}, {"side", o.side()},
                                    {"qty", o.qty()}, {"price", o.price()}}.dump(), "return=minimal");
    }
    json listUsers() override { return call("GET", "profiles?select=id,email,role,balance&order=created_at.desc"); }
    json listTrades(const std::string& uid) override {
        std::string f = uid.empty() ? "" : "&user_id=eq." + uid;
        return call("GET", "trades?select=*&order=created_at.desc&limit=100" + f);
    }
    void setBalance(const std::string& id, double b) override {
        call("PATCH", "profiles?id=eq." + id, json{{"balance", b}}.dump(), "return=minimal");
    }
};
