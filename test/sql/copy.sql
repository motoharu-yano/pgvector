CREATE TABLE t (val vector(3), val2 halfvec(3), val3 sparsevec(3));
INSERT INTO t (val, val2, val3) VALUES ('[0,0,0]', '[0,0,0]', '{}/3'), ('[1,2,3]', '[1,2,3]', '{1:1,2:2,3:3}/3'), ('[1,1,1]', '[1,1,1]', '{1:1,2:1,3:1}/3'), (NULL, NULL, NULL);

CREATE TABLE t2 (val vector(3), val2 halfvec(3), val3 sparsevec(3));

\copy t TO 'results/data.bin' WITH (FORMAT binary)
\copy t2 FROM 'results/data.bin' WITH (FORMAT binary)

SELECT * FROM t2 ORDER BY val;

DROP TABLE t;
DROP TABLE t2;
