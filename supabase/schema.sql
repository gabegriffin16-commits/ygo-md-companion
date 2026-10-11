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

-- ===== Engine test runs (added later: run this part if you ran the file before) =====
-- Claude posts a test plan to bench_jobs; the app's "Run engine test" button (admin only) runs it on that PC
-- and posts what it found to bench_results. Readable by any signed-in account; each row is written by its owner.
create table if not exists public.bench_jobs (
  id bigint generated always as identity primary key,
  owner uuid not null references public.profiles (id) on delete cascade default auth.uid(),
  created_at timestamptz not null default now(),
  name text not null default 'Engine test',
  spec jsonb not null
);
create table if not exists public.bench_results (
  id bigint generated always as identity primary key,
  owner uuid not null references public.profiles (id) on delete cascade default auth.uid(),
  job_id bigint references public.bench_jobs (id) on delete cascade,
  created_at timestamptz not null default now(),
  info jsonb,
  result jsonb
);
alter table public.bench_jobs enable row level security;
alter table public.bench_results enable row level security;
drop policy if exists "bench jobs readable" on public.bench_jobs;
create policy "bench jobs readable" on public.bench_jobs for select to authenticated using (true);
drop policy if exists "bench jobs insert own" on public.bench_jobs;
create policy "bench jobs insert own" on public.bench_jobs for insert to authenticated with check (owner = auth.uid());
drop policy if exists "bench jobs delete own" on public.bench_jobs;
create policy "bench jobs delete own" on public.bench_jobs for delete to authenticated using (owner = auth.uid());
drop policy if exists "bench results readable" on public.bench_results;
create policy "bench results readable" on public.bench_results for select to authenticated using (true);
drop policy if exists "bench results insert own" on public.bench_results;
create policy "bench results insert own" on public.bench_results for insert to authenticated with check (owner = auth.uid());

-- ===== Line feedback (added later: run this part if you ran the file before) =====
-- "This line is wrong" reports from the app: the hand, the line / board the engine suggested, and what the player
-- would do instead. Each row is written by its owner; Claude reads them (with the "enginetest" account) to turn them
-- into engine test cases.
create table if not exists public.line_feedback (
  id bigint generated always as identity primary key,
  owner uuid not null references public.profiles (id) on delete cascade default auth.uid(),
  created_at timestamptz not null default now(),
  deck text,                -- deck name / id
  hand jsonb,               -- card codes of the starting hand
  suggested jsonb,          -- the line and end board shown (steps, field, backrow, hand, gy, score, engine version)
  note text                 -- what's wrong / what they'd do instead
);
alter table public.line_feedback enable row level security;
drop policy if exists "line feedback readable" on public.line_feedback;
-- Readable by whoever wrote it, the admin, and the "enginetest" account Claude reads reports with.
create policy "line feedback readable" on public.line_feedback for select to authenticated
  using (owner = auth.uid() or public.is_admin() or exists (select 1 from public.profiles where id = auth.uid() and lower(username) = 'enginetest'));
drop policy if exists "line feedback insert own" on public.line_feedback;
create policy "line feedback insert own" on public.line_feedback for insert to authenticated with check (owner = auth.uid());
drop policy if exists "line feedback delete own" on public.line_feedback;
create policy "line feedback delete own" on public.line_feedback for delete to authenticated using (owner = auth.uid());
