-- Additive Original 3DS Relay tables. Existing New 3DS history is untouched.
-- No IP addresses, request logs, account IDs or console details.
CREATE TABLE IF NOT EXISTS relay_installation_daily (
  day TEXT NOT NULL CHECK(length(day) = 10),
  installation_id TEXT NOT NULL CHECK(length(installation_id) = 36),
  sessions INTEGER NOT NULL CHECK(sessions >= 0),
  seconds INTEGER NOT NULL CHECK(seconds >= 0),
  PRIMARY KEY(day, installation_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS relay_daily_totals (
  day TEXT PRIMARY KEY NOT NULL CHECK(length(day) = 10),
  sessions INTEGER NOT NULL CHECK(sessions >= 0),
  seconds INTEGER NOT NULL CHECK(seconds >= 0)
) WITHOUT ROWID;
-- Cumulative per-session/day receipts make recovery idempotent. Same 90-day
-- retention as relay_installation_daily; no timestamps, IPs or gameplay are stored.
CREATE TABLE IF NOT EXISTS relay_session_day_receipts (
  day TEXT NOT NULL CHECK(length(day)=10),
  installation_id TEXT NOT NULL CHECK(length(installation_id)=36),
  session_id TEXT NOT NULL CHECK(length(session_id)=36),
  seconds INTEGER NOT NULL CHECK(seconds BETWEEN 0 AND 86400),
  PRIMARY KEY(day, installation_id, session_id)
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS relay_usage_session_days ON relay_session_day_receipts(installation_id,session_id,day);
DROP TRIGGER IF EXISTS relay_usage_receipt_insert;
CREATE TRIGGER relay_usage_receipt_insert AFTER INSERT ON relay_session_day_receipts BEGIN
  -- If an older segment arrives out of order, move the session-start count to
  -- that earlier activity day. Duration always belongs to its own segment day.
  UPDATE relay_installation_daily SET sessions=MAX(sessions-1,0)
    WHERE installation_id=NEW.installation_id AND day=(SELECT MIN(day) FROM relay_session_day_receipts
      WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id AND day!=NEW.day)
    AND NEW.day<(SELECT MIN(day) FROM relay_session_day_receipts WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id AND day!=NEW.day);
  UPDATE relay_daily_totals SET sessions=MAX(sessions-1,0)
    WHERE day=(SELECT MIN(day) FROM relay_session_day_receipts WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id AND day!=NEW.day)
    AND NEW.day<(SELECT MIN(day) FROM relay_session_day_receipts WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id AND day!=NEW.day);
  INSERT INTO relay_installation_daily(day,installation_id,sessions,seconds)
  VALUES(NEW.day,NEW.installation_id,CASE WHEN NEW.day=(SELECT MIN(day) FROM relay_session_day_receipts WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id) THEN 1 ELSE 0 END,NEW.seconds)
  ON CONFLICT(day,installation_id) DO UPDATE SET sessions=MIN(sessions+excluded.sessions,1000000000),seconds=MIN(seconds+NEW.seconds,1000000000000);
  INSERT INTO relay_daily_totals(day,sessions,seconds)
  VALUES(NEW.day,CASE WHEN NEW.day=(SELECT MIN(day) FROM relay_session_day_receipts WHERE installation_id=NEW.installation_id AND session_id=NEW.session_id) THEN 1 ELSE 0 END,NEW.seconds)
  ON CONFLICT(day) DO UPDATE SET sessions=MIN(sessions+excluded.sessions,1000000000),seconds=MIN(seconds+NEW.seconds,1000000000000);
END;
DROP TRIGGER IF EXISTS relay_usage_receipt_update;
CREATE TRIGGER relay_usage_receipt_update AFTER UPDATE OF seconds ON relay_session_day_receipts
WHEN NEW.seconds>OLD.seconds BEGIN
  UPDATE relay_installation_daily SET seconds=MIN(seconds+NEW.seconds-OLD.seconds,1000000000000)
  WHERE day=NEW.day AND installation_id=NEW.installation_id;
  UPDATE relay_daily_totals SET seconds=MIN(seconds+NEW.seconds-OLD.seconds,1000000000000) WHERE day=NEW.day;
END;
