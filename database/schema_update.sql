-- Run this ONCE in the Supabase SQL Editor if you already ran schema.sql earlier

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
