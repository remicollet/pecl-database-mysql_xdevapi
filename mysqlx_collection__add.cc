/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2019 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Andrey Hristov <andrey@php.net>                             |
  +----------------------------------------------------------------------+
*/
#include "php_api.h"
#include "mysqlnd_api.h"
extern "C" {
#include <ext/json/php_json.h>
#include <ext/json/php_json_parser.h>
}
#include "xmysqlnd/xmysqlnd.h"
#include "xmysqlnd/xmysqlnd_session.h"
#include "xmysqlnd/xmysqlnd_schema.h"
#include "xmysqlnd/xmysqlnd_stmt.h"
#include "xmysqlnd/xmysqlnd_collection.h"
#include "xmysqlnd/xmysqlnd_crud_collection_commands.h"
#include "php_mysqlx.h"
#include "mysqlx_exception.h"
#include "mysqlx_class_properties.h"
#include "mysqlx_executable.h"
#include "mysqlx_sql_statement.h"
#include "mysqlx_collection__add.h"
#include "mysqlx_exception.h"
#include "util/allocator.h"
#include "util/json_utils.h"
#include "util/object.h"
#include "util/strings.h"
#include "util/string_utils.h"
#include "util/zend_utils.h"

namespace mysqlx {

namespace devapi {

using namespace drv;

namespace {

zend_class_entry* collection_add_class_entry;

ZEND_BEGIN_ARG_INFO_EX(arginfo_mysqlx_collection__add__execute, 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mysqlx_collection__add__add, 0, ZEND_RETURN_VALUE, 1)
	ZEND_ARG_INFO(no_pass_by_ref, json)
ZEND_END_ARG_INFO()


//------------------------------------------------------------------------------


/* {{{ execute_statement */
enum_func_status
execute_statement(xmysqlnd_stmt* stmt,zval* return_value)
{
	enum_func_status ret{FAIL};
	if (stmt) {
		zval stmt_zv;
		ZVAL_UNDEF(&stmt_zv);
		mysqlx_new_stmt(&stmt_zv, stmt);
		if (Z_TYPE(stmt_zv) == IS_NULL) {
			xmysqlnd_stmt_free(stmt, nullptr, nullptr);
		}
		if (Z_TYPE(stmt_zv) == IS_OBJECT) {
			zval zv;
			ZVAL_UNDEF(&zv);
			zend_long flags{0};
			mysqlx_statement_execute_read_response(Z_MYSQLX_P(&stmt_zv),
								flags, MYSQLX_RESULT, &zv);
			ZVAL_COPY(return_value, &zv);
			zval_dtor(&zv);
			ret = PASS;
		}
		zval_ptr_dtor(&stmt_zv);
	}
	return ret;
}
/* }}} */


#define ID_COLUMN_NAME		"_id"
#define ID_TEMPLATE_PREFIX	"\"" ID_COLUMN_NAME "\":\""
#define ID_TEMPLATE_SUFFIX	"\"}"

enum class Add_op_status
{
	success,
	fail,
	noop
};

struct doc_add_op_return_status
{
	Add_op_status return_status;
	MYSQLND_CSTRING doc_id;
};

/* {{{ collection_add_string */
Add_op_status
collection_add_string(
	st_xmysqlnd_crud_collection_op__add* add_op,
	zval* doc,
	zval* /*return_value*/)
{
	if( PASS == xmysqlnd_crud_collection_add__add_doc(add_op,doc) ) {
		return Add_op_status::success;
	}
	return Add_op_status::fail;
}
/* }}} */


/* {{{ collection_add_object*/
Add_op_status
collection_add_object_impl(
	st_xmysqlnd_crud_collection_op__add* add_op,
	zval* doc,
	zval* /*return_value*/)
{
	zval new_doc;
	Add_op_status ret = Add_op_status::fail;
	util::json::to_zv_string(doc, &new_doc);
	if( PASS == xmysqlnd_crud_collection_add__add_doc(add_op, &new_doc) ) {
		ret = Add_op_status::success;
	}
	zval_dtor(&new_doc);
	return ret;
}
/* }}} */


/* {{{ collection_add_object*/
Add_op_status
collection_add_object(
	st_xmysqlnd_crud_collection_op__add* add_op,
	zval* doc,
	zval* return_value)
{
	return collection_add_object_impl(
				add_op, doc, return_value);
}
/* }}} */


/* {{{ collection_add_array*/
Add_op_status
collection_add_array(
	st_xmysqlnd_crud_collection_op__add* add_op,
	zval* doc,
	zval* return_value)
{
	Add_op_status ret = Add_op_status::fail;
	if( zend_hash_num_elements(Z_ARRVAL_P(doc)) == 0 ) {
		ret = Add_op_status::noop;
	} else {
		ret = collection_add_object_impl(
			add_op, doc, return_value);
	}
	return ret;
}
/* }}} */

} // anonymous namespace

//------------------------------------------------------------------------------


/* {{{ Collection_add::init() */
bool Collection_add::add_docs(
	zval* obj_zv,
	xmysqlnd_collection* coll,
	zval* documents,
	int num_of_documents)
{
	if (!obj_zv || !documents || !num_of_documents) return false;

	for (int i{0}; i < num_of_documents; ++i) {
		if (Z_TYPE(documents[i]) != IS_STRING &&
			Z_TYPE(documents[i]) != IS_OBJECT &&
			Z_TYPE(documents[i]) != IS_ARRAY) {
			php_error_docref(nullptr, E_WARNING, "Only strings, objects and arrays can be added. Type is %u", Z_TYPE(documents[i]));
			return false;
		}
	}

	if( !add_op ) {
		if( !coll ) return false;
		object_zv = obj_zv;
		collection = coll->get_reference();
		add_op = xmysqlnd_crud_collection_add__create(
			mnd_str2c(collection->get_schema()->get_name()),
			mnd_str2c(collection->get_name()));
		if (!add_op) return false;
	} else {
		ZVAL_COPY(obj_zv, object_zv);
	}

	zval doc;
	for (int i{0}; i < num_of_documents; ++i) {
		ZVAL_DUP(&doc, &documents[i]);
		docs.push_back( doc );
	}

	return true;
}
/* }}} */


/* {{{ Collection_add::init() */
bool Collection_add::add_docs(
	zval* obj_zv,
	xmysqlnd_collection* coll,
	const util::string_view& /*doc_id*/,
	zval* doc)
{
	const int num_of_documents = 1;
	if (!add_docs(obj_zv, coll, doc, num_of_documents)) return false;
	return xmysqlnd_crud_collection_add__set_upsert(add_op) == PASS;
}
/* }}} */


/* {{{ Collection_add::~Collection_add() */
Collection_add::~Collection_add()
{
	for (size_t i{0}; i < docs.size(); ++i) {
		zval_ptr_dtor(&docs[i]);
		ZVAL_UNDEF(&docs[i]);
	}

	if (add_op) {
		xmysqlnd_crud_collection_add__destroy(add_op);
	}

	if (collection) {
		xmysqlnd_collection_free(collection, nullptr, nullptr);
	}
}
/* }}} */


/* {{{ Collection_add::execute() */
void Collection_add::execute(zval* return_value)
{
	enum_func_status execute_ret_status{PASS};
	size_t noop_cnt{0};

	DBG_ENTER("Collection_add::execute");

	RETVAL_FALSE;

	Add_op_status ret = Add_op_status::success;
	for (size_t i{0}; i < docs.size() && ret != Add_op_status::fail ; ++i) {
		ret = Add_op_status::fail;
		switch(Z_TYPE(docs[i])) {
		case IS_STRING:
			ret = collection_add_string(
				add_op, &docs[i], return_value);
			break;
		case IS_ARRAY:
			ret = collection_add_array(
				add_op, &docs[i], return_value);
			break;
		case IS_OBJECT:
			ret = collection_add_object(
				add_op, &docs[i], return_value);
			break;
		}
		if( ret == Add_op_status::noop ) {
			++noop_cnt;
		}
	}

	if ( execute_ret_status != FAIL && docs.size() > noop_cnt ) {
		xmysqlnd_stmt* stmt = collection->add(add_op);
		if( nullptr != stmt ) {
			execute_ret_status =  execute_statement(stmt,return_value);
		} else {
			execute_ret_status = FAIL;
		}
	}
	if (FAIL == execute_ret_status && !EG(exception)) {
		RAISE_EXCEPTION(err_msg_add_doc);
	}
	DBG_VOID_RETURN;
}
/* }}} */


//------------------------------------------------------------------------------


/* {{{ mysqlx_collection__add::__construct */
MYSQL_XDEVAPI_PHP_METHOD(mysqlx_collection__add, __construct)
{
	UNUSED_INTERNAL_FUNCTION_PARAMETERS();
}
/* }}} */


/* {{{ proto mixed mysqlx_collection__add::execute() */
MYSQL_XDEVAPI_PHP_METHOD(mysqlx_collection__add, execute)
{
	zval* object_zv{nullptr};

	DBG_ENTER("mysqlx_collection__add::execute");

	if (FAILURE == util::zend::parse_method_parameters(execute_data, getThis(), "O",
												&object_zv,
												collection_add_class_entry))
	{
		DBG_VOID_RETURN;
	}

	Collection_add& coll_add = util::fetch_data_object<Collection_add>(object_zv);
	coll_add.execute(return_value);

	DBG_VOID_RETURN;
}
/* }}} */


/* {{{ proto mixed mysqlx_collection__add::add() */
MYSQL_XDEVAPI_PHP_METHOD(mysqlx_collection__add, add)
{
	zval* object_zv{nullptr};
	zval* docs{nullptr};
	int num_of_docs{0};

	DBG_ENTER("mysqlx_collection::add");

	if (FAILURE == util::zend::parse_method_parameters(execute_data, getThis(), "O+",
												&object_zv,
												collection_add_class_entry,
												&docs,
												&num_of_docs))
	{
		DBG_VOID_RETURN;
	}

	/*
	 * For subsequent calls to add_docs, the xmysqlnd_collection is set to NULL
	 */
	Collection_add& coll_add = util::fetch_data_object<Collection_add>(object_zv);
	coll_add.add_docs(return_value, nullptr, docs, num_of_docs);

	DBG_VOID_RETURN;
}
/* }}} */


/* {{{ mysqlx_collection__add_methods[] */
static const zend_function_entry mysqlx_collection__add_methods[] = {
	PHP_ME(mysqlx_collection__add, __construct,	nullptr,											ZEND_ACC_PRIVATE)

	PHP_ME(mysqlx_collection__add,	execute,		arginfo_mysqlx_collection__add__execute,	ZEND_ACC_PUBLIC)
	PHP_ME(mysqlx_collection__add,	add,			arginfo_mysqlx_collection__add__add,		ZEND_ACC_PUBLIC)

	{nullptr, nullptr, nullptr}
};
/* }}} */

#if 0
/* {{{ mysqlx_collection__add_property__name */
static zval *
mysqlx_collection__add_property__name(const st_mysqlx_object* obj, zval* return_value)
{
	const Collection_add* object = (const Collection_add *) (obj->ptr);
	DBG_ENTER("mysqlx_collection__add_property__name");
	if (object->collection && object->collection->get_name().s) {
		ZVAL_STRINGL(return_value, object->collection->get_name().s, object->collection->get_name().l);
	} else {
		/*
		  This means EG(uninitialized_value). If we return just return_value, this is an UNDEF-ed value
		  and ISSET will say 'true' while for EG(unin) it is false.
		  In short:
		  return nullptr; -> isset()===false, value is nullptr
		  return return_value; (without doing ZVAL_XXX)-> isset()===true, value is nullptr
		*/
		return_value = nullptr;
	}
	DBG_RETURN(return_value);
}
/* }}} */
#endif

static zend_object_handlers collection_add_handlers;
static HashTable collection_add_properties;

const st_mysqlx_property_entry collection_add_property_entries[] =
{
#if 0
	{{"name",	sizeof("name") - 1}, mysqlx_collection__add_property__name,	nullptr},
#endif
	{{nullptr,	0}, nullptr, nullptr}
};

/* {{{ mysqlx_collection__add_free_storage */
static void
mysqlx_collection__add_free_storage(zend_object* object)
{
	util::free_object<Collection_add>(object);
}
/* }}} */


/* {{{ php_mysqlx_collection__add_object_allocator */
static zend_object *
php_mysqlx_collection__add_object_allocator(zend_class_entry* class_type)
{
	DBG_ENTER("php_mysqlx_collection__add_object_allocator");
	st_mysqlx_object* mysqlx_object = util::alloc_object<Collection_add>(
		class_type,
		&collection_add_handlers,
		&collection_add_properties);
	DBG_RETURN(&mysqlx_object->zo);
}
/* }}} */


/* {{{ mysqlx_register_collection__add_class */
void
mysqlx_register_collection__add_class(UNUSED_INIT_FUNC_ARGS, zend_object_handlers* mysqlx_std_object_handlers)
{
	MYSQL_XDEVAPI_REGISTER_CLASS(
		collection_add_class_entry,
		"CollectionAdd",
		mysqlx_std_object_handlers,
		collection_add_handlers,
		php_mysqlx_collection__add_object_allocator,
		mysqlx_collection__add_free_storage,
		mysqlx_collection__add_methods,
		collection_add_properties,
		collection_add_property_entries,
		mysqlx_executable_interface_entry);

#if 0
	/* The following is needed for the Reflection API */
	zend_declare_property_null(collection_add_class_entry, "name",	sizeof("name") - 1,	ZEND_ACC_PUBLIC);
#endif
}
/* }}} */


/* {{{ mysqlx_unregister_collection__add_class */
void
mysqlx_unregister_collection__add_class(UNUSED_SHUTDOWN_FUNC_ARGS)
{
	zend_hash_destroy(&collection_add_properties);
}
/* }}} */


/* {{{ mysqlx_new_collection__add */
void
mysqlx_new_collection__add(
	zval* return_value,
	xmysqlnd_collection* collection,
	zval* docs,
	int num_of_docs)
{
	DBG_ENTER("mysqlx_new_collection__add");

	if (SUCCESS == object_init_ex(return_value, collection_add_class_entry) && IS_OBJECT == Z_TYPE_P(return_value)) {
		const st_mysqlx_object* const mysqlx_object = Z_MYSQLX_P(return_value);
		Collection_add* const coll_add = static_cast<Collection_add*>(mysqlx_object->ptr);
		if (!coll_add || !coll_add->add_docs(return_value, collection, docs, num_of_docs)) {
			php_error_docref(nullptr, E_WARNING, "invalid object of class %s", ZSTR_VAL(mysqlx_object->zo.ce->name));
			zval_ptr_dtor(return_value);
			ZVAL_NULL(return_value);
		}
	}

	DBG_VOID_RETURN;
}
/* }}} */

} // namespace devapi

} // namespace mysqlx
