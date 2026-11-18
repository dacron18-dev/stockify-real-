// Stockify API server (C++20). Routes are thin; all rules live in the OOP classes.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <future>
#include "services.hpp"
#include "threads.hpp"
#include "files.hpp"

static std::string cleanSymbol(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    bool ok = !s.empty() && s.size() <= 15 &&
              std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '^' || c == '='; });
    if (!ok) throw TradeError("Enter a valid ticker, e.g. AAPL or RELIANCE.NS");
    return s;
}
static void reply(httplib::Response& rs, int code, const json& j) { rs.status = code; rs.set_content(j.dump(), "application/json"); }
static json bodyOf(const httplib::Request& rq) {
    json j = json::parse(rq.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw TradeError("Invalid request body");
    return j;
}

static AuditRecord makeRecord(const Order& o) {
    AuditRecord r{};
    std::strncpy(r.symbol, o.symbol().c_str(), sizeof r.symbol - 1);
    r.side = o.side()[0];
    r.qty = o.qty();
    r.price = o.price();
    r.ts = static_cast<long long>(o.at());
    return r;
}

int main() {
    YahooMarketData market;
    SupabaseAuth auth(env("SUPABASE_URL"), env("SUPABASE_ANON_KEY"));
    SupabaseRepository repo(env("SUPABASE_URL"), env("SUPABASE_SERVICE_KEY"));
    const double START_CASH = 100000.0;

    // Logging and background work
    FileLogger logger("stockify.log");
    AuditFile audit("stockify.audit");
    ThreadPool bg(1);                                   // one writer thread: producer-consumer queue
    std::atomic<int> tradeCount{0};
    const std::vector<std::string> watchlist{"AAPL", "MSFT", "GOOGL", "TSLA", "NVDA"};
    PeriodicTask refresher([&] {                        // keeps popular prices warm in the cache
        for (auto& s : watchlist) { try { market.getQuote(s); } catch (...) {} }
    }, 25000);
    Thread refreshThread(refresher);
    refreshThread.start();

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
    // Wraps every route with the same error handling.
    auto guard = [](auto fn) {
        return [fn](const httplib::Request& rq, httplib::Response& rs) {
            try { fn(rq, rs); }
            catch (const AuthError& e)  { reply(rs, 401, {{"error", e.what()}}); }
            catch (const InsufficientFunds& e) { reply(rs, 402, {{"error", e.what()}}); }   // derived first
            catch (const TradeError& e) { reply(rs, 400, {{"error", e.what()}}); }          // then base
            catch (const std::exception& e) { reply(rs, 500, {{"error", e.what()}}); }
            catch (...) { reply(rs, 500, {{"error", "Unexpected error"}}); }                // catch-all
        };
    };

    httplib::Server svr;
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
                             {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
    svr.Options(".*", [](const httplib::Request&, httplib::Response& rs) { rs.status = 204; });
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& rs) { reply(rs, 200, {{"ok", true}}); });

    svr.Post("/api/signup", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        json b = bodyOf(rq);
        std::string email = b.value("email", ""), pw = b.value("password", "");
        if (email.empty() || pw.size() < 6) throw TradeError("Use a valid email and a password of 6+ characters");
        std::string id = auth.signUp(email, pw);
        repo.createProfile(id, email);
        json s = auth.signIn(email, pw);
        reply(rs, 200, {{"token", s["token"]}, {"role", "user"}, {"email", email}});
    }));

    svr.Post("/api/login", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        json b = bodyOf(rq);
        json s = auth.signIn(b.value("email", ""), b.value("password", ""));
        auto u = repo.loadUser(s["id"].get<std::string>());
        reply(rs, 200, {{"token", s["token"]}, {"role", u->role()}, {"email", u->email()}});
    }));

    svr.Get("/api/quote", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        authed(rq);
        reply(rs, 200, market.getQuote(cleanSymbol(rq.get_param_value("symbol"))).toJson());
    }));

    svr.Get("/api/portfolio", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        Trader& t = trader(u);
        // Fetch all quotes in parallel (futures); worker exceptions resurface at fut.get().
        std::vector<std::pair<Holding, std::future<double>>> jobs;
        for (auto& [sym, h] : t.portfolio().all())
            jobs.emplace_back(h, std::async(std::launch::async, [&market, sym = sym] { return market.getQuote(sym).price; }));
        json rows = json::array();
        Money invested;
        for (auto& [h, fut] : jobs) {
            double px = fut.get();
            invested += Money(px) * h.qty;
            rows.push_back({{"symbol", h.symbol}, {"qty", h.qty}, {"avgPrice", h.avgPrice}, {"price", px},
                            {"value", px * h.qty}, {"pnl", (px - h.avgPrice) * h.qty}});
        }
        Money equity = Money(t.balance()) + invested;
        reply(rs, 200, {{"email", t.email()}, {"balance", t.balance()}, {"holdings", rows},
                        {"equity", static_cast<double>(equity)}, {"startCash", START_CASH}});
    }));

    svr.Post("/api/trade", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        Trader& t = trader(u);
        json b = bodyOf(rq);
        std::string sym = cleanSymbol(b.value("symbol", ""));
        Quote q = market.getQuote(sym);                       // server decides the price
        auto order = Order::create(b.value("side", ""), sym, b.value("qty", 0), q.price);
        order->execute(t);                                    // polymorphic call
        repo.saveTrader(t);
        repo.recordTrade(t.id(), *order);
        ++tradeCount;
        bg.submit([&logger, &audit, line = order->toLogLine(), rec = makeRecord(*order)] {
            logger.write(line);                         // text log
            audit.append(rec);                          // binary audit file
        });
        reply(rs, 200, {{"message", order->side() + " " + std::to_string(order->qty()) + " " + sym + " filled"},
                        {"balance", t.balance()}});
    }));

    svr.Get("/api/trades", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq);
        reply(rs, 200, repo.listTrades(trader(u).id()));
    }));

    svr.Get("/api/admin/users", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        reply(rs, 200, repo.listUsers());
    }));
    svr.Get("/api/admin/trades", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        reply(rs, 200, repo.listTrades());
    }));
    svr.Post("/api/admin/reset", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        repo.setBalance(bodyOf(rq).value("id", ""), START_CASH);
        reply(rs, 200, {{"ok", true}});
    }));

    svr.Get("/api/watchlist", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        authed(rq);
        std::vector<std::future<json>> fs;
        for (auto& s : watchlist)
            fs.push_back(std::async(std::launch::async, [&market, s] {
                try { return market.getQuote(s).toJson(); } catch (...) { return json(nullptr); }
            }));
        json out = json::array();
        for (auto& f : fs) { json q = f.get(); if (!q.is_null()) out.push_back(q); }
        reply(rs, 200, out);
    }));

    svr.Get("/api/admin/stats", guard([&](const httplib::Request& rq, httplib::Response& rs) {
        auto u = authed(rq); adminOnly(u);
        std::vector<double> sizes;                      // read binary file with random access
        for (std::size_t i = 0, n = audit.count(); i < n; ++i)
            if (auto r = audit.read(i)) sizes.push_back(r->qty * r->price);
        reply(rs, 200, {{"tradesThisSession", tradeCount.load()}, {"ordersCreated", Order::created()},
                        {"auditRecords", audit.count()}, {"avgTradeValue", average(sizes)},
                        {"recentLogs", logger.recent()}});
    }));

    int port = env("PORT").empty() ? 8080 : std::stoi(env("PORT"));
    svr.listen("0.0.0.0", port);
    refresher.stop();                                   // clean shutdown: stop, then join
    refreshThread.join();
}
