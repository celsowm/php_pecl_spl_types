/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2006 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Marcus Boerger <helly@php.net>                              |
   +----------------------------------------------------------------------+
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"

#include "php_spl_types.h"
#include "ext/spl/spl_functions.h"
#include "ext/spl/spl_engine.h"
#include "ext/spl/spl_iterators.h"
#include "ext/spl/spl_exceptions.h"
#include "spl_type.h"

zend_object_handlers spl_handler_SplType;
PHPAPI zend_class_entry  *spl_ce_SplType;
PHPAPI zend_class_entry  *spl_ce_SplEnum;
PHPAPI zend_class_entry  *spl_ce_SplBool;
PHPAPI zend_class_entry  *spl_ce_SplInt;

static void spl_type_object_free_storage(void *_object TSRMLS_DC) /* {{{ */
{
	spl_type_object *object = (spl_type_object*)_object;

	zend_hash_destroy(object->std.properties);
	FREE_HASHTABLE(object->std.properties);
	
	if (object->properties_copy) {
		zend_hash_destroy(object->properties_copy);
		FREE_HASHTABLE(object->properties_copy);
	}

	zval_ptr_dtor(&object->value);

	efree(object);
}
/* }}} */

static zend_object_value spl_type_object_new_ex(zend_class_entry *class_type, spl_type_object **obj, spl_type_set_t set TSRMLS_DC)
{
	zend_object_value retval;
	spl_type_object *object;
	zval *tmp, **def;

	object = emalloc(sizeof(spl_type_object));
	memset(object, 0, sizeof(spl_type_object));
	object->std.ce = class_type;
	object->set = set;
	/* object->strict = 0; done by set 0 */
	if (obj) *obj = object;

	ALLOC_HASHTABLE(object->std.properties);
	zend_hash_init(object->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(object->std.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref, (void *) &tmp, sizeof(zval *));

	retval.handle = zend_objects_store_put(object, (zend_objects_store_dtor_t) zend_objects_destroy_object, (zend_objects_free_object_storage_t) spl_type_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = &spl_handler_SplType;
	
	zend_update_class_constants(class_type TSRMLS_CC);

	ALLOC_INIT_ZVAL(object->value);

	if (zend_hash_find(&class_type->constants_table, "__default", sizeof("__default"), (void **) &def) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_COMPILE_ERROR, "Class constant %v::__default doesn not exist", class_type->name);
		return retval;
	}

	ZVAL_ZVAL(object->value, *def, 1, 0);

	return retval;
}
/* }}} */

static zend_object_value spl_type_object_clone(zval *zobject TSRMLS_DC)
{
	zend_object_value new_obj_val;
	zend_object *old_object;
	zend_object *new_object;
	zend_object_handle handle = Z_OBJ_HANDLE_P(zobject);
	spl_type_object *object;
	spl_type_object *source;

	old_object = zend_objects_get_address(zobject TSRMLS_CC);
	source = (spl_type_object*)old_object;

	new_obj_val = spl_type_object_new_ex(old_object->ce, &object, source->set TSRMLS_CC);
	new_object = &object->std;

	zend_objects_clone_members(new_object, new_obj_val, old_object, handle TSRMLS_CC);
	
	ZVAL_ZVAL(object->value, source->value, 1, 0);
	object->strict = source->strict;

	return new_obj_val;
}
/* }}} */

static int spl_enum_apply_set(zval **pzconst, spl_type_set_info *inf TSRMLS_DC) /* {{{ */
{
	zval result;
	
	INIT_ZVAL(result);

	if (inf->done || is_equal_function(&result, *pzconst, inf->value TSRMLS_CC) == FAILURE)
	{
		return 0; /*ZEND_HASH_APPLY_KEEP;*/
	}
	if (Z_LVAL(result)) {
		zval_dtor(inf->object->value);
		ZVAL_ZVAL(inf->object->value, *pzconst, 1, 0);
		inf->done = 1;
		return 0; /*ZEND_HASH_APPLY_STOP;*/
	}

	return 0;/*ZEND_HASH_APPLY_KEEP;*/
}
/* }}} */

static int spl_enum_apply_set_strict(zval **pzconst, spl_type_set_info *inf TSRMLS_DC) /* {{{ */
{
	zval result;
	
	INIT_ZVAL(result);

	if (inf->done || is_identical_function(&result, *pzconst, inf->value TSRMLS_CC) == FAILURE)
	{
		return 0; /*ZEND_HASH_APPLY_KEEP;*/
	}
	if (Z_LVAL(result)) {
		zval_dtor(inf->object->value);
		ZVAL_ZVAL(inf->object->value, *pzconst, 1, 0);
		inf->done = 1;
		return 0; /*ZEND_HASH_APPLY_STOP;*/
	}

	return 0;/*ZEND_HASH_APPLY_KEEP;*/
}
/* }}} */

static void spl_type_set_enum(spl_type_set_info *inf TSRMLS_DC) /* {{{ */
{
	if (inf->object->strict) {
		zend_hash_apply_with_argument(&inf->object->std.ce->constants_table, (apply_func_arg_t)spl_enum_apply_set_strict, (void*)inf TSRMLS_CC);
	} else {
		zend_hash_apply_with_argument(&inf->object->std.ce->constants_table, (apply_func_arg_t)spl_enum_apply_set, (void*)inf TSRMLS_CC);
	}
	
	if (!inf->done) {
		zend_throw_exception_ex(spl_ce_UnexpectedValueException, 0 TSRMLS_CC, "Value not a const in enum %v", inf->object->std.ce->name);
	}
}
/* }}} */

static void spl_type_set_int(spl_type_set_info *inf TSRMLS_DC) /* {{{ */
{
	if (inf->object->strict && Z_TYPE_P(inf->value) != IS_LONG) {
		zend_throw_exception_ex(spl_ce_UnexpectedValueException, 0 TSRMLS_CC, "Value not an integer");
	} else {
		zval_dtor(inf->object->value);
		ZVAL_ZVAL(inf->object->value, inf->value, 1, 0);
		convert_to_long_ex(&inf->object->value);
		inf->done = 1;
	}
}
/* }}} */

static void spl_type_object_set(zval **pzobject, zval *value TSRMLS_DC) /* {{{ */
{
	spl_type_set_info inf;

	inf.object = (spl_type_object*)zend_object_store_get_object(*pzobject TSRMLS_CC);
	inf.value  = value;
	inf.done   = 0;

	if (!inf.object->std.ce) {
		zend_throw_exception_ex(spl_ce_RuntimeException, 0 TSRMLS_CC, "Value of type %v was not initialized properly", Z_OBJCE_PP(pzobject)->name);
		return;
	}
	
	if (Z_TYPE_P(value) == IS_OBJECT && Z_OBJ_HANDLER_P(value, get)) {
		inf.value = Z_OBJ_HANDLER_P(value, get)(value TSRMLS_CC);
	}

	inf.object->set(&inf TSRMLS_CC);
}
/* }}} */

static zval* spl_type_object_get(zval *zobject TSRMLS_DC) /* {{{ */
{
	spl_type_object *object = (spl_type_object*)zend_object_store_get_object(zobject TSRMLS_CC);
	zval *value;
	
	MAKE_STD_ZVAL(value);
	ZVAL_ZVAL(value, object->value, 1, 0);
	value->refcount = 0;

	return value;
}

static HashTable* spl_type_object_get_properties(zval *zobject TSRMLS_DC) /* {{{ */
{
	spl_type_object *object = (spl_type_object*)zend_object_store_get_object(zobject TSRMLS_CC);
	zval *tmp;

	if (object->properties_copy) {
		zend_hash_clean(object->properties_copy);
	} else {
		ALLOC_HASHTABLE(object->properties_copy);
		zend_hash_init(object->properties_copy, 0, NULL, ZVAL_PTR_DTOR, 0);
	}
	zend_hash_copy(object->properties_copy, object->std.properties, (copy_ctor_func_t) zval_add_ref, (void*)&tmp, sizeof(zval *));
	
	object->value->refcount++;
	zend_hash_update(object->properties_copy, "__default", sizeof("__default"), (void*)&object->value, sizeof(zval *), (void**)&tmp);

	return object->properties_copy;
}
/* }}} */

#if MBO_0
static int spl_type_object_cast(zval *zobject, zval *writeobj, int type TSRMLS_DC) /* {{{ */
{
	spl_type_object *object = (spl_type_object*)zend_object_store_get_object(zobject TSRMLS_CC);

	ZVAL_ZVAL(writeobj, object->value, 1, 0);
	convert_to_explicit_type(writeobj, type);
	return SUCCESS;
}
/* }}} */
#endif

/* {{{ proto void SplType::__construct(mixed initial_value [, bool strict])
 Cronstructs a enum with given value. */
SPL_METHOD(SplType, __construct)
{
	zval *zobject = getThis();
	zval *value = NULL;
	spl_type_object *object;
	zend_bool strict = 0;

	object = (spl_type_object*)zend_object_store_get_object(zobject TSRMLS_CC);

	php_set_error_handling(EH_THROW, spl_ce_InvalidArgumentException TSRMLS_CC);

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|zb", &value, &strict) == FAILURE) {
		php_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
		return;
	}

	object->strict = strict;
	if (ZEND_NUM_ARGS()) {
		spl_type_object_set(&zobject, value TSRMLS_CC);
	}

	php_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

static zend_object_value spl_enum_object_new(zend_class_entry *class_type TSRMLS_DC) /* {{{ */
{
	return spl_type_object_new_ex(class_type, NULL, spl_type_set_enum TSRMLS_CC);
}
/* }}} */

static zend_object_value spl_int_object_new(zend_class_entry *class_type TSRMLS_DC) /* {{{ */
{
	return spl_type_object_new_ex(class_type, NULL, spl_type_set_int TSRMLS_CC);
}
/* }}} */

static zend_function_entry spl_funcs_SplType[] = {
	SPL_ME(SplType, __construct,   NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

PHP_MINIT_FUNCTION(spl_type) /* {{{ */
{
	REGISTER_SPL_STD_CLASS_EX(SplType, spl_enum_object_new, spl_funcs_SplType);
	memcpy(&spl_handler_SplType, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	spl_handler_SplType.clone_obj      = spl_type_object_clone;
	spl_handler_SplType.get            = spl_type_object_get;
	spl_handler_SplType.set            = spl_type_object_set;
	spl_handler_SplType.get_properties = spl_type_object_get_properties;
#if MBO_0
	spl_handler_SplType.cast_object    = spl_type_object_cast;
#else
	spl_handler_SplType.cast_object    = NULL;
#endif
	
	zend_declare_class_constant_null(spl_ce_SplType, "__default", sizeof("__default") - 1 TSRMLS_CC);

	spl_ce_SplType->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

	REGISTER_SPL_SUB_CLASS_EX(SplEnum, SplType, spl_enum_object_new, NULL);
	spl_ce_SplEnum->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

	REGISTER_SPL_SUB_CLASS_EX(SplBool, SplEnum, spl_enum_object_new, NULL);
	zend_declare_class_constant_bool(spl_ce_SplBool, "false", sizeof("false") - 1, 0 TSRMLS_CC);
	zend_declare_class_constant_bool(spl_ce_SplBool, "true", sizeof("true") - 1, 1 TSRMLS_CC);
	zend_declare_class_constant_bool(spl_ce_SplBool, "__default", sizeof("__default") - 1, 0 TSRMLS_CC);

	REGISTER_SPL_SUB_CLASS_EX(SplInt, SplType, spl_int_object_new, NULL);
	zend_declare_class_constant_long(spl_ce_SplInt, "__default", sizeof("__default") - 1, 0 TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: noet sw=4 ts=4
 */