SET enable_seqscan = off;

CREATE TABLE t (val halfvec(3));
INSERT INTO t (val) VALUES ('[0,0,0]'), ('[1,2,3]'), ('[1,1,1]'), (NULL);
CREATE INDEX ON t USING hnsw (val halfvec_l2_ops);

INSERT INTO t (val) VALUES ('[1,2,4]');

SELECT * FROM t ORDER BY val <-> '[3,3,3]';
SELECT COUNT(*) FROM (SELECT * FROM t ORDER BY val <-> (SELECT NULL::halfvec)) t2;
SELECT COUNT(*) FROM t;

TRUNCATE t;
SELECT * FROM t ORDER BY val <-> '[3,3,3]';

DROP TABLE t;
