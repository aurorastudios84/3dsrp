# 3DSRP optional usage service

A public ingestion endpoint stores opted-in installation/day aggregates and
identifier-free daily totals. It has no public read endpoint or authentication
secret embedded in the app. Reports can be spoofed; these are approximate usage
figures, not billing or security records.

## Deploy

Use the Cloudflare Free plan, Node 22 or newer, and run from this directory:

```sh
npm ci
npm test
npx wrangler login
# For a fresh account/database, create an EU database and update the binding:
npx wrangler d1 create 3dsrp-usage --jurisdiction eu --binding DB --update-config
npx wrangler d1 execute 3dsrp-usage --remote --file schema.sql
npx wrangler deploy
```

The included database ID identifies the current deployment, not a credential.
Change it when deploying your own service. On a device-code login, use
`wrangler login --device --browser=false --scopes account:read user:read workers_scripts:write workers_routes:write d1:write`.
Then set `USAGE_ENDPOINT` in `3ds/app/include/usage_config.h` to your deployed
HTTPS URL plus `/v2/session`. The app uses TLS 1.2+ through mbedTLS and its configured network DNS server.
Keep certificate and hostname checks enabled.
An empty endpoint disables network reporting, even if consent is saved.

## View statistics without a public dashboard

Sign in to Cloudflare, open **Storage & databases > D1 > 3dsrp-usage > Console**,
and run the queries in `stats.sql`. Or run `npm run stats` after CLI login.
The first query compares each UUID by sessions, minutes and hours across the
retained 90-day window. The other results show reporting installations,
reported sessions and streamed hours, and daily totals. They do not measure
unique people or all installations. A release update preserves the local UUID;
CIA and 3DSX use the same preferences file.

D1 Free row-write quotas apply to receipt/index updates, aggregate triggers,
retention and administrative changes. Stay on the Free plan; quota exhaustion
keeps reports pending without affecting streaming. Recovery is attempted on
launch, without requiring another session. Each pending record is attempted at
most once per launch; a transient failure pauses the remaining backlog. New
session reports can still make their own attempt. No waiting screen is added.

## Data and operations

- `/v2/session` requires exactly `installation_id` and `session_id` (UUIDv4),
  `day` (YYYY-MM-DD from the device clock) and integer `seconds` (0–86400).
- The original activity date is used, not the receipt date. Dates older than
  90 days are rejected with 410; dates more than one UTC day ahead are rejected.
  One future UTC day is allowed for devices in timezones ahead of UTC.
- One receipt row per installation/session/day stores the maximum reported
  duration. Atomic SQLite triggers add only new duration to daily aggregates.
  Exact duplicates or stale checkpoints add nothing. A cross-midnight session
  counts once on its earliest reported day, even if records arrive out of order.
- The legacy `/v1/session` remains compatible with earlier app builds using
  installation ID and duration; those older reports retain receipt-day behavior.
- JSON bodies are bounded to 192 bytes; extra fields/invalid values are rejected.
- The local checksummed/atomic journal holds up to 128 pending records, bound to
  the installation UUID and consent generation. HOME/sleep/disconnect checkpoints
  survive app closure. Consent withdrawal purges them; a new consent generation
  prevents stale withdrawn files from being reactivated.
- Cron runs daily at 03:10 UTC. Installation/day and session/day deduplication
  data older than 90 days are removed; identifier-free totals retain 365 days.
  Daily cleanup of date buckets can add about a day; D1 Free recovery up to 7 days.
- Worker request logs/traces are disabled. No IPs, headers, exact event timestamps,
  gameplay or diagnostic logs are stored. Cloudflare receives connection metadata.
- Statistics reads require the Cloudflare account. For an authenticated privacy
  request, remove matching installation_daily AND session_day_receipts rows by
  installation UUID. Overall totals have no individual linkage.
- Apply schema.sql before deploying the v2 Worker. It preserves existing
  aggregates and creates/replaces the receipt triggers. For future trigger
  migrations with active v2 traffic, use a transaction/maintenance window.


## Original 3DS Relay / app distinction

Before deploying this worker over an existing service, apply only the additive
`schema-relay.sql` migration. It creates the separate `relay_*` table family and
its triggers; existing New 3DS tables, receipts and totals are unchanged. Fresh
local databases can use `schema.sql`, which includes both families. Do not seed
synthetic reports in production.

`POST /v3/session` requires the exact v2 fields plus `app`, restricted to
`new_3ds` or `original_3ds`. It uses the same 192-byte bound, validation,
maximum-duration deduplication, out-of-order midnight correction and retention.
Older `/v1` and `/v2` reports continue to use the native New 3DS table family.
The shared private dashboard unions the families for All apps and filters by
app throughout overview, history, details and CSV. The same UUID in two app
families counts as two app installations.

The original relay client keeps consent, UUID, timing and pending reports on the
handheld, using the existing C accounting/journal code. Android only delivers
reports via HTTPS; its fixed source is `original_3ds`, never an arbitrary caller
value. The relay app's own previews/tests do not start handheld gameplay sessions.
Privacy deletions for an Original 3DS install use `relay_installation_daily` AND
`relay_session_day_receipts`; native deletion queries keep their existing table
names. Neither family's overall daily totals retain individual linkage.
