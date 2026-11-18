-- Run ONCE in the Supabase SQL Editor to convert an existing database to Indian stocks (INR) only.

-- 1) New default starting cash: Rs 10,00,000
alter table profiles alter column balance set default 1000000;

-- 2) Remove old US / non-Indian data (anything that is not an NSE .NS or BSE .BO symbol)
delete from holdings     where symbol !~ '\.(NS|BO)$';
delete from trades       where symbol !~ '\.(NS|BO)$';
delete from price_alerts where symbol !~ '\.(NS|BO)$';

-- 3) Reset every normal user's cash to Rs 10,00,000 (admins untouched)
update profiles set balance = 1000000 where role = 'user';
