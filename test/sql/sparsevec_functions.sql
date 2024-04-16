SELECT '{1:1,2:2,3:3}/3'::sparsevec < '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec < '{1:1,2:2}/2';
SELECT '{1:1,2:2,3:3}/3'::sparsevec <= '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec <= '{1:1,2:2}/2';
SELECT '{1:1,2:2,3:3}/3'::sparsevec = '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec = '{1:1,2:2}/2';
SELECT '{1:1,2:2,3:3}/3'::sparsevec != '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec != '{1:1,2:2}/2';
SELECT '{1:1,2:2,3:3}/3'::sparsevec >= '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec >= '{1:1,2:2}/2';
SELECT '{1:1,2:2,3:3}/3'::sparsevec > '{1:1,2:2,3:3}/3';
SELECT '{1:1,2:2,3:3}/3'::sparsevec > '{1:1,2:2}/2';

SELECT sparsevec_cmp('{1:1,2:2,3:3}/3', '{1:1,2:2,3:3}/3');
SELECT sparsevec_cmp('{1:1,2:2,3:3}/3', '{}/3');
SELECT sparsevec_cmp('{}/3', '{1:1,2:2,3:3}/3');
SELECT sparsevec_cmp('{1:1,2:2}/2', '{1:1,2:2,3:3}/3');
SELECT sparsevec_cmp('{1:1,2:2,3:3}/3', '{1:1,2:2}/2');
SELECT sparsevec_cmp('{1:1,2:2}/2', '{1:2,2:3,3:4}/3');
SELECT sparsevec_cmp('{1:2,2:3}/2', '{1:1,2:2,3:3}/3');

SELECT round(l2_norm('{1:1,2:1}/2'::sparsevec)::numeric, 5);
SELECT l2_norm('{1:3,2:4}/2'::sparsevec);
SELECT l2_norm('{2:1}/2'::sparsevec);
SELECT l2_norm('{1:3e37,2:4e37}/2'::sparsevec)::real;

SELECT l2_distance('{}/2'::sparsevec, '{1:3,2:4}/2');
SELECT l2_distance('{1:3}/2'::sparsevec, '{2:4}/2');
SELECT l2_distance('{2:4}/2'::sparsevec, '{1:3}/2');
SELECT l2_distance('{}/2'::sparsevec, '{2:1}/2');
SELECT '{}/2'::sparsevec <-> '{1:3,2:4}/2';

SELECT inner_product('{1:1,2:2}/2'::sparsevec, '{1:2,2:4}/2');
SELECT inner_product('{1:1,2:2}/2'::sparsevec, '{1:3}/1');
SELECT inner_product('{1:3e38}/1'::sparsevec, '{1:3e38}/1');
SELECT inner_product('{1:1,3:3,5:5}/5'::sparsevec, '{2:4,3:6,4:8}/5');
SELECT '{1:1,2:2}/2'::sparsevec <#> '{1:3,2:4}/2';

SELECT cosine_distance('{1:1,2:2}/2'::sparsevec, '{1:2,2:4}/2');
SELECT cosine_distance('{1:1,2:2}/2'::sparsevec, '{}/2');
SELECT cosine_distance('{1:1,2:1}/2'::sparsevec, '{1:1,2:1}/2');
SELECT cosine_distance('{1:1}/2'::sparsevec, '{2:2}/2');
SELECT cosine_distance('{1:1,2:1}/2'::sparsevec, '{1:-1,2:-1}/2');
SELECT cosine_distance('{1:2}/2'::sparsevec, '{2:2}/2');
SELECT cosine_distance('{1:1,2:2}/2'::sparsevec, '{1:3}/1');
SELECT cosine_distance('{1:1,2:1}/2'::sparsevec, '{1:1.1,2:1.1}/2');
SELECT cosine_distance('{1:1,2:1}/2'::sparsevec, '{1:-1.1,2:-1.1}/2');
SELECT cosine_distance('{1:3e38}/1'::sparsevec, '{1:3e38}/1');
SELECT cosine_distance('{}/1'::sparsevec, '{}/1');
SELECT '{1:1,2:2}/2'::sparsevec <=> '{1:2,2:4}/2';

SELECT l1_distance('{}/2'::sparsevec, '{1:3,2:4}/2');
SELECT l1_distance('{}/2'::sparsevec, '{2:1}/2');
SELECT l1_distance('{1:1,2:2}/2'::sparsevec, '{1:3}/1');
SELECT l1_distance('{1:3e38}/1'::sparsevec, '{1:-3e38}/1');
SELECT l1_distance('{1:1,3:3,5:5,7:7}/8'::sparsevec, '{2:2,4:4,6:6,8:8}/8');
SELECT l1_distance('{1:1,3:3,5:5,7:7,9:9}/9'::sparsevec, '{2:2,4:4,6:6,8:8}/9');

SELECT l2_normalize('{1:3,2:4}/2'::sparsevec);
SELECT l2_normalize('{1:3}/2'::sparsevec);
SELECT l2_normalize('{2:0.1}/2'::sparsevec);
SELECT l2_normalize('{}/2'::sparsevec);
SELECT l2_normalize('{1:3e38}/1'::sparsevec);
SELECT l2_normalize('{1:3e38,2:1e-37}/2'::sparsevec);
SELECT l2_normalize('{2:3e37,4:3e-37,6:4e37,8:4e-37}/9'::sparsevec);
