// Stockify domain model (matches the class diagram). Pure OOP, no networking.
#pragma once
#include <ctime>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include "core.hpp"
using json = nlohmann::json;

// ---------- Exceptions (base + derived) ----------
struct TradeError : std::runtime_error { using std::runtime_error::runtime_error; };
struct AuthError  : std::runtime_error { using std::runtime_error::runtime_error; };
struct InsufficientFunds : TradeError { using TradeError::TradeError; };
struct UnknownSymbol     : TradeError { using TradeError::TradeError; };
struct ValidationError   : TradeError { using TradeError::TradeError; };

// ---------- Enums ----------
enum class OrderType { BUY, SELL };
enum class OrderStatus { PENDING, EXECUTED, CANCELLED, REJECTED };
inline std::string toString(OrderType t) { return t == OrderType::BUY ? "BUY" : "SELL"; }
inline std::string toString(OrderStatus s) {
    switch (s) {
        case OrderStatus::PENDING:   return "PENDING";
        case OrderStatus::EXECUTED:  return "EXECUTED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED:  return "REJECTED";
    }
    return "?";
}
inline OrderType parseOrderType(const std::string& s) {
    if (s == "BUY") return OrderType::BUY;
    if (s == "SELL") return OrderType::SELL;
    throw ValidationError("Side must be BUY or SELL");
}

struct Quote { std::string symbol, name, currency; double price = 0, prevClose = 0; };  // raw market data
struct Candle { long long time = 0; double close = 0; };                               // one point on the price chart

// ---------- Stock ----------
class Stock {
    std::string symbol_, companyName_, currency_;
    double currentPrice_, previousPrice_;
public:
    Stock(std::string symbol = "", std::string name = "", double cur = 0, double prev = 0, std::string currency = "INR")
        : symbol_(std::move(symbol)), companyName_(std::move(name)), currency_(std::move(currency)),
          currentPrice_(cur), previousPrice_(prev) {}
    void updatePrice(double p) noexcept { previousPrice_ = currentPrice_; currentPrice_ = p; }
    double getPrice() const noexcept { return currentPrice_; }
    double previousPrice() const noexcept { return previousPrice_; }
    double changePct() const noexcept { return previousPrice_ > 0 ? (currentPrice_ - previousPrice_) / previousPrice_ * 100 : 0; }
    const std::string& symbol() const noexcept { return symbol_; }
    const std::string& companyName() const noexcept { return companyName_; }
    const std::string& currency() const noexcept { return currency_; }
};

// ---------- Holding / Portfolio (composition) ----------
class Holding {
    std::string symbol_; int quantity_; double averagePrice_;
public:
    Holding(std::string s = "", int q = 0, double avg = 0) : symbol_(std::move(s)), quantity_(q), averagePrice_(avg) {}
    void increaseQuantity(int amount, double price) {
        averagePrice_ = (averagePrice_ * quantity_ + price * amount) / (quantity_ + amount);
        quantity_ += amount;
    }
    void decreaseQuantity(int amount) {
        if (amount > quantity_) throw TradeError("You don't own enough shares of " + symbol_);
        quantity_ -= amount;
    }
    double calculateValue(double currentPrice) const noexcept { return quantity_ * currentPrice; }
    const std::string& symbol() const noexcept { return symbol_; }
    int quantity() const noexcept { return quantity_; }
    double averagePrice() const noexcept { return averagePrice_; }
};

class Portfolio {
    int portfolioId_;
    std::map<std::string, Holding> holdings_;
public:
    explicit Portfolio(int id = 0) : portfolioId_(id) {}
    void addHolding(const Holding& h) {
        auto it = holdings_.find(h.symbol());
        if (it == holdings_.end()) holdings_[h.symbol()] = h;
        else it->second.increaseQuantity(h.quantity(), h.averagePrice());
    }
    void removeHolding(const std::string& symbol, int quantity) {
        auto it = holdings_.find(symbol);
        if (it == holdings_.end()) throw TradeError("You don't own any shares of " + symbol);
        it->second.decreaseQuantity(quantity);
        if (it->second.quantity() == 0) holdings_.erase(it);
    }
    double calculateValue(const std::map<std::string, double>& prices) const {
        double total = 0;
        for (const auto& [sym, h] : holdings_) {
            auto p = prices.find(sym);
            total += h.calculateValue(p == prices.end() ? h.averagePrice() : p->second);
        }
        return total;
    }
    const std::map<std::string, Holding>& holdings() const noexcept { return holdings_; }
    int id() const noexcept { return portfolioId_; }
};

