make -j$CPU_COUNT

make install

export PGPORT=54322
export PGDATA=$SRC_DIR/pgdata

# cleanup required when building variants
rm -rf $PGDATA

pg_ctl initdb

# ensure that the gzip extension is loaded at process startup
echo "shared_preload_libraries = 'vector'" >> $PGDATA/postgresql.conf

pg_ctl start -l $PGDATA/log.txt

# wait a few seconds just to make sure that the server has started
sleep 2

set +e
make installcheck        # regression tests
check_result=$?
set -e

pg_ctl stop

exit $check_result