// Application layer from the class and collaboration diagrams:
// Market, MarketEvent, Simulation, OrderManager, TransactionManager, AlertManager.
#pragma once
#include <algorithm>
#include <random>
#include "interfaces.hpp"
#include "threads.hpp"

class MarketEvent {
    static inline std::atomic<int> next_{0};
    int id_; std::string eventType_; double impactFactor_;
public:
    MarketEvent(std::string type, double impact) : id_(++next_), eventType_(std::move(type)), impactFactor_(impact) {}
    void apply(Stock& s) const { s.updatePrice(s.getPrice() * (1.0 + impactFactor_)); }
    int id() const noexcept { return id_; }
    const std::string& eventType() const noexcept { return eventType_; }
    double impactFactor() const noexcept { return impactFactor_; }
};

class Market {
    int marketId_; std::string name_; std::atomic<bool> open_{true};
    IMarketData& data_;
    std::map<std::string, Stock> stocks_;
    std::map<std::string, double> impact_;                    // price multiplier from market events
    mutable std::mutex m_;
    Stock fetch(const std::string& sym) {
        Quote q = data_.getQuote(sym);                         // throws UnknownSymbol for a bad ticker
        double k = 1.0;
        { std::lock_guard<std::mutex> g(m_); auto it = impact_.find(sym); if (it != impact_.end()) k = it->second; }
        return Stock(q.symbol, q.name, q.price * k, q.prevClose * k, q.currency);
    }
public:
    Market(int id, std::string name, IMarketData& d) : marketId_(id), name_(std::move(name)), data_(d) {}
    bool isOpen() const noexcept { return open_; }
    void setOpen(bool o) noexcept { open_ = o; }
    void addStock(const Stock& s) { std::lock_guard<std::mutex> g(m_); stocks_[s.symbol()] = s; }
    Stock getStock(const std::string& sym) { Stock s = fetch(sym); addStock(s); return s; }
    bool validateStock(const std::string& sym) {
        try { getStock(sym); return true; } catch (const UnknownSymbol&) { return false; }
    }
    std::vector<Stock> updatePrices() {
        std::vector<std::string> names;
        { std::lock_guard<std::mutex> g(m_); for (auto& [k, v] : stocks_) names.push_back(k); }
        std::vector<Stock> out;
        for (auto& n : names) { try { out.push_back(getStock(n)); } catch (const std::exception&) {} }
        return out;
    }
    void processEvent(const MarketEvent& e, const std::string& sym) {
        std::lock_guard<std::mutex> g(m_);
        double& k = impact_.try_emplace(sym, 1.0).first->second;
        k = std::clamp(k * (1.0 + e.impactFactor()), 0.2, 5.0);
        auto it = stocks_.find(sym);
        if (it != stocks_.end()) e.apply(it->second);
    }
    int id() const noexcept { return marketId_; }
    const std::string& name() const noexcept { return name_; }
};

class TransactionManager {
    IRepository& repo_;
public:
    explicit TransactionManager(IRepository& r) : repo_(r) {}
    Transaction createTransaction(const std::string& userId, const Transaction& tx) {   // steps 14-15
        repo_.recordTransaction(userId, tx);
        return tx;
    }
};

class OrderManager {
    Market& market_; TransactionManager& txm_; IRepository& repo_;
public:
    OrderManager(Market& m, TransactionManager& t, IRepository& r) : market_(m), txm_(t), repo_(r) {}
    // Collaboration diagram: placeBuyOrder(...) steps 5-16, with the alternative flows.
    Transaction placeOrder(Trader& trader, OrderType type, const std::string& symbol, int quantity) {
        if (!market_.isOpen()) throw TradeError("The market is closed right now");
        if (!market_.validateStock(symbol)) throw UnknownSymbol("Unknown symbol: " + symbol);   // 6-7
        Stock stock = market_.getStock(symbol);                                                // 2-3
        auto order = type == OrderType::BUY ? trader.placeBuyOrder(stock, quantity)
                                            : trader.placeSellOrder(stock, quantity);
        if (!order->validate()) { order->reject(); throw ValidationError("Invalid stock or quantity"); }
        try {
            Transaction tx = order->execute(trader);           // 8-13: funds check, deduct cash, add holding
            repo_.saveTrader(trader);
            return txm_.createTransaction(trader.id(), tx);    // 14-15
        } catch (const TradeError&) { order->reject(); throw; }   // insufficient funds -> order rejected
    }
};

class AlertManager {
    IRepository& repo_; Market& market_;
public:
    AlertManager(IRepository& r, Market& m) : repo_(r), market_(m) {}
    void checkAll() {
        for (auto& alert : repo_.activeAlerts()) {
            try {
                Stock s = market_.getStock(alert.symbol());
                if (alert.check(s.getPrice())) {
                    repo_.markTriggered(alert.id());
                    repo_.addNotification(Notification(0, alert.ownerId(),
                        join(" ", s.symbol(), "reached your target of", Formatter<double>::fmt(alert.target()),
                             "(now", Formatter<double>::fmt(s.getPrice()) + ")")));
                }
            } catch (const std::exception&) {}
        }
    }
};

// Simulation = a background thread that advances the market tick by tick.
class Simulation {
    int id_; int tickMs_; std::atomic<bool> running_{false}; std::atomic<int> ticks_{0};
    Market& market_; AlertManager& alerts_;
    PeriodicTask task_; Thread thread_;
public:
    Simulation(int id, int tickMs, Market& m, AlertManager& a)
        : id_(id), tickMs_(tickMs), market_(m), alerts_(a), task_([this] { advanceTick(); }, tickMs), thread_(task_) {}
    ~Simulation() { stop(); }
    void start() { if (running_.exchange(true)) return; task_.reset(); thread_.start(); }
    void stop() { if (!running_.exchange(false)) return; task_.stop(); thread_.join(); }
    void advanceTick() { ++ticks_; market_.updatePrices(); alerts_.checkAll(); }
    MarketEvent generateMarketEvent() {
        static const std::pair<const char*, double> kinds[] = {
            {"Strong earnings", 0.04}, {"Regulatory probe", -0.05}, {"New product launch", 0.025}, {"Supply chain delay", -0.03}};
        static std::mt19937 rng{std::random_device{}()};
        const auto& k = kinds[rng() % std::size(kinds)];
        return MarketEvent(k.first, k.second);
    }
    bool running() const noexcept { return running_; }
    int ticks() const noexcept { return ticks_; }
    int tickMs() const noexcept { return tickMs_; }
    int id() const noexcept { return id_; }
};
