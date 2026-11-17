// Stockify domain model: pure OOP, no networking here.
#pragma once
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ---- Custom exceptions (exception handling) ----
struct TradeError : std::runtime_error { using std::runtime_error::runtime_error; };
struct AuthError  : std::runtime_error { using std::runtime_error::runtime_error; };

// ---- Value object ----
struct Holding {
    std::string symbol;
    int qty = 0;
    double avgPrice = 0;
};

// ---- Composition: a Portfolio HAS-A collection of Holdings ----
class Portfolio {
    std::map<std::string, Holding> holdings_;  // encapsulated state
public:
    void add(const std::string& s, int qty, double price) {
        Holding& h = holdings_[s];
        h.avgPrice = (h.avgPrice * h.qty + price * qty) / (h.qty + qty);
        h.symbol = s;
        h.qty += qty;
    }
    void remove(const std::string& s, int qty) {
        auto it = holdings_.find(s);
        if (it == holdings_.end() || it->second.qty < qty)
            throw TradeError("You don't own enough shares of " + s);
        it->second.qty -= qty;
        if (it->second.qty == 0) holdings_.erase(it);
    }
    void set(const Holding& h) { holdings_[h.symbol] = h; }
    const std::map<std::string, Holding>& all() const { return holdings_; }
};

// ---- Abstraction + Inheritance: abstract base class User ----
class User {
protected:
    std::string id_, email_;
public:
    User(std::string id, std::string email) : id_(std::move(id)), email_(std::move(email)) {}
    virtual ~User() = default;
    virtual std::string role() const = 0;      // pure virtual
    virtual bool canTrade() const = 0;         // pure virtual
    const std::string& id() const { return id_; }
    const std::string& email() const { return email_; }
};

class Trader : public User {
    double balance_;
    Portfolio portfolio_;
public:
    Trader(std::string id, std::string email, double balance)
        : User(std::move(id), std::move(email)), balance_(balance) {}
    std::string role() const override { return "user"; }
    bool canTrade() const override { return true; }
    double balance() const { return balance_; }
    void debit(double amt) {
        if (amt > balance_ + 1e-9) throw TradeError("Not enough virtual cash for this order");
        balance_ -= amt;
    }
    void credit(double amt) { balance_ += amt; }
    Portfolio& portfolio() { return portfolio_; }
    const Portfolio& portfolio() const { return portfolio_; }
};

class Admin : public User {
public:
    using User::User;
    std::string role() const override { return "admin"; }
    bool canTrade() const override { return false; }
};

// ---- Polymorphism: Order::execute behaves differently per subclass ----
class Order {
protected:
    std::string symbol_;
    int qty_;
    double price_;
public:
    Order(std::string s, int q, double p) : symbol_(std::move(s)), qty_(q), price_(p) {
        if (q <= 0) throw TradeError("Quantity must be at least 1");
    }
    virtual ~Order() = default;
    virtual void execute(Trader& t) const = 0;
    virtual std::string side() const = 0;
    double total() const { return qty_ * price_; }
    const std::string& symbol() const { return symbol_; }
    int qty() const { return qty_; }
    double price() const { return price_; }
    static std::unique_ptr<Order> create(const std::string& side, const std::string& s, int q, double p);
};

class BuyOrder : public Order {
public:
    using Order::Order;
    std::string side() const override { return "BUY"; }
    void execute(Trader& t) const override {
        t.debit(total());
        t.portfolio().add(symbol_, qty_, price_);
    }
};

class SellOrder : public Order {
public:
    using Order::Order;
    std::string side() const override { return "SELL"; }
    void execute(Trader& t) const override {
        t.portfolio().remove(symbol_, qty_);
        t.credit(total());
    }
};

// Factory method
inline std::unique_ptr<Order> Order::create(const std::string& side, const std::string& s, int q, double p) {
    if (side == "BUY")  return std::make_unique<BuyOrder>(s, q, p);
    if (side == "SELL") return std::make_unique<SellOrder>(s, q, p);
    throw TradeError("Side must be BUY or SELL");
}
