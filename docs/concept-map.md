# Syllabus concept map

Real code = used inside the trading app. Demo = `backend/demo/concepts_demo.cpp` (run it: `g++ -std=c++20 -pthread backend/demo/concepts_demo.cpp -o demo && ./demo`).

| Syllabus topic | Where |
|---|---|
| Classes, objects, access specifiers, encapsulation | `models.hpp` (`Trader` balance is private; changed only by `debit`/`credit`) |
| Abstraction, abstract classes, pure virtual | `User`, `Order`, `Loggable`, `Serializable`, `IMarketData`, `IRepository`, `IAuthService` |
| Inheritance: single, hierarchical, multilevel | `User`→`Trader`/`Admin`; `Order`→`BuyOrder`/`SellOrder` |
| Multiple, hybrid, diamond, virtual base | `Transaction : Loggable, Serializable`, both virtual-inheriting `Timestamped` (`core.hpp`, `models.hpp`) |
| Private / protected inheritance, ambiguity | Demo (`Car`, `Bike`, `Both`) |
| Runtime polymorphism, virtual, dynamic dispatch, virtual destructor | `order->execute(trader)`, `user->role()` |
| Object slicing, early vs late binding | Demo |
| Constructors: init list, default args, inherited (`using`) | `Trader`, `Money`, `Admin` |
| Copy/move ctor, deep vs shallow copy, destructor order | Demo (`Buffer`) |
| Static data member and static function | `Order::created_`, `Order::created()`, `Order::create()` (factory), `Transaction::next_` |
| `this`, const members, friend function/class, nested class | `Money` friend `<<`; demo for the rest |
| Operator overloading (binary, unary, ++, --, <<, >>, [], (), conversion) | `Money` (`core.hpp`); `[]` and `()` in the demo |
| Templates: function, class, multi-param, non-type, specialization, partial, variadic, concepts | `average`, `TtlCache<K,V>`, `RingBuffer<T,N>`, `Formatter<T>`, `join(...)`, `Numeric` (`core.hpp`) |
| Smart pointers | `unique_ptr<User>`, `unique_ptr<Order>`; `shared_ptr`/`weak_ptr` in the demo |
| RAII | `lock_guard`, `Thread` destructor joins, `FileLogger` closes file in destructor |
| Exceptions: try/catch/throw, user-defined, base vs derived, catch-all, propagation, noexcept | `TradeError`→`InsufficientFunds`/`UnknownSymbol`, `AuthError`; guard in `main.cpp`; futures re-throw worker errors |
| "Finally" | C++ has no `finally`; RAII destructors do the job (demo `Guard`) |
| File handling: ofstream/ifstream, append, binary, seekg/tellg | `FileLogger`, `AuditFile` (`files.hpp`) |
| Multithreading: thread, join, detach, mutex, lock_guard, unique_lock, condition_variable, atomic, producer-consumer, thread pool, future/promise, async | `threads.hpp` (`Runnable`, `Thread`, `PeriodicTask`, `ThreadPool`), `Simulation` runs the market on its own thread, `/api/portfolio` (`std::async`), demo for recursive/timed mutex, deadlock, promise |
| Java-style "extend Thread / implement Runnable" | C++ equivalent: `Runnable` interface and `Thread` wrapper with `start/run/join/sleep/yield` |
| Type casting | `static_cast` (Money to double), `dynamic_cast` (User to Trader/Admin), `const_cast` (`Trader::portfolio`), `reinterpret_cast` (binary file I/O) |

Class-diagram classes: `Trader`, `Account`, `Portfolio`, `Holding`, `Order`, `Transaction`, `Stock`, `PriceAlert`, `Notification` are in `models.hpp`; `Market`, `MarketEvent`, `Simulation`, `OrderManager`, `TransactionManager`, `AlertManager` are in `engine.hpp`.
