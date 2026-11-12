-- Compare reported usage per installation across the retained 90-day window.
SELECT installation_id AS unique_id, SUM(sessions) AS sessions,
  ROUND(SUM(seconds)/60.0,1) AS minutes_played,
  ROUND(SUM(seconds)/3600.0,2) AS hours_played
FROM installation_daily
GROUP BY installation_id
ORDER BY SUM(seconds) DESC;

-- Approximate reporting installations within the retained 90-day window.
SELECT COUNT(DISTINCT installation_id) AS reporting_installations FROM installation_daily;
SELECT SUM(sessions) AS reported_sessions, ROUND(SUM(seconds)/3600.0,2) AS streamed_hours FROM daily_totals;
SELECT day, COUNT(*) AS reporting_installations, SUM(sessions) AS sessions,
  ROUND(SUM(seconds)/3600.0,2) AS hours FROM installation_daily
  WHERE day >= date('now', '-30 days') GROUP BY day ORDER BY day DESC;
