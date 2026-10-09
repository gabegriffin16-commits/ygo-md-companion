-- Master Duel Companion App: accounts and shared decks.
-- Paste this whole file into Supabase > SQL Editor > New query, then press Run. Safe to run again.

-- One row per user. Created automatically when someone signs up.
create table if not exists public.profiles (
  id uuid primary key references auth.users on delete cascade,
  username text not null,
  is_admin boolean not null default false,
  created_at timestamptz not null default now()
);
create unique index if not exists profiles_username_key on public.profiles (lower(username));

-- One row per saved deck. "data" holds the cards, notes and starters exactly as the app saves them.
create table if not exists public.decks (
  id text primary key,
  owner uuid not null references public.profiles (id) on delete cascade,
  name text not null default 'Untitled deck',
  data jsonb not null,
  shared boolean not null default true,
  updated_at timestamptz not null default now()
);
create index if not exists decks_owner_idx on public.decks (owner);

-- Is the signed-in user the admin? (security definer so the deck rules can ask without looping)
create or replace function public.is_admin() returns boolean
language sql stable security definer set search_path = public as $$
  select coalesce((select is_admin from public.profiles where id = auth.uid()), false)
$$;

-- Make the profile row when an account is created, using the username the app sends.
create or replace function public.handle_new_user() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  insert into public.profiles (id, username)
  values (new.id, coalesce(nullif(trim(new.raw_user_meta_data ->> 'username'), ''), split_part(new.email, '@', 1)));
  return new;
end $$;
drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created after insert on auth.users
  for each row execute function public.handle_new_user();

-- Row level security: the rules the database itself enforces, whatever the page does.
alter table public.profiles enable row level security;
alter table public.decks enable row level security;

-- Signed-in users can see everyone's username (for the Users tab). Nobody can edit profiles from the app,
-- so nobody can make themselves admin.
drop policy if exists "profiles readable" on public.profiles;
create policy "profiles readable" on public.profiles for select to authenticated using (true);

-- Decks: you can see your own, anyone's shared ones, and the admin sees all. Only the owner can change them.
drop policy if exists "decks readable" on public.decks;
create policy "decks readable" on public.decks for select to authenticated
  using (owner = auth.uid() or shared or public.is_admin());
drop policy if exists "decks insert own" on public.decks;
create policy "decks insert own" on public.decks for insert to authenticated with check (owner = auth.uid());
drop policy if exists "decks update own" on public.decks;
create policy "decks update own" on public.decks for update to authenticated using (owner = auth.uid()) with check (owner = auth.uid());
drop policy if exists "decks delete own" on public.decks;
create policy "decks delete own" on public.decks for delete to authenticated using (owner = auth.uid());

-- After you've created YOUR account in the app, run this one line (with your username) to make yourself admin:
-- update public.profiles set is_admin = true where lower(username) = lower('YOUR_USERNAME');
