SET enable_seqscan = off;

CREATE TABLE t (val bit(3));
INSERT INTO t (val) VALUES (B'000'), (B'100'), (B'111'), (NULL);
CREATE INDEX ON t USING hnsw (val bit_hamming_ops);

INSERT INTO t (val) VALUES (B'110');

SELECT * FROM t ORDER BY val <~> B'111';
SELECT COUNT(*) FROM (SELECT * FROM t ORDER BY val <~> (SELECT NULL::bit)) t2;

DROP TABLE t;

-- TODO move
CREATE TABLE t (val varbit(3));
CREATE INDEX ON t USING hnsw (val bit_hamming_ops);
CREATE INDEX ON t USING hnsw ((val::bit(3)) bit_hamming_ops);
CREATE INDEX ON t USING hnsw ((val::bit(64001)) bit_hamming_ops);
DROP TABLE t;
