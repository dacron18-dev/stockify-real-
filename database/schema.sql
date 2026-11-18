-- Run this in Supabase -> SQL Editor
create table if not exists profiles (
  id uuid primary key,                       -- same id as auth.users
  email text not null,
  role text not null default 'user' check (role in ('user','admin')),
  balance numeric(14,2) not null default 100000,
  created_at timestamptz default now()
);
create table if not exists holdings (
  user_id uuid references profiles(id) on delete cascade,
  symbol text not null,
  qty int not null check (qty > 0),
  avg_price numeric(14,4) not null,
  primary key (user_id, symbol)
);
create table if not exists trades (
  id bigint generated always as identity primary key,
  user_id uuid references profiles(id) on delete cascade,
  symbol text not null,
  side text not null check (side in ('BUY','SELL')),
  qty int not null,
  price numeric(14,4) not null,
  created_at timestamptz default now()
);
-- Only the C++ backend (service key) touches these tables.
alter table profiles enable row level security;
alter table holdings enable row level security;
alter table trades enable row level security;

-- Price alerts and notifications (class diagram: PriceAlert, Notification)
create table if not exists price_alerts (
  id bigint generated always as identity primary key,
  user_id uuid references profiles(id) on delete cascade,
  symbol text not null,
  target_price numeric(14,4) not null,
  above boolean not null,
  triggered boolean not null default false,
  created_at timestamptz default now()
);
create table if not exists notifications (
  id bigint generated always as identity primary key,
  user_id uuid references profiles(id) on delete cascade,
  message text not null,
  is_read boolean not null default false,
  created_at timestamptz default now()
);
alter table price_alerts enable row level security;
alter table notifications enable row level security;

-- After you sign up once in the app, make yourself admin:
-- update profiles set role = 'admin' where email = 'you@example.com';
