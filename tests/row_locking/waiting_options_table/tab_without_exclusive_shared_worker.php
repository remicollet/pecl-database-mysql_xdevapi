<?php
require_once(__DIR__."/../../connect.inc");
require_once(__DIR__."/../row_locking_utils.inc");

assert_mysql_xdevapi_loaded();

notify_worker_started();

$session = mysql_xdevapi\getSession($connection_uri);
$schema = $session->getSchema($test_schema_name);
$table = $schema->getTable($test_table_name);

recv_let_worker_run_cmd();
// terminal/cmd 2
$session->startTransaction();
check_select_lock_one($table, '1', 1, $Lock_shared);
// the execution should not return immediately, the transaction should be blocked
notify_worker_ran_cmd();

recv_let_worker_rollback();
// terminal/cmd 2
// since commit is done in cmd1 then the read must be possible now
check_select_lock_one($table, '1', 1, $Lock_shared);
$session->rollback();
// rollback the open transaction
notify_worker_rolled_back();

recv_let_worker_end();
?>