// ---------- Account (uses the Money value type) ----------
class Account {
    int accountId_;
    Money cash_;
public:
    explicit Account(int id = 0, double cash = 0) : accountId_(id), cash_(cash) {}
    void deposit(double amount) { cash_ += Money(amount); }
    bool hasSufficientCash(double amount) const noexcept { return cash_.value() + 1e-9 >= amount; }
    bool deductCash(double amount) {
        if (!hasSufficientCash(amount)) return false;
        cash_ = cash_ - Money(amount);
        return true;
    }
    double cashBalance() const noexcept { return cash_.value(); }
    int id() const noexcept { return accountId_; }
};

// ---------- Transaction: multiple + virtual (diamond) inheritance ----------
class Transaction : public Loggable, public Serializable {
    static inline std::atomic<long long> next_{0};          // static data member
    long long id_;
    OrderType type_; std::string symbol_; int quantity_; double executionPrice_;
public:
    Transaction(OrderType t, std::string s, int q, double p)
        : id_(++next_), type_(t), symbol_(std::move(s)), quantity_(q), executionPrice_(p) {}
    double getValue() const noexcept { return quantity_ * executionPrice_; }
    long long id() const noexcept { return id_; }
    OrderType type() const noexcept { return type_; }
    const std::string& symbol() const noexcept { return symbol_; }
    int quantity() const noexcept { return quantity_; }
    double executionPrice() const noexcept { return executionPrice_; }
    std::string toLogLine() const override {
        std::ostringstream o;
        o << at() << " " << toString(type_) << " " << quantity_ << " " << symbol_ << " @ " << Formatter<double>::fmt(executionPrice_)
          << " = " << Formatter<double>::fmt(getValue());
        return o.str();
    }
    std::string toCsv() const override { return join(",", at(), toString(type_), symbol_, quantity_, executionPrice_); }
};

// ---------- Alerts and notifications ----------
class PriceAlert {
    int id_; std::string ownerId_, symbol_; double target_; bool above_, triggered_;
public:
    PriceAlert(int id, std::string owner, std::string symbol, double target, bool above, bool triggered = false)
        : id_(id), ownerId_(std::move(owner)), symbol_(std::move(symbol)), target_(target), above_(above), triggered_(triggered) {}
    bool check(double currentPrice) {                        // true once, when the target is reached
        if (triggered_) return false;
        bool hit = above_ ? currentPrice >= target_ : currentPrice <= target_;
        if (hit) triggered_ = true;
        return hit;
    }
    int id() const noexcept { return id_; }
    const std::string& ownerId() const noexcept { return ownerId_; }
    const std::string& symbol() const noexcept { return symbol_; }
    double target() const noexcept { return target_; }
    bool above() const noexcept { return above_; }
    bool triggered() const noexcept { return triggered_; }
};

class Notification {
    int id_; std::string userId_, message_, createdAt_; bool read_;
public:
    Notification(int id, std::string user, std::string msg, bool read = false, std::string createdAt = "")
        : id_(id), userId_(std::move(user)), message_(std::move(msg)), createdAt_(std::move(createdAt)), read_(read) {}
    void markAsRead() noexcept { read_ = true; }
    int id() const noexcept { return id_; }
    const std::string& userId() const noexcept { return userId_; }
    const std::string& message() const noexcept { return message_; }
    const std::string& createdAt() const noexcept { return createdAt_; }
    bool read() const noexcept { return read_; }
};

// ---------- Users: abstraction + inheritance ----------
class User {
protected:
    std::string id_, email_;
public:
    User(std::string id, std::string email) : id_(std::move(id)), email_(std::move(email)) {}
    virtual ~User() = default;
    virtual std::string role() const = 0;
    virtual bool canTrade() const = 0;
    const std::string& id() const noexcept { return id_; }
    const std::string& email() const noexcept { return email_; }
};

