# 3DSRP privacy notice

3DSRP communicates with your selected PlayStation for discovery, pairing, wake
and Remote Play. Settings and saved pairing credentials are stored on your SD
card under `/3ds/n3ds-remoteplay/`. Keep those files private.

## Optional usage statistics

Statistics are **off until you agree**. The first launch of this version asks
whether to Allow or choose No Thanks. Allow is highlighted initially, but
reporting remains off until you explicitly confirm it. Either choice
allows you to use the app. A saved decline is not asked again. Change your choice
at any time in **Settings > Usage statistics**. Declining sends no statistics.
A missing or damaged consent file leaves reporting off and asks again.

After you agree, the app creates a random UUIDv4 and saves it in `usage.bin`
in the folder above. This pseudonymous identifier is unrelated to your console,
account, MAC address or serial number. The CIA and 3DSX share it. It remains
stable across updates and consent changes while that file remains present.
Copying the file copies the identifier; deleting it resets the saved choice and
identifier. It is an approximate installation count, not a count of people.

For a session that shows video, the app sends cumulative activity reports containing:

- The random installation UUID.
- A random session UUID used to prevent duplicate counting during recovery.
- The activity date from the 3DS calendar clock, not the upload date.
- Streamed seconds for that session on that activity date.

A session crossing midnight is divided between its activity dates. Time while
suspended on HOME or asleep is excluded. Keep the 3DS clock correct. Sessions
are counted once, on the earliest reported activity date for that session.
In-app menus count while streaming remains active.

These reports are used **only for usage statistics**: reporting installations,
session counts and streamed hours. The maintainer does not use them for ads,
profiling, sale or sharing with advertisers. No PlayStation/account identifiers,
game names, controller input, audio, video, pairing credentials or diagnostic
logs are included. This build has no diagnostic file logger or developer UDP
sender; old saved logging flags cannot enable them.

The service is hosted by Cloudflare Workers and Cloudflare D1. Cloudflare receives
your IP address and connection metadata to deliver and secure HTTPS requests;
this is not a claim that no personal data is processed. Our statistics database
does not store IP addresses, request headers or gameplay logs. Minimal cumulative
session/day receipts are stored only to prevent duplicate counting. Worker
request logging and traces are disabled. Cloudflare may process operational and
security metadata under [its privacy policy](https://www.cloudflare.com/privacypolicy/).
The database is configured for EU jurisdiction; network requests may be handled
elsewhere by Cloudflare.

The server combines reports under their original activity dates. Installation-ID
daily totals and duplicate-prevention session/day receipts have a 90-day retention
period; overall daily totals
without identifiers have a 365-day retention period. Daily cleanup of UTC date
buckets can add about one day before deletion.
Cloudflare's Free D1 recovery backups may retain deleted data for up to a further
seven days. Statistics are accessible to the project administrator through the
Cloudflare account, not through a public statistics endpoint.

Disabling statistics immediately stops further reporting and discards any
pending local report. It does not undo reports already received by the service.
An unsuccessful consent save leaves reporting off for that app launch and shows
a save notice; a previous saved choice may remain for the next launch.

Pending activity is saved in `usage-pending.bin` on the SD card when HOME or sleep
suspends the app and when a stream ends. The file is bound to the installation
and saved consent state. Recovery starts automatically at launch after consent
and networking are ready; it does not require another stream or disconnect.
Up to 128 pending session/day records are retained locally; if full, the oldest
is discarded to make space for newer activity. Copies on SD may remain until
accepted, permanently rejected as invalid/older than 90 days, or statistics are
disabled. The file is private app data, not a diagnostic log.

Reporting failures, offline service and quota exhaustion produce no pop-ups,
error messages or waiting screen. Failed/unconfirmed reports remain pending for
a later launch. A transient failure pauses the remaining backlog for that launch;
newly completed sessions may still make their own attempt. Starting console work
or exiting cancels unfinished networking without waiting. Acknowledged reports
are removed locally; the server counts only increases in cumulative time, so a
lost acknowledgement or repeated recovery cannot duplicate usage.

HOME/sleep saves the activity accumulated so far. Closing from HOME can recover
that activity at the next launch, even on a different day. Sudden power loss or
crashes before a checkpoint, SD write failures, a wrong console clock, the local
queue limit or server retention can still leave usage uncounted. Figures remain
approximate and opt-in only.

For privacy questions or deletion requests, start by contacting the
[project maintainer](https://github.com/aurorastudios84). Do not put pairing
files, account IDs, credentials or installation UUIDs in a public issue.
Arrange a private channel before sending an identifier. Daily records and
receipts tied to an installation UUID can be removed; identifier-free overall
totals cannot be traced back to you.

GitHub hosts public downloads and handles web traffic under
[its own privacy policy](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).


## Original 3DS Relay reporting

The separate Android-assisted Original 3DS app now shares this service. It keeps
its own consent, random installation UUID and pending journal under
`/3ds/3dsrp-relay/`. Its initial selection is No thanks; Menu → Usage logging
changes the saved choice. Android forwards the same installation/session/date/
duration fields over HTTPS plus the fixed app type `original_3ds` to `/v3/session`.
The local relay link uses UDP. Phone previews/tests are excluded: only actual
console pictures presented on the handheld start accounting. Android stores no
telemetry journal. Withdrawal, recovery, duplicate handling and retention follow
the same rules above. A persistence error leaves logging off for the current
launch and is shown on the handheld; the last saved choice may return on restart.

Existing native `/v1` and `/v2` clients continue to report as New 3DS without an
update. Relay tables are stored separately. The private dashboard offers All,
New 3DS and Original 3DS Relay filters, including individual history and CSV.
The distinction identifies the app family, not a hardware probe; a relay app
running on New 3DS hardware is still Original 3DS Relay. The same UUID appearing
in two app families counts as two reporting app installations.

## Windows companion

The Windows companion can forward the same consent-controlled Original 3DS
usage reports as Android. It does not collect a separate Windows installation
identifier for these reports. Phone/PC preview sessions are excluded.
Settings and saved PlayStation pairings are local, per-user application data.
Local startup diagnostics may be written beside the EXE, with a temporary-folder
fallback. Automatic startup-log uploads have been removed. Diagnostic views and
files can contain local addresses; review them before sharing. The local relay
transport has the same trusted-LAN limitations described in the README.
