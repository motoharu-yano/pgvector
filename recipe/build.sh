make -j$CPU_COUNT

make install

export PGPORT=54322
export PGDATA=$SRC_DIR/pgdata

# cleanup required when building variants
rm -rf $PGDATA

initdb -D $PGDATA

# ensure that the gzip extension is loaded at process startup
echo "shared_preload_libraries = 'vector'" >> $PGDATA/postgresql.conf

sed -i "s/#port = 5432/port = $PGPORT/g" $PGDATA/postgresql.conf

pg_ctl -D $PGDATA -l $PGDATA/log.txt start

# wait a few seconds just to make sure that the server has started
sleep 2

set +e
make installcheck        # regression tests
check_result1=$?
make prove_installcheck
check_result12=$?
set -e

pg_ctl -D $PGDATA stop

exit $check_result1 || $check_result2