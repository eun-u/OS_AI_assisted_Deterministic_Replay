DROP TABLE IF EXISTS users;
CREATE TABLE users (
  id INTEGER PRIMARY KEY,
  name TEXT,
  score INTEGER
);

INSERT INTO users(name, score) VALUES ('alice', 10);
INSERT INTO users(name, score) VALUES ('bob', 20);
INSERT INTO users(name, score) VALUES ('carol', 30);

SELECT COUNT(*) FROM users;
SELECT name, score FROM users ORDER BY score DESC;
UPDATE users SET score = score + 5 WHERE name = 'alice';
SELECT name, score FROM users ORDER BY id;
