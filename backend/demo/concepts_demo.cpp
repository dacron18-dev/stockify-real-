// Stockify concepts demo: textbook topics that don't fit naturally in the trading API.
// Build: g++ -std=c++20 -pthread concepts_demo.cpp -o demo && ./demo
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std;

static void section(const char* t) { cout << "\n=== " << t << " ===\n"; }

// ---- 1. Basics: namespace, overloading, recursion, scope resolution, references, struct ----
namespace math {
    int area(int s) { return s * s; }
    int area(int w, int h) { return w * h; }                // function overloading
    long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); } // recursion
}
int gx = 10;
struct Point { int x, y; };
void swapRef(int& a, int& b) { int t = a; a = b; b = t; }  // references
void basics() {
    section("Basics");
    int gx = 5;
    int a = 1, b = 2; swapRef(a, b);
    cout << math::area(3) << " " << math::area(2, 4) << " " << math::fact(5) << " local=" << gx
         << " global=" << ::gx << " swapped=" << a << b << " P=" << Point{1, 2}.y << "\n";
}

// ---- 2. Rule of five: constructors, deep copy, move ----
class Buffer {
    int* data_; size_t n_;
public:
    explicit Buffer(size_t n = 3) : data_(new int[n]()), n_(n) { cout << " ctor\n"; }          // dynamic constructor
    Buffer(const Buffer& o) : data_(new int[o.n_]), n_(o.n_) { copy(o.data_, o.data_ + n_, data_); cout << " deep copy\n"; }
    Buffer& operator=(const Buffer& o) { if (this != &o) { Buffer t(o); swap(data_, t.data_); swap(n_, t.n_); } return *this; } // copy assignment
    Buffer(Buffer&& o) noexcept : data_(o.data_), n_(o.n_) { o.data_ = nullptr; o.n_ = 0; cout << " move ctor\n"; }
    Buffer& operator=(Buffer&& o) noexcept { if (this != &o) { delete[] data_; data_ = o.data_; n_ = o.n_; o.data_ = nullptr; o.n_ = 0; } return *this; }
    ~Buffer() { delete[] data_; cout << " dtor\n"; }
    int& operator[](size_t i) { return data_[i]; }          // subscript operator
    int operator()(int k) const { return k * (int)n_; }     // function call operator
    size_t size() const { return n_; }
};
void ctors() {
    section("Constructors / copy / move");
    Buffer a(2); a[0] = 7;
    Buffer b = a;                       // copy constructor (deep copy: b has its own memory)
    b[0] = 99; cout << " a[0]=" << a[0] << " b[0]=" << b[0] << " call=" << a(5) << "\n";
    Buffer c = std::move(a);            // move constructor
    c = b;                              // copy assignment
    cout << " end of scope: destructors run in reverse order\n";
}

// ---- 3. Class features: static, nested, friend, const, this, objects ----
class Account {
    static int count_;                  // static data member
    int bal_;
    friend class Auditor;               // friend class
    friend int peek(const Account&);    // friend function
public:
    class Receipt { public: string text() const { return "receipt"; } };   // nested class
    Account(int b = 0) : bal_(b) { ++count_; }
    Account(int b, int bonus) : Account(b + bonus) {}       // delegating constructor
    static int count() { return count_; }
    Account& deposit(int x) { bal_ += x; return *this; }    // this pointer, chaining
    int balance() const { return bal_; }                    // const member function
};
int Account::count_ = 0;
class Auditor { public: int see(const Account& a) { return a.bal_; } };
int peek(const Account& a) { return a.bal_; }
Account makeAccount() { return Account(50); }               // returning object
int total(const Account& a) { return a.balance(); }         // object as argument
void classes() {
    section("Class features");
    Account accs[3] = {Account(1), Account(2), Account(3, 4)};   // array of objects
    const Account fixed(100);                                    // const object
    accs[0].deposit(5).deposit(5);
    cout << Account::count() << " " << accs[0].balance() << " " << Auditor().see(fixed) << " " << peek(fixed)
         << " " << total(makeAccount()) << " " << Account::Receipt().text() << "\n";
}

// ---- 4. Inheritance ----
struct Base { Base() { cout << " Base ctor\n"; } virtual ~Base() { cout << " Base dtor\n"; } virtual string who() const { return "Base"; } string name() const { return "Base-nonvirtual"; } };
struct Derived : Base { Derived() { cout << " Derived ctor\n"; } ~Derived() override { cout << " Derived dtor\n"; } string who() const override { return "Derived"; } string name() const { return "Derived-nonvirtual"; } };
class Engine { protected: int hp = 100; public: int power() const { return hp; } };
class Car : private Engine { public: int speed() const { return hp * 2; } };       // private inheritance (implementation reuse)
class Bike : protected Engine {};                                                   // protected inheritance
struct Animal { int age = 3; };
struct Swimmer : virtual Animal {}; struct Runner : virtual Animal {};              // virtual base class
struct Frog : Swimmer, Runner {};                                                   // diamond solved: one Animal
struct A1 { int v = 1; }; struct B1 { int v = 2; };
struct Both : A1, B1 { int get() const { return A1::v + B1::v; } };                 // ambiguity resolved with ::
struct Walker : Frog {};                                                            // multilevel + hybrid
void inheritance() {
    section("Inheritance / polymorphism");
    { Derived d; Base* p = &d; cout << " late binding: " << p->who() << ", early binding: " << p->name() << "\n"; }
    Base sliced = Derived(); cout << " slicing keeps only Base part: " << sliced.who() << "\n";
    unique_ptr<Base> up = make_unique<Derived>(); cout << " virtual destructor frees Derived via Base*\n"; up.reset();
    Frog f; Both b; Walker w; cout << " Car speed " << Car().speed() << ", Frog age " << f.age << ", ambiguity sum " << b.get() << ", Walker age " << w.age << "\n";
}

