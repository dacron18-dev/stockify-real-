# Stockify – paper trading platform (C++ OOP project)

Learn trading with ₹10,00,000 of virtual money and live NSE/BSE stock prices (Indian stocks only).

| Layer | Tech |
|---|---|
| Frontend | HTML/CSS/JS, hosted on **Vercel** |
| Backend | **C++20** (cpp-httplib + nlohmann/json), Docker, hosted on **Render** |
| Database + Auth | **Supabase** (Postgres + email/password auth) |
| Market data | **Yahoo Finance** chart API |

Vercel cannot run a C++ server, so the C++ API runs in Docker on Render and Vercel forwards `/api/*` to it (`frontend/vercel.json`).

## OOP concepts used 
| Concept | Where |
|---|---|
| Abstraction | `User`, `Order` (pure virtual); interfaces `IMarketData`, `IAuthService`, `IRepository` |
| Inheritance | `Trader`, `Admin` extend `User`; `BuyOrder`, `SellOrder` extend `Order` |
| Polymorphism | `order->execute(trader)` and `user->role()` resolve at runtime; Yahoo/Supabase swappable via interfaces |
| Encapsulation | private balance and holdings; changes only through `debit`, `credit`, `add`, `remove` |
| Composition / aggregation | `Trader` owns a `Portfolio`, which holds `Holding`s |
| Factory pattern | `Order::create("BUY"/"SELL", ...)` |
| Exception handling | `TradeError`, `AuthError`, mapped to HTTP 400 / 401 |
| Smart pointers, STL, lambdas | `unique_ptr<User/Order>`, `map`, route guard lambda |

Full syllabus mapping: `docs/concept-map.md`. Extra topics demo: `backend/demo/concepts_demo.cpp`.

Diagrams: `docs/class-diagram.mmd`, `docs/object-diagram.mmd` and `docs/uml-diagrams.md` (collaboration, sequence, use case, ER, deployment).

## Setup
### 1. Supabase
1. Create a project. Run `database/schema.sql` in the SQL Editor. (If you already ran an older version, run only `database/schema_update.sql`.)
2. Authentication → Providers → Email: **turn off "Confirm email"** (so sign-up logs in immediately).
3. Project Settings → API: copy the URL, `anon` key and `service_role` key.
4. Sign up once in the app, then run: `update profiles set role='admin' where email='you@example.com';`

### 2. Backend on Render
New Web Service → connect your GitHub repo → Runtime **Docker** (uses the root `Dockerfile`).
Environment variables: `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `SUPABASE_SERVICE_KEY`.
Keep the service key secret; never put it in the frontend.

### 3. Frontend on Vercel
Edit `frontend/vercel.json` and replace `YOUR-BACKEND.onrender.com` with your Render URL.
Import the repo in Vercel and set **Root Directory = frontend**.

### Run locally
```
cd backend
mkdir -p third_party/nlohmann
wget -O third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.0/httplib.h
wget -O third_party/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
cmake -B build && cmake --build build
SUPABASE_URL=... SUPABASE_ANON_KEY=... SUPABASE_SERVICE_KEY=... ./build/stockify
```
Then serve `frontend/` with any static server and proxy `/api` to `localhost:8080` (or use `npx vercel dev`).

## Features
Traders: live quotes, buy/sell, portfolio with profit/loss, trade history, price alerts and notifications. Admin: all users and trades, reset balances, trigger a market event, start or stop the simulation thread.

## Known limits (mention these honestly)
- The server writes `stockify.log` and `stockify.audit` on its own disk; on Render these reset on redeploy.
- Prices are market prices from Yahoo's unofficial endpoint and may be delayed; it can rate-limit.
- Only Indian stocks (NSE `.NS`, BSE `.BO`) are allowed; everything is in INR, so there is no currency conversion. Typing `TCS` is treated as `TCS.NS`. The market table lists 30 large-cap Nifty 50 stocks (RELIANCE, TCS, HDFCBANK, ICICIBANK, INFY, HINDUNILVR, ITC, SBIN, BHARTIARTL, KOTAKBANK, LT, AXISBANK, ASIANPAINT, MARUTI, SUNPHARMA, TITAN, BAJFINANCE, HCLTECH, NTPC, ULTRACEMCO, ONGC, WIPRO, POWERGRID, M&M, TATASTEEL, JSWSTEEL, ADANIPORTS, COALINDIA, NESTLEIND, TECHM); any other NSE/BSE symbol can be searched.
- Saving a trade makes several database calls, not one atomic transaction (a Postgres function would fix this).
- Render's free tier sleeps when idle, so the first request can be slow.
