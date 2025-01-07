LOAD 'pg_mooncake';

CREATE TABLE s (a int);
INSERT INTO s VALUES (1), (2), (3);

CREATE TABLE c (a int) USING columnstore;
INSERT INTO c VALUES (1), (2), (3);

CREATE TABLE t AS SELECT * FROM s;
SELECT * FROM t;
DROP TABLE t;

CREATE TABLE t USING columnstore AS SELECT * FROM s;
SELECT * FROM t;
DROP TABLE t;

CREATE TABLE t USING columnstore AS SELECT * FROM c;
SELECT * FROM t;
DROP TABLE t;

DROP TABLE s;
DROP TABLE c;
