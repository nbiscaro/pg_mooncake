CREATE TABLE s (a int);

-- insert rowstore_size - 1 rows
INSERT INTO s SELECT * FROM generate_series(0, 122879); 
SELECT * FROM s;

INSERT INTO s VALUES (1);
SELECT * FROM s;

DROP s;
