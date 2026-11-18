// Stockify API server (C++20). Routes are thin; the rules live in the OOP classes.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <future>
#include "services.hpp"
#include "engine.hpp"
#include "files.hpp"

// Indian stocks only: NSE (.NS) or BSE (.BO). "TCS" and "tcs" both become "TCS.NS".
static std::string cleanSymbol(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    auto endsWith = [&](const std::string& suf) { return s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; };
    std::string base = s;
    std::string suffix = ".NS";                                   // NSE by default
    if (endsWith(".NS")) base = s.substr(0, s.size() - 3);
    else if (endsWith(".BO")) { base = s.substr(0, s.size() - 3); suffix = ".BO"; }
    // base name: letters, digits, '&' and '-' only (e.g. M&M, BAJAJ-AUTO, TCS)
    bool ok = !base.empty() && base.size() <= 20 &&
              std::all_of(base.begin(), base.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '&' || c == '-'; });
    if (!ok) throw ValidationError("Enter an Indian stock symbol, e.g. RELIANCE, TCS or INFY.NS (NSE/BSE only)");
    return base + suffix;
}
static void reply(httplib::Response& rs, int code, const json& j) { rs.status = code; rs.set_content(j.dump(), "application/json"); }
static json bodyOf(const httplib::Request& rq) {
    json j = json::parse(rq.body.empty() ? "{}" : rq.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw ValidationError("Invalid request body");
    return j;
}
static json stockJson(const Stock& s) {
    return {{"symbol", s.symbol()}, {"name", s.companyName()}, {"currency", s.currency()}, {"price", s.getPrice()},
            {"prevClose", s.previousPrice()}, {"changePct", s.changePct()}};
}
static json alertJson(const PriceAlert& a) {
    return {{"id", a.id()}, {"symbol", a.symbol()}, {"target", a.target()}, {"above", a.above()}, {"triggered", a.triggered()}};
}
static json noteJson(const Notification& n) {
    return {{"id", n.id()}, {"message", n.message()}, {"read", n.read()}, {"createdAt", n.createdAt()}};
}
static AuditRecord makeRecord(const Transaction& t) {
    AuditRecord r{};
    std::strncpy(r.symbol, t.symbol().c_str(), sizeof r.symbol - 1);
    r.side = toString(t.type())[0];
    r.qty = t.quantity();
    r.price = t.executionPrice();
    r.ts = static_cast<long long>(t.at());
    return r;
}

int main() {
    const double START_CASH = 1000000.0;   // Rs 10,00,000 virtual cash
    // 30 large-cap Nifty 50 stocks shown in the market table (any other NSE/BSE symbol can still be searched)
    const std::vector<std::string> watchlist{
        "RELIANCE.NS", "TCS.NS", "HDFCBANK.NS", "ICICIBANK.NS", "INFY.NS",
        "HINDUNILVR.NS", "ITC.NS", "SBIN.NS", "BHARTIARTL.NS", "KOTAKBANK.NS",
        "LT.NS", "AXISBANK.NS", "ASIANPAINT.NS", "MARUTI.NS", "SUNPHARMA.NS",
        "TITAN.NS", "BAJFINANCE.NS", "HCLTECH.NS", "NTPC.NS", "ULTRACEMCO.NS",
        "ONGC.NS", "WIPRO.NS", "POWERGRID.NS", "M&M.NS", "TATASTEEL.NS",
        "JSWSTEEL.NS", "ADANIPORTS.NS", "COALINDIA.NS", "NESTLEIND.NS", "TECHM.NS"};

    YahooMarketData yahoo;
    SupabaseAuth auth(env("SUPABASE_URL"), env("SUPABASE_ANON_KEY"));
    SupabaseRepository repo(env("SUPABASE_URL"), env("SUPABASE_SERVICE_KEY"));
    Market market(1, "Indian Equities (NSE/BSE)", yahoo);
    TransactionManager txm(repo);
    OrderManager orders(market, txm, repo);
    AlertManager alerts(repo, market);
    Simulation sim(1, 45000, market, alerts);            // background thread: refresh prices + check alerts
    for (auto& s : watchlist) market.addStock(Stock(s));
    sim.start();

    FileLogger logger("stockify.log");
    AuditFile audit("stockify.audit");
    ThreadPool bg(1);                                    // single writer thread (producer-consumer queue)
    std::atomic<int> tradeCount{0};

    auto authed = [&](const httplib::Request& rq) {
        std::string h = rq.get_header_value("Authorization");
        if (h.rfind("Bearer ", 0) != 0) throw AuthError("Please log in");
        return repo.loadUser(auth.verify(h.substr(7)));
    };
    auto trader = [&](std::unique_ptr<User>& u) -> Trader& {
        auto* t = dynamic_cast<Trader*>(u.get());
        if (!t) throw AuthError("This action is for trader accounts");
        return *t;
    };
    auto adminOnly = [&](std::unique_ptr<User>& u) {
        if (!dynamic_cast<Admin*>(u.get())) throw AuthError("Admins only");
    };
    auto guard = [](auto fn) {
        return [fn](const httplib::Request& rq, httplib::Response& rs) {
            try { fn(rq, rs); }
            catch (const AuthError& e)         { reply(rs, 401, {{"error", e.what()}}); }
            catch (const InsufficientFunds& e) { reply(rs, 402, {{"error", e.what()}}); }   // derived first
            catch (const TradeError& e)        { reply(rs, 400, {{"error", e.what()}}); }   // then base
            catch (const std::exception& e)    { reply(rs, 500, {{"error", e.what()}}); }
            catch (...)                        { reply(rs, 500, {{"error", "Unexpected error"}}); }
        };
    };

    httplib::Server svr;
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
                             {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
    svr.Options(".*", [](const httplib::Request&, httplib::Response& rs) { rs.status = 204; });
    svr.Get("/", [](const httplib::Request&, httplib::Response& rs) { rs.set_content("Stockify API is running", "text/plain"); });
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& rs) { reply(rs, 200, {{"ok", true}}); });

    // ---- auth ----
    svr.Post("/api/signup", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        json b = bodyOf(rq);
        std::string email = b.value("email", ""), pw = b.value("password", "");
        if (email.empty() || pw.size() < 6) throw ValidationError("Use a valid email and a password of 6+ characters");
        std::string id = auth.signUp(email, pw);
        repo.createProfile(id, email);
        json s = auth.signIn(email, pw);
        reply(rs, 200, {{"token", s["token"]}, {"role", "user"}, {"email", email}});
    }));
    svr.Post("/api/login", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        json b = bodyOf(rq);
        json s = auth.signIn(b.value("email", ""), b.value("password", ""));
        repo.createProfile(s["id"].get<std::string>(), b.value("email", ""));   // self-heals a missing profile row
        auto u = repo.loadUser(s["id"].get<std::string>());
        reply(rs, 200, {{"token", s["token"]}, {"role", u->role()}, {"email", u->email()}});
    }));

    // ---- trading ----
    svr.Get("/api/quote", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        authed(rq);
        reply(rs, 200, stockJson(market.getStock(cleanSymbol(rq.get_param_value("symbol")))));
    }));
    svr.Get("/api/history", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        authed(rq);
        std::string sym = cleanSymbol(rq.get_param_value("symbol"));
        std::string range = rq.has_param("range") ? rq.get_param_value("range") : "1mo";
        json pts = json::array();
        for (const Candle& c : yahoo.getHistory(sym, range)) pts.push_back({{"t", c.time}, {"c", c.close}});
        reply(rs, 200, {{"symbol", sym}, {"range", range}, {"points", pts}});
    }));
    svr.Get("/api/watchlist", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        authed(rq);
        std::vector<std::future<json>> fs;
        for (auto& s : watchlist)
            fs.push_back(std::async(std::launch::async, [&market, s] {
                try { return stockJson(market.getStock(s)); } catch (...) { return json(nullptr); }
            }));
        json out = json::array();
        for (auto& f : fs) { json q = f.get(); if (!q.is_null()) out.push_back(q); }
        reply(rs, 200, out);
    }));
    svr.Get("/api/portfolio", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        Trader& t = trader(u);
        std::vector<std::pair<std::string, std::future<double>>> jobs;     // quotes fetched in parallel
        for (auto& [sym, h] : t.viewPortfolio().holdings())
            jobs.emplace_back(sym, std::async(std::launch::async, [&market, sym = sym] { return market.getStock(sym).getPrice(); }));
        std::map<std::string, double> prices;
        for (auto& [sym, fut] : jobs) prices[sym] = fut.get();             // worker exceptions resurface here
        json rows = json::array();
        for (auto& [sym, h] : t.viewPortfolio().holdings()) {
            double px = prices[sym];
            rows.push_back({{"symbol", sym}, {"qty", h.quantity()}, {"avgPrice", h.averagePrice()}, {"price", px},
                            {"value", h.calculateValue(px)}, {"pnl", (px - h.averagePrice()) * h.quantity()}});
        }
        Money equity = Money(t.balance()) + Money(t.viewPortfolio().calculateValue(prices));
        reply(rs, 200, {{"email", t.email()}, {"balance", t.balance()}, {"holdings", rows},
                        {"equity", static_cast<double>(equity)}, {"startCash", START_CASH}});
    }));
    svr.Post("/api/trade", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        Trader& t = trader(u);
        json b = bodyOf(rq);
        Transaction tx = orders.placeOrder(t, parseOrderType(b.value("side", "")), cleanSymbol(b.value("symbol", "")), b.value("qty", 0));
        ++tradeCount;
        bg.submit([&logger, &audit, line = tx.toLogLine(), rec = makeRecord(tx)] { logger.write(line); audit.append(rec); });
        reply(rs, 200, {{"message", toString(tx.type()) + " " + std::to_string(tx.quantity()) + " " + tx.symbol() + " filled"},
                        {"balance", t.balance()}});
    }));
    svr.Get("/api/trades", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        reply(rs, 200, repo.listTransactions(trader(u).id()));
    }));

    // ---- price alerts and notifications ----
    svr.Post("/api/alerts", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        Trader& t = trader(u);
        json b = bodyOf(rq);
        double price = b.value("price", 0.0);
        if (price <= 0) throw ValidationError("Enter a target price above 0");
        PriceAlert a = t.setPriceAlert(market.getStock(cleanSymbol(b.value("symbol", ""))), price);
        reply(rs, 200, {{"id", repo.saveAlert(a)}, {"message", "Alert set for " + a.symbol() + " at " + Formatter<double>::fmt(price)}});
    }));
    svr.Get("/api/alerts", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        json out = json::array();
        for (auto& a : repo.alertsFor(trader(u).id())) out.push_back(alertJson(a));
        reply(rs, 200, out);
    }));
    svr.Get("/api/notifications", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        json out = json::array();
        for (auto& n : repo.notifications(trader(u).id())) out.push_back(noteJson(n));
        reply(rs, 200, out);
    }));
    svr.Post("/api/notifications/read", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        repo.markNotificationRead(bodyOf(rq).value("id", 0), trader(u).id());
        reply(rs, 200, {{"ok", true}});
    }));

    // ---- admin ----
    svr.Get("/api/admin/users", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        reply(rs, 200, repo.listUsers());
    }));
    svr.Get("/api/admin/trades", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        reply(rs, 200, repo.listTransactions());
    }));
    svr.Post("/api/admin/reset", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        repo.setBalance(bodyOf(rq).value("id", ""), START_CASH);
        reply(rs, 200, {{"ok", true}});
    }));
    svr.Post("/api/admin/event", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        std::string sym = bodyOf(rq).value("symbol", "");
        sym = sym.empty() ? watchlist[std::chrono::steady_clock::now().time_since_epoch().count() % watchlist.size()] : cleanSymbol(sym);
        MarketEvent e = sim.generateMarketEvent();
        market.processEvent(e, sym);
        reply(rs, 200, {{"symbol", sym}, {"eventType", e.eventType()}, {"impactPct", e.impactFactor() * 100}});
    }));
    svr.Post("/api/admin/simulation", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        if (bodyOf(rq).value("run", true)) sim.start(); else sim.stop();
        reply(rs, 200, {{"running", sim.running()}});
    }));
    svr.Get("/api/admin/stats", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        std::vector<double> sizes;                       // binary file, random access
        for (std::size_t i = 0, n = audit.count(); i < n; ++i)
            if (auto r = audit.read(i)) sizes.push_back(r->qty * r->price);
        reply(rs, 200, {{"tradesThisSession", tradeCount.load()}, {"ordersCreated", Order::created()},
                        {"auditRecords", audit.count()}, {"avgTradeValue", average(sizes)},
                        {"simulationRunning", sim.running()}, {"ticks", sim.ticks()},
                        {"marketOpen", market.isOpen()}, {"recentLogs", logger.recent()}});
    }));

    int port = env("PORT").empty() ? 8080 : std::stoi(env("PORT"));
    svr.listen("0.0.0.0", port);
    sim.stop();                                          // clean shutdown
}
