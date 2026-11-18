# Stockify – UML diagrams

Open this file in GitHub or VS Code (Mermaid preview), or paste each block into https://mermaid.live to export PNG/SVG.
Class diagram: `class-diagram.mmd`. Object diagram: `object-diagram.mmd`.

## Collaboration (communication) diagram: buying a stock
Same 17 numbered messages as your diagram. The numbers show the order of the calls.
```mermaid
flowchart LR
  T([Trader]) -- "1. Select stock + enter quantity" --> TI[Trading Interface]
  TI -- "2. getCurrentPrice(symbol)" --> M[Market]
  M -. "3. currentPrice" .-> TI
  T -- "4. Submit buy order" --> TI
  TI -- "5. placeBuyOrder(...)" --> OM[Order Manager]
  OM -- "6. validateStock(stock)" --> M
  M -. "7. stockValid" .-> OM
  OM -- "8. hasSufficientFunds(orderValue)" --> A[Account]
  A -. "9. fundsAvailable" .-> OM
  OM -- "10. deductCash(orderValue)" --> A
  A -. "11. cashDeducted" .-> OM
  OM -- "12. addHolding(stock, quantity, price)" --> P[Portfolio]
  P -. "13. holdingUpdated" .-> OM
  OM -- "14. createTransaction(order)" --> TM[Transaction Manager]
  TM -. "15. transactionCreated" .-> OM
  OM -. "16. Order executed successfully" .-> TI
  TI -. "17. Display confirmation" .-> T
  N["Alternative flows: insufficient funds = order rejected; invalid stock or quantity = validation error"] -.- OM
```

## Sequence diagram (same flow, with the real classes)
```mermaid
sequenceDiagram
  actor T as Trader
  participant FE as Frontend
  participant API as C++ API
  participant OM as OrderManager
  participant M as Market
  participant Y as Yahoo Finance
  participant TR as Trader object
  participant TM as TransactionManager
  participant DB as Supabase
  T->>FE: Select stock, quantity, press Buy
  FE->>API: POST /api/trade
  API->>OM: placeOrder(trader, BUY, symbol, qty)
  OM->>M: validateStock(symbol), getStock(symbol)
  M->>Y: live price
  Y-->>M: price
  M-->>OM: Stock
  OM->>TR: placeBuyOrder(stock, qty)
  TR-->>OM: BuyOrder
  OM->>OM: order.validate()
  OM->>TR: order.execute(trader): hasSufficientCash, deductCash, addHolding
  alt insufficient funds or invalid order
    OM-->>API: exception, order REJECTED
    API-->>FE: error message
  else success
    OM->>DB: saveTrader(trader)
    OM->>TM: createTransaction(userId, tx)
    TM->>DB: insert trade
    OM-->>API: Transaction
    API-->>FE: filled + new balance
    FE-->>T: confirmation
  end
```

## Use case diagram
```mermaid
flowchart LR
  T([Trader]) --- U1(Sign up / Log in)
  T --- U2(Search live stock price)
  T --- U3(Buy shares)
  T --- U4(Sell shares)
  T --- U5(View portfolio and profit/loss)
  T --- U6(View trade history)
  T --- U10(Set price alert)
  T --- U11(Read notifications)
  A([Admin]) --- U1
  A --- U7(View all users)
  A --- U8(View all trades)
  A --- U9(Reset user balance)
  A --- U12(Trigger market event)
  A --- U13(Start / stop simulation)
  U3 -.->|include| U2
  U4 -.->|include| U2
  U10 -.->|include| U2
```

## ER diagram
```mermaid
erDiagram
  PROFILES ||--o{ HOLDINGS : has
  PROFILES ||--o{ TRADES : places
  PROFILES ||--o{ PRICE_ALERTS : sets
  PROFILES ||--o{ NOTIFICATIONS : receives
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
  PRICE_ALERTS { bigint id PK
    uuid user_id FK
    string symbol
    numeric target_price
    bool above
    bool triggered }
  NOTIFICATIONS { bigint id PK
    uuid user_id FK
    string message
    bool is_read }
```

## Deployment diagram
```mermaid
flowchart LR
  B[Browser] --> V[Vercel: static frontend + /api rewrite]
  V --> R[Render: C++ server in Docker]
  R --> S[(Supabase: Auth + Postgres)]
  R --> Y[Yahoo Finance]
```