// ---- 5. Pointers, smart pointers, RAII ----
struct Node { shared_ptr<Node> next; weak_ptr<Node> prev; ~Node() { cout << " Node freed\n"; } };
struct Item { int qty = 3; int bump() { return ++qty; } };
class Guard { public: Guard() { cout << " acquire\n"; } ~Guard() { cout << " release\n"; } };   // RAII
void pointers() {
    section("Pointers / smart pointers / RAII");
    Item it; int Item::*pm = &Item::qty; int (Item::*pf)() = &Item::bump;           // pointer to class members
    void* vp = &it; Item* ip = static_cast<Item*>(vp); int* np = nullptr;           // void and null pointers
    cout << " ptr-to-member " << it.*pm << " " << (it.*pf)() << " voidptr " << ip->qty << " null=" << (np == nullptr) << "\n";
    auto a = make_shared<Node>(); auto b = make_shared<Node>(); a->next = b; b->prev = a;   // weak_ptr breaks cycle
    cout << " use_count(a)=" << a.use_count() << " (weak_ptr does not add)\n";
    Guard g;
}

// ---- 6. Exceptions ----
struct AppError : runtime_error { using runtime_error::runtime_error; };
struct DbError : AppError { using AppError::AppError; };
void risky(int k) noexcept(false) { if (k == 1) throw DbError("db down"); if (k == 2) throw out_of_range("idx"); if (k == 3) throw 42; }
void exceptions() {
    section("Exceptions");
    for (int k = 1; k <= 3; ++k) {
        try {
            try { Guard g; risky(k); }                      // stack unwinding releases the Guard (acts like 'finally')
            catch (const DbError&) { cout << " rethrowing\n"; throw; }   // nested try + rethrow
        }
        catch (const AppError& e) { cout << " base catch: " << e.what() << "\n"; }
        catch (const exception& e) { cout << " std exception: " << e.what() << "\n"; }
        catch (...) { cout << " catch-all\n"; }
    }
}

// ---- 7. Concurrency ----
void threads() {
    section("Multithreading");
    int unsafe = 0; atomic<int> safe{0}; mutex m; int locked = 0;
    auto work = [&] { for (int i = 0; i < 20000; ++i) { ++safe; lock_guard<mutex> g(m); ++locked; } };
    thread t1(work), t2(work); t1.join(); t2.join();
    cout << " atomic=" << safe << " mutex=" << locked << " (a plain int here would be a race condition: " << unsafe << ")\n";
    recursive_mutex rm; { lock_guard<recursive_mutex> a(rm); lock_guard<recursive_mutex> b(rm); cout << " recursive mutex re-locked\n"; }
    timed_mutex tm; { unique_lock<timed_mutex> ul(tm, defer_lock); cout << " timed lock: " << ul.try_lock_for(chrono::milliseconds(50)) << "\n"; }
    mutex x, y; thread d1([&] { scoped_lock l(x, y); }), d2([&] { scoped_lock l(y, x); }); d1.join(); d2.join();   // deadlock avoided
    cout << " scoped_lock avoided deadlock\n";
    queue<int> q; mutex qm; condition_variable cv; bool done = false; long sum = 0;           // producer-consumer
    thread cons([&] { unique_lock<mutex> lk(qm); for (;;) { cv.wait(lk, [&] { return !q.empty() || done; }); while (!q.empty()) { sum += q.front(); q.pop(); } if (done) break; } });
    thread prod([&] { for (int i = 1; i <= 100; ++i) { { lock_guard<mutex> g(qm); q.push(i); } cv.notify_one(); } { lock_guard<mutex> g(qm); done = true; } cv.notify_one(); });
    prod.join(); cons.join(); cout << " producer-consumer sum=" << sum << "\n";
    promise<int> pr; auto fut = pr.get_future(); thread pt([&] { pr.set_value(7); }); pt.join();
    auto af = async(launch::async, [] { return 35; }); cout << " promise=" << fut.get() << " async=" << af.get() << "\n";
    thread bg([] {}); bg.detach();                                                              // detached thread
}

// ---- 8. Type casting ----
void casts() {
    section("Type casting");
    double d = 3.9; int i = static_cast<int>(d); float f = i;                                    // explicit + implicit
    const int ci = 5; int* mp = const_cast<int*>(&ci); (void)mp;                                 // const_cast (never write through it)
    Base* b = new Derived; Derived* dp = dynamic_cast<Derived*>(b); Item it; auto* raw = reinterpret_cast<unsigned char*>(&it);
    cout << " static=" << i << " implicit=" << f << " dynamic_cast ok=" << (dp != nullptr) << " first byte=" << (int)raw[0] << "\n";
    delete b;
}

int main() { basics(); ctors(); classes(); inheritance(); pointers(); exceptions(); threads(); casts(); cout << "\nAll sections finished.\n"; }
