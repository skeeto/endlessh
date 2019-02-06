CREATE TABLE IF NOT EXISTS log (
    host TEXT,
    port INTEGER,
    time REAL,
    bytes INTEGER
);
.mode csv
.import /dev/stdin log
