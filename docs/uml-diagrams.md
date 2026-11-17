# Stockify – UML diagrams

Open this file in GitHub, VS Code (Markdown Preview Mermaid Support) or paste each block into https://mermaid.live to export PNG/SVG.
The class diagram is in `class-diagram.mmd`.

## Use case diagram
```mermaid
flowchart LR
  T([Trader]) --- U1(Sign up / Log in)
  T --- U2(Search live stock price)
  T --- U3(Buy shares)
  T --- U4(Sell shares)
  T --- U5(View portfolio and P/L)
  T --- U6(View trade history)
  A([Admin]) --- U1
  A --- U7(View all users)
  A --- U8(View all trades)
  A --- U9(Reset user balance)
  U2 -.->|uses| Y[(Yahoo Finance API)]
  U3 -.->|include| U2
  U4 -.->|include| U2
```

## Sequence diagram – buying a stock
```mermaid
sequenceDiagram
  actor T as Trader
  participant FE as Frontend (Vercel)
  participant API as C++ API
  participant S as Supabase
  participant Y as Yahoo Finance
  T->>FE: Enter AAPL, qty 5, press Buy
  FE->>API: POST /api/trade (token)
  API->>S: verify token, load profile + holdings
  API->>Y: GET chart AAPL
  Y-->>API: live price
  API->>API: Order.create("BUY") then BuyOrder.execute(trader)
  API->>S: save balance + holdings, insert trade
  API-->>FE: filled + new balance
  FE-->>T: Toast and refreshed portfolio
```

## ER diagram
```mermaid
erDiagram
  PROFILES ||--o{ HOLDINGS : has
  PROFILES ||--o{ TRADES : places
  PROFILES { uuid id PK
    string email
    string role
    numeric balance }
  HOLDINGS { uuid user_id FK
    string symbol
    int qty
    numeric avg_price }
  TRADES { bigint id PK
    uuid user_id FK
    string symbol
    string side
    int qty
    numeric price }
```

## Deployment diagram
```mermaid
flowchart LR
  B[Browser] --> V[Vercel: static frontend + /api rewrite]
  V --> R[Render: C++ server in Docker]
  R --> S[(Supabase: Auth + Postgres)]
  R --> Y[Yahoo Finance]
```
