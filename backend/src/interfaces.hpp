// Abstract interfaces (abstraction): the domain depends on these, not on Yahoo or Supabase.
#pragma once
#include <vector>
#include "models.hpp"

class IMarketData {
public:
    virtual ~IMarketData() = default;
    virtual Quote getQuote(const std::string& symbol) = 0;
    virtual std::vector<Candle> getHistory(const std::string& symbol, const std::string& range) = 0;   // for the price chart
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
    virtual void recordTransaction(const std::string& userId, const Transaction& tx) = 0;
    virtual json listUsers() = 0;
    virtual json listTransactions(const std::string& userId = "") = 0;
    virtual void setBalance(const std::string& id, double balance) = 0;
    virtual int saveAlert(const PriceAlert& a) = 0;
    virtual std::vector<PriceAlert> activeAlerts() = 0;
    virtual std::vector<PriceAlert> alertsFor(const std::string& userId) = 0;
    virtual void markTriggered(int alertId) = 0;
    virtual void addNotification(const Notification& n) = 0;
    virtual std::vector<Notification> notifications(const std::string& userId) = 0;
    virtual void markNotificationRead(int id, const std::string& userId) = 0;
};