class Order;
class Trader : public User {
    Account account_;
    Portfolio portfolio_;
public:
    Trader(std::string id, std::string email, double cash) : User(std::move(id), std::move(email)), account_(0, cash) {}
    std::string role() const override { return "user"; }
    bool canTrade() const override { return true; }
    Account& account() noexcept { return account_; }
    const Account& account() const noexcept { return account_; }
    double balance() const noexcept { return account_.cashBalance(); }
    const Portfolio& portfolio() const noexcept { return portfolio_; }
    Portfolio& portfolio() { return const_cast<Portfolio&>(std::as_const(*this).portfolio()); }   // const_cast idiom
    const Portfolio& viewPortfolio() const noexcept { return portfolio_; }
    std::unique_ptr<Order> placeBuyOrder(const Stock& s, int quantity);
    std::unique_ptr<Order> placeSellOrder(const Stock& s, int quantity);
    PriceAlert setPriceAlert(const Stock& s, double price) const {
        return PriceAlert(0, id_, s.symbol(), price, price > s.getPrice());
    }
};

class Admin : public User {
public:
    using User::User;
    std::string role() const override { return "admin"; }
    bool canTrade() const override { return false; }
};

// ---------- Orders: polymorphism (execute differs per subclass) ----------
class Order {
    static inline std::atomic<int> created_{0};
protected:
    int orderId_; OrderType type_; std::string symbol_; int quantity_; double price_;
    OrderStatus status_ = OrderStatus::PENDING; std::time_t timestamp_;
public:
    Order(OrderType t, std::string s, int q, double p)
        : orderId_(++created_), type_(t), symbol_(std::move(s)), quantity_(q), price_(p), timestamp_(std::time(nullptr)) {}
    virtual ~Order() = default;
    bool validate() const noexcept { return quantity_ > 0 && price_ > 0 && !symbol_.empty(); }
    virtual Transaction execute(Trader& t) = 0;               // returns the Transaction
    void cancel() noexcept { if (status_ == OrderStatus::PENDING) status_ = OrderStatus::CANCELLED; }
    void reject() noexcept { status_ = OrderStatus::REJECTED; }
    int orderId() const noexcept { return orderId_; }
    OrderType type() const noexcept { return type_; }
    OrderStatus status() const noexcept { return status_; }
    const std::string& symbol() const noexcept { return symbol_; }
    int quantity() const noexcept { return quantity_; }
    double price() const noexcept { return price_; }
    Money total() const { return Money(price_) * quantity_; }
    static int created() { return created_; }                 // static member function
    static std::unique_ptr<Order> create(OrderType t, const std::string& s, int q, double p);   // factory
};

class BuyOrder : public Order {
public:
    BuyOrder(const std::string& s, int q, double p) : Order(OrderType::BUY, s, q, p) {}
    Transaction execute(Trader& t) override {
        double cost = static_cast<double>(total());
        if (!t.account().hasSufficientCash(cost)) throw InsufficientFunds("Not enough virtual cash for this order");
        t.account().deductCash(cost);
        t.portfolio().addHolding(Holding(symbol_, quantity_, price_));
        status_ = OrderStatus::EXECUTED;
        return Transaction(type_, symbol_, quantity_, price_);
    }
};

class SellOrder : public Order {
public:
    SellOrder(const std::string& s, int q, double p) : Order(OrderType::SELL, s, q, p) {}
    Transaction execute(Trader& t) override {
        t.portfolio().removeHolding(symbol_, quantity_);
        t.account().deposit(static_cast<double>(total()));
        status_ = OrderStatus::EXECUTED;
        return Transaction(type_, symbol_, quantity_, price_);
    }
};

inline std::unique_ptr<Order> Order::create(OrderType t, const std::string& s, int q, double p) {
    if (t == OrderType::BUY) return std::make_unique<BuyOrder>(s, q, p);
    return std::make_unique<SellOrder>(s, q, p);
}
inline std::unique_ptr<Order> Trader::placeBuyOrder(const Stock& s, int q)  { return Order::create(OrderType::BUY, s.symbol(), q, s.getPrice()); }
inline std::unique_ptr<Order> Trader::placeSellOrder(const Stock& s, int q) { return Order::create(OrderType::SELL, s.symbol(), q, s.getPrice()); }
