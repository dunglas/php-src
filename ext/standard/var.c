/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2014 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Jani Lehtimäki <jkl@njet.net>                               |
   |          Thies C. Arntzen <thies@thieso.net>                         |
   |          Sascha Schumann <sascha@schumann.cx>                        |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

/* {{{ includes
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "php.h"
#include "php_string.h"
#include "php_var.h"
#include "php_smart_str.h"
#include "basic_functions.h"
#include "php_incomplete_class.h"

#define COMMON (is_ref ? "&" : "")
/* }}} */

static uint zend_obj_num_elements(HashTable *ht)
{
	Bucket *p;
	uint idx;
	uint num;

	num = ht->nNumOfElements;
	for (idx = 0; idx < ht->nNumUsed; idx++) {
		p = ht->arData + idx;
		if (Z_TYPE(p->val) == IS_UNDEF) continue;
		if (Z_TYPE(p->val) == IS_INDIRECT) {
			if (Z_TYPE_P(Z_INDIRECT(p->val)) == IS_UNDEF) {
				num--;
			}			
		}
	}
	return num;
}

static void php_array_element_dump(zval *zv, zend_ulong index, zend_string *key, int level TSRMLS_DC) /* {{{ */
{
	if (key == NULL) { /* numeric key */
		php_printf("%*c[" ZEND_LONG_FMT "]=>\n", level + 1, ' ', index);
	} else { /* string key */
		php_printf("%*c[\"", level + 1, ' ');
		PHPWRITE(key->val, key->len);
		php_printf("\"]=>\n");
		}
	php_var_dump(zv, level + 2 TSRMLS_CC);
}
/* }}} */

static void php_object_property_dump(zval *zv, zend_ulong index, zend_string *key, int level TSRMLS_DC) /* {{{ */
{
	const char *prop_name, *class_name;

	if (key == NULL) { /* numeric key */
		php_printf("%*c[" ZEND_LONG_FMT "]=>\n", level + 1, ' ', index);
	} else { /* string key */
		int unmangle = zend_unmangle_property_name(key->val, key->len, &class_name, &prop_name);
		php_printf("%*c[", level + 1, ' ');

		if (class_name && unmangle == SUCCESS) {
			if (class_name[0] == '*') {
				php_printf("\"%s\":protected", prop_name);
			} else {
				php_printf("\"%s\":\"%s\":private", prop_name, class_name);
			}
		} else {
			php_printf("\"");
			PHPWRITE(key->val, key->len);
			php_printf("\"");
		}
		ZEND_PUTS("]=>\n");
	}
	php_var_dump(zv, level + 2 TSRMLS_CC);
}
/* }}} */

PHPAPI void php_var_dump(zval *struc, int level TSRMLS_DC) /* {{{ */
{
	HashTable *myht;
	zend_string *class_name;
	int is_temp;
	int is_ref = 0;
	zend_ulong num;
	zend_string *key;
	zval *val;

	if (level > 1) {
		php_printf("%*c", level - 1, ' ');
	}

again:
	switch (Z_TYPE_P(struc)) {
		case IS_FALSE:
			php_printf("%sbool(false)\n", COMMON);
			break;
		case IS_TRUE:
			php_printf("%sbool(true)\n", COMMON);
			break;
		case IS_NULL:
			php_printf("%sNULL\n", COMMON);
			break;
		case IS_LONG:
			php_printf("%sint(" ZEND_LONG_FMT ")\n", COMMON, Z_LVAL_P(struc));
			break;
		case IS_DOUBLE:
			php_printf("%sfloat(%.*G)\n", COMMON, (int) EG(precision), Z_DVAL_P(struc));
			break;
		case IS_STRING:
			php_printf("%sstring(%d) \"", COMMON, Z_STRLEN_P(struc));
			PHPWRITE(Z_STRVAL_P(struc), Z_STRLEN_P(struc));
			PUTS("\"\n");
			break;
		case IS_ARRAY:
			myht = Z_ARRVAL_P(struc);
			if (level > 1 && ZEND_HASH_APPLY_PROTECTION(myht) && ++myht->u.v.nApplyCount > 1) {
				PUTS("*RECURSION*\n");
				--myht->u.v.nApplyCount;
				return;
			}
			php_printf("%sarray(%d) {\n", COMMON, zend_hash_num_elements(myht));
			is_temp = 0;

			ZEND_HASH_FOREACH_KEY_VAL_IND(myht, num, key, val) {
				php_array_element_dump(val, num, key, level TSRMLS_CC);
			} ZEND_HASH_FOREACH_END();
			if (level > 1 && ZEND_HASH_APPLY_PROTECTION(myht)) {
				--myht->u.v.nApplyCount;
			}
			if (is_temp) {
				zend_hash_destroy(myht);
				efree(myht);
			}
			if (level > 1) {
				php_printf("%*c", level-1, ' ');
			}
			PUTS("}\n");
			break;
		case IS_OBJECT:
			myht = Z_OBJDEBUG_P(struc, is_temp);
			if (myht && ++myht->u.v.nApplyCount > 1) {
				PUTS("*RECURSION*\n");
				--myht->u.v.nApplyCount;
				return;
			}

			if (Z_OBJ_HANDLER_P(struc, get_class_name)) {
				class_name = Z_OBJ_HANDLER_P(struc, get_class_name)(Z_OBJ_P(struc), 0 TSRMLS_CC);
				php_printf("%sobject(%s)#%d (%d) {\n", COMMON, class_name->val, Z_OBJ_HANDLE_P(struc), myht ? zend_obj_num_elements(myht) : 0);
				zend_string_release(class_name);
			} else {
				php_printf("%sobject(unknown class)#%d (%d) {\n", COMMON, Z_OBJ_HANDLE_P(struc), myht ? zend_obj_num_elements(myht) : 0);
			}
			if (myht) {
				zend_ulong num;
				zend_string *key;
				zval *val;

				ZEND_HASH_FOREACH_KEY_VAL_IND(myht, num, key, val) {
					php_object_property_dump(val, num, key, level TSRMLS_CC);
				} ZEND_HASH_FOREACH_END();
				--myht->u.v.nApplyCount;
				if (is_temp) {
					zend_hash_destroy(myht);
					efree(myht);
				}
			}
			if (level > 1) {
				php_printf("%*c", level-1, ' ');
			}
			PUTS("}\n");
			break;
		case IS_RESOURCE: {
			const char *type_name = zend_rsrc_list_get_rsrc_type(Z_RES_P(struc) TSRMLS_CC);
			php_printf("%sresource(%pd) of type (%s)\n", COMMON, Z_RES_P(struc)->handle, type_name ? type_name : "Unknown");
			break;
		}
		case IS_REFERENCE:
			//??? hide references with refcount==1 (for compatibility)
			if (Z_REFCOUNT_P(struc) > 1) {
				is_ref = 1;
			}
			struc = Z_REFVAL_P(struc);
			goto again;
			break;
		default:
			php_printf("%sUNKNOWN:0\n", COMMON);
			break;
	}
}
/* }}} */

/* {{{ proto void var_dump(mixed var)
   Dumps a string representation of variable to output */
PHP_FUNCTION(var_dump)
{
	zval *args;
	int argc;
	int	i;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "+", &args, &argc) == FAILURE) {
		return;
	}

	for (i = 0; i < argc; i++) {
		php_var_dump(&args[i], 1 TSRMLS_CC);
	}
}
/* }}} */

static void zval_array_element_dump(zval *zv, zend_ulong index, zend_string *key, int level TSRMLS_DC) /* {{{ */
{
	if (key == NULL) { /* numeric key */
		php_printf("%*c[" ZEND_LONG_FMT "]=>\n", level + 1, ' ', index);
	} else { /* string key */
		php_printf("%*c[\"", level + 1, ' ');
		PHPWRITE(key->val, key->len);
		php_printf("\"]=>\n");
	}
	php_debug_zval_dump(zv, level + 2 TSRMLS_CC);
}
/* }}} */

static void zval_object_property_dump(zval *zv, zend_ulong index, zend_string *key, int level TSRMLS_DC) /* {{{ */
{
	const char *prop_name, *class_name;

	if (key == NULL) { /* numeric key */
		php_printf("%*c[" ZEND_LONG_FMT "]=>\n", level + 1, ' ', index);
	} else { /* string key */
		zend_unmangle_property_name(key->val, key->len, &class_name, &prop_name);
		php_printf("%*c[", level + 1, ' ');

		if (class_name) {
			if (class_name[0] == '*') {
				php_printf("\"%s\":protected", prop_name);
			} else {
				php_printf("\"%s\":\"%s\":private", prop_name, class_name);
			}
		} else {
			php_printf("\"%s\"", prop_name);
		}
		ZEND_PUTS("]=>\n");
	}
	php_debug_zval_dump(zv, level + 2 TSRMLS_CC);
}
/* }}} */

PHPAPI void php_debug_zval_dump(zval *struc, int level TSRMLS_DC) /* {{{ */
{
	HashTable *myht = NULL;
	zend_string *class_name;
	int is_temp = 0;
	int is_ref = 0;
	zend_ulong index;
	zend_string *key;
	zval *val;

	if (level > 1) {
		php_printf("%*c", level - 1, ' ');
	}

again:
	switch (Z_TYPE_P(struc)) {
	case IS_FALSE:
		php_printf("%sbool(false)\n", COMMON);
		break;
	case IS_TRUE:
		php_printf("%sbool(true)\n", COMMON);
		break;
	case IS_NULL:
		php_printf("%sNULL\n", COMMON);
		break;
	case IS_LONG:
		php_printf("%slong(" ZEND_LONG_FMT ")\n", COMMON, Z_LVAL_P(struc));
		break;
	case IS_DOUBLE:
		php_printf("%sdouble(%.*G)\n", COMMON, (int) EG(precision), Z_DVAL_P(struc));
		break;
	case IS_STRING:
		php_printf("%sstring(%d) \"", COMMON, Z_STRLEN_P(struc));
		PHPWRITE(Z_STRVAL_P(struc), Z_STRLEN_P(struc));
		php_printf("\" refcount(%u)\n", IS_INTERNED(Z_STR_P(struc)) ? 1 : Z_REFCOUNT_P(struc));
		break;
	case IS_ARRAY:
		myht = Z_ARRVAL_P(struc);
		if (level > 1 && ZEND_HASH_APPLY_PROTECTION(myht) && myht->u.v.nApplyCount++ > 1) {
			myht->u.v.nApplyCount--;
			PUTS("*RECURSION*\n");
			return;
		}
		php_printf("%sarray(%d) refcount(%u){\n", COMMON, zend_hash_num_elements(myht), Z_REFCOUNTED_P(struc) ? Z_REFCOUNT_P(struc) : 1);
		ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, val) {
			zval_array_element_dump(val, index, key, level TSRMLS_CC);
		} ZEND_HASH_FOREACH_END();
		if (level > 1 && ZEND_HASH_APPLY_PROTECTION(myht)) {
			myht->u.v.nApplyCount--;
		}
		if (is_temp) {
			zend_hash_destroy(myht);
			efree(myht);
		}
		if (level > 1) {
			php_printf("%*c", level - 1, ' ');
		}
		PUTS("}\n");
		break;
	case IS_OBJECT:
		myht = Z_OBJDEBUG_P(struc, is_temp);
		if (myht) {
			if (myht->u.v.nApplyCount > 1) {
				PUTS("*RECURSION*\n");
				return;
			} else {
				myht->u.v.nApplyCount++;
			}
		}
		class_name = Z_OBJ_HANDLER_P(struc, get_class_name)(Z_OBJ_P(struc), 0 TSRMLS_CC);
		php_printf("%sobject(%s)#%d (%d) refcount(%u){\n", COMMON, class_name->val, Z_OBJ_HANDLE_P(struc), myht ? zend_obj_num_elements(myht) : 0, Z_REFCOUNT_P(struc));
		zend_string_release(class_name);
		if (myht) {
			ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, val) {
				zval_object_property_dump(val, index, key, level TSRMLS_CC);
			} ZEND_HASH_FOREACH_END();
			myht->u.v.nApplyCount--;
			if (is_temp) {
				zend_hash_destroy(myht);
				efree(myht);
			}
		}
		if (level > 1) {
			php_printf("%*c", level - 1, ' ');
		}
		PUTS("}\n");
		break;
	case IS_RESOURCE: {
		const char *type_name = zend_rsrc_list_get_rsrc_type(Z_RES_P(struc) TSRMLS_CC);
		php_printf("%sresource(" ZEND_LONG_FMT ") of type (%s) refcount(%u)\n", COMMON, Z_RES_P(struc)->handle, type_name ? type_name : "Unknown", Z_REFCOUNT_P(struc));
		break;
	}
	case IS_REFERENCE:
		//??? hide references with refcount==1 (for compatibility)
		if (Z_REFCOUNT_P(struc) > 1) {
			is_ref = 1;
		}
		struc = Z_REFVAL_P(struc);
		goto again;
	default:
		php_printf("%sUNKNOWN:0\n", COMMON);
		break;
	}
}
/* }}} */

/* {{{ proto void debug_zval_dump(mixed var)
   Dumps a string representation of an internal zend value to output. */
PHP_FUNCTION(debug_zval_dump)
{
	zval *args;
	int argc;
	int	i;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "+", &args, &argc) == FAILURE) {
		return;
	}

	for (i = 0; i < argc; i++) {
		php_debug_zval_dump(&args[i], 1 TSRMLS_CC);
	}
}
/* }}} */

#define buffer_append_spaces(buf, num_spaces) \
	do { \
		char *tmp_spaces; \
		int tmp_spaces_len; \
		tmp_spaces_len = spprintf(&tmp_spaces, 0,"%*c", num_spaces, ' '); \
		smart_str_appendl(buf, tmp_spaces, tmp_spaces_len); \
		efree(tmp_spaces); \
	} while(0);

static void php_array_element_export(zval *zv, zend_ulong index, zend_string *key, int level, smart_str *buf TSRMLS_DC) /* {{{ */
{
	if (key == NULL) { /* numeric key */
		buffer_append_spaces(buf, level+1);
		smart_str_append_long(buf, (zend_long) index);
		smart_str_appendl(buf, " => ", 4);

	} else { /* string key */
		zend_string *tmp_str;
		zend_string *ckey = php_addcslashes(key->val, key->len, 0, "'\\", 2 TSRMLS_CC);
		tmp_str = php_str_to_str_ex(ckey->val, ckey->len, "\0", 1, "' . \"\\0\" . '", 12, 0, NULL);

		buffer_append_spaces(buf, level + 1);

		smart_str_appendc(buf, '\'');
		smart_str_appendl(buf, tmp_str->val, tmp_str->len);
		smart_str_appendl(buf, "' => ", 5);

		zend_string_free(ckey);
		zend_string_free(tmp_str);
	}
	php_var_export_ex(zv, level + 2, buf TSRMLS_CC);

	smart_str_appendc(buf, ',');
	smart_str_appendc(buf, '\n');
}
/* }}} */

static void php_object_element_export(zval *zv, zend_ulong index, zend_string *key, int level, smart_str *buf TSRMLS_DC) /* {{{ */
{
	buffer_append_spaces(buf, level + 2);
	if (key != NULL) {
		const char *class_name; /* ignored, but must be passed to unmangle */
		const char *pname;
		zend_string *pname_esc;
		
		zend_unmangle_property_name(key->val, key->len,
				&class_name, &pname);
		pname_esc = php_addcslashes(pname, strlen(pname), 0, "'\\", 2 TSRMLS_CC);

		smart_str_appendc(buf, '\'');
		smart_str_appendl(buf, pname_esc->val, pname_esc->len);
		smart_str_appendc(buf, '\'');
		zend_string_release(pname_esc);
	} else {
		smart_str_append_long(buf, (zend_long) index);
	}
	smart_str_appendl(buf, " => ", 4);
	php_var_export_ex(zv, level + 2, buf TSRMLS_CC);
	smart_str_appendc(buf, ',');
	smart_str_appendc(buf, '\n');
}
/* }}} */

PHPAPI void php_var_export_ex(zval *struc, int level, smart_str *buf TSRMLS_DC) /* {{{ */
{
	HashTable *myht;
	char *tmp_str;
	size_t tmp_len;
	zend_string *class_name;
	zend_string *ztmp, *ztmp2;
	zend_ulong index;
	zend_string *key;
	zval *val;

again:
	switch (Z_TYPE_P(struc)) {
		case IS_FALSE:
			smart_str_appendl(buf, "false", 5);
			break;
		case IS_TRUE:
			smart_str_appendl(buf, "true", 4);
			break;
		case IS_NULL:
			smart_str_appendl(buf, "NULL", 4);
			break;
		case IS_LONG:
			smart_str_append_long(buf, Z_LVAL_P(struc));
			break;
		case IS_DOUBLE:
			tmp_len = spprintf(&tmp_str, 0,"%.*H", PG(serialize_precision), Z_DVAL_P(struc));
			smart_str_appendl(buf, tmp_str, tmp_len);
			efree(tmp_str);
			break;
		case IS_STRING:
			ztmp = php_addcslashes(Z_STRVAL_P(struc), Z_STRLEN_P(struc), 0, "'\\", 2 TSRMLS_CC);
			ztmp2 = php_str_to_str_ex(ztmp->val, ztmp->len, "\0", 1, "' . \"\\0\" . '", 12, 0, NULL);

			smart_str_appendc(buf, '\'');
			smart_str_appendl(buf, ztmp2->val, ztmp2->len);
			smart_str_appendc(buf, '\'');

			zend_string_free(ztmp);
			zend_string_free(ztmp2);
			break;
		case IS_ARRAY:
			myht = Z_ARRVAL_P(struc);
			if (ZEND_HASH_APPLY_PROTECTION(myht) && myht->u.v.nApplyCount++ > 0) {
				myht->u.v.nApplyCount--;
				smart_str_appendl(buf, "NULL", 4);
				zend_error(E_WARNING, "var_export does not handle circular references");
				return;
			}
			if (level > 1) {
				smart_str_appendc(buf, '\n');
				buffer_append_spaces(buf, level - 1);
			}
			smart_str_appendl(buf, "array (\n", 8);
			ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, val) {
				php_array_element_export(val, index, key, level, buf TSRMLS_CC);
			} ZEND_HASH_FOREACH_END();
			if (ZEND_HASH_APPLY_PROTECTION(myht)) {
				myht->u.v.nApplyCount--;
			}
			if (level > 1) {
				buffer_append_spaces(buf, level - 1);
			}
			smart_str_appendc(buf, ')');
		
			break;

		case IS_OBJECT:
			myht = Z_OBJPROP_P(struc);
			if (myht) {
				if (myht->u.v.nApplyCount > 0) {
					smart_str_appendl(buf, "NULL", 4);
					zend_error(E_WARNING, "var_export does not handle circular references");
					return;
				} else {
					myht->u.v.nApplyCount++;
				}
			}
			if (level > 1) {
				smart_str_appendc(buf, '\n');
				buffer_append_spaces(buf, level - 1);
			}
			class_name = Z_OBJ_HANDLER_P(struc, get_class_name)(Z_OBJ_P(struc), 0 TSRMLS_CC);

			smart_str_appendl(buf, class_name->val, class_name->len);
			smart_str_appendl(buf, "::__set_state(array(\n", 21);

			zend_string_release(class_name);
			if (myht) {
				ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, val) {
					php_object_element_export(val, index, key, level, buf TSRMLS_CC);
				} ZEND_HASH_FOREACH_END();
				myht->u.v.nApplyCount--;
			}
			if (level > 1) {
				buffer_append_spaces(buf, level - 1);
			}
			smart_str_appendl(buf, "))", 2);

			break;
		case IS_REFERENCE:
			struc = Z_REFVAL_P(struc);
			goto again;
			break;
		default:
			smart_str_appendl(buf, "NULL", 4);
			break;
	}
}
/* }}} */

/* FOR BC reasons, this will always perform and then print */
PHPAPI void php_var_export(zval *struc, int level TSRMLS_DC) /* {{{ */
{
	smart_str buf = {0};
	php_var_export_ex(struc, level, &buf TSRMLS_CC);
	smart_str_0(&buf);
	PHPWRITE(buf.s->val, buf.s->len);
	smart_str_free(&buf);
}
/* }}} */


/* {{{ proto mixed var_export(mixed var [, bool return])
   Outputs or returns a string representation of a variable */
PHP_FUNCTION(var_export)
{
	zval *var;
	zend_bool return_output = 0;
	smart_str buf = {0};

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|b", &var, &return_output) == FAILURE) {
		return;
	}

	php_var_export_ex(var, 1, &buf TSRMLS_CC);
	smart_str_0 (&buf);

	if (return_output) {
		RETURN_STR(buf.s);
	} else {
		PHPWRITE(buf.s->val, buf.s->len);
		smart_str_free(&buf);
	}
}
/* }}} */

static void php_var_serialize_intern(smart_str *buf, zval *struc, HashTable *var_hash TSRMLS_DC);

static inline int php_add_var_hash(HashTable *var_hash, zval *var_ptr, zval *var_old TSRMLS_DC) /* {{{ */
{
	zval var_no, *zv;
	char id[32], *p;
	register int len;
	zval *var = var_ptr;

	if (Z_ISREF_P(var)) {
		var = Z_REFVAL_P(var);
	}
	if ((Z_TYPE_P(var) == IS_OBJECT) && Z_OBJ_HT_P(var)->get_class_entry) {
		p = smart_str_print_long(id + sizeof(id) - 1,
				(zend_long) Z_OBJ_P(var));
		*(--p) = 'O';
		len = id + sizeof(id) - 1 - p;
	} else if (var_ptr != var) {
		p = smart_str_print_long(id + sizeof(id) - 1,
				(zend_long) Z_REF_P(var_ptr));
		*(--p) = 'R';
		len = id + sizeof(id) - 1 - p;
	} else {
		p = smart_str_print_long(id + sizeof(id) - 1, (zend_long) var);
		len = id + sizeof(id) - 1 - p;
	}

	if ((zv = zend_hash_str_find(var_hash, p, len)) != NULL) {
		ZVAL_COPY_VALUE(var_old, zv);
		if (var == var_ptr) {
			/* we still need to bump up the counter, since non-refs will
			 * be counted separately by unserializer */
			ZVAL_LONG(&var_no, -1);
			zend_hash_next_index_insert(var_hash, &var_no);
		}
#if 0
		fprintf(stderr, "- had var (%d): %lu\n", Z_TYPE_P(var), **(zend_ulong**)var_old);
#endif
		return FAILURE;
	}

	/* +1 because otherwise hash will think we are trying to store NULL pointer */
	ZVAL_LONG(&var_no, zend_hash_num_elements(var_hash) + 1);
	zend_hash_str_add(var_hash, p, len, &var_no);
#if 0
	fprintf(stderr, "+ add var (%d): %lu\n", Z_TYPE_P(var), Z_LVAL(var_no));
#endif
	return SUCCESS;
}
/* }}} */

static inline void php_var_serialize_long(smart_str *buf, zend_long val) /* {{{ */
{
	smart_str_appendl(buf, "i:", 2);
	smart_str_append_long(buf, val);
	smart_str_appendc(buf, ';');
}
/* }}} */

static inline void php_var_serialize_string(smart_str *buf, char *str, int len) /* {{{ */
{
	smart_str_appendl(buf, "s:", 2);
	smart_str_append_long(buf, len);
	smart_str_appendl(buf, ":\"", 2);
	smart_str_appendl(buf, str, len);
	smart_str_appendl(buf, "\";", 2);
}
/* }}} */

static inline zend_bool php_var_serialize_class_name(smart_str *buf, zval *struc TSRMLS_DC) /* {{{ */
{
	PHP_CLASS_ATTRIBUTES;

	PHP_SET_CLASS_ATTRIBUTES(struc);
	smart_str_appendl(buf, "O:", 2);
	smart_str_append_long(buf, (int)class_name->len);
	smart_str_appendl(buf, ":\"", 2);
	smart_str_appendl(buf, class_name->val, class_name->len);
	smart_str_appendl(buf, "\":", 2);
	PHP_CLEANUP_CLASS_ATTRIBUTES();
	return incomplete_class;
}
/* }}} */

static void php_var_serialize_class(smart_str *buf, zval *struc, zval *retval_ptr, HashTable *var_hash TSRMLS_DC) /* {{{ */
{
	int count;
	zend_bool incomplete_class;

	incomplete_class = php_var_serialize_class_name(buf, struc TSRMLS_CC);
	/* count after serializing name, since php_var_serialize_class_name
	 * changes the count if the variable is incomplete class */
	count = zend_hash_num_elements(HASH_OF(retval_ptr));
	if (incomplete_class) {
		--count;
	}
	smart_str_append_long(buf, count);
	smart_str_appendl(buf, ":{", 2);

	if (count > 0) {
		zend_string *key;
		zval *d, *name;
		zval nval, *nvalp;
		HashTable *propers, *ht;

		ZVAL_NULL(&nval);
		nvalp = &nval;

		ht = HASH_OF(retval_ptr);
		ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, name) {
			if (incomplete_class && strcmp(key->val, MAGIC_MEMBER) == 0) {
				continue;
			}

			if (Z_TYPE_P(name) != IS_STRING) {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "__sleep should return an array only containing the names of instance-variables to serialize.");
				/* we should still add element even if it's not OK,
				 * since we already wrote the length of the array before */
				smart_str_appendl(buf,"N;", 2);
				continue;
			}
			propers = Z_OBJPROP_P(struc);
			if ((d = zend_hash_find(propers, Z_STR_P(name))) != NULL) {
				if (Z_TYPE_P(d) == IS_INDIRECT) {
					d = Z_INDIRECT_P(d);
					if (Z_TYPE_P(d) == IS_UNDEF) {
						continue;
					}
				}
				php_var_serialize_string(buf, Z_STRVAL_P(name), Z_STRLEN_P(name));
				php_var_serialize_intern(buf, d, var_hash TSRMLS_CC);
			} else {
				zend_class_entry *ce;
				ce = zend_get_class_entry(Z_OBJ_P(struc) TSRMLS_CC);
				if (ce) {
					zend_string *prot_name, *priv_name;

					do {
						priv_name = zend_mangle_property_name(ce->name->val, ce->name->len, Z_STRVAL_P(name), Z_STRLEN_P(name), ce->type & ZEND_INTERNAL_CLASS);
						if ((d = zend_hash_find(propers, priv_name)) != NULL) {
							if (Z_TYPE_P(d) == IS_INDIRECT) {
								d = Z_INDIRECT_P(d);
								if (Z_ISUNDEF_P(d)) {
									break;
								}
							}
							php_var_serialize_string(buf, priv_name->val, priv_name->len);
							zend_string_free(priv_name);
							php_var_serialize_intern(buf, d, var_hash TSRMLS_CC);
							break;
						}
						zend_string_free(priv_name);
						prot_name = zend_mangle_property_name("*", 1, Z_STRVAL_P(name), Z_STRLEN_P(name), ce->type & ZEND_INTERNAL_CLASS);
						if ((d = zend_hash_find(propers, prot_name)) != NULL) {
							if (Z_TYPE_P(d) == IS_INDIRECT) {
								d = Z_INDIRECT_P(d);
								if (Z_TYPE_P(d) == IS_UNDEF) {
									zend_string_free(prot_name);
									break;
								}
							}
							php_var_serialize_string(buf, prot_name->val, prot_name->len);
							zend_string_free(prot_name);
							php_var_serialize_intern(buf, d, var_hash TSRMLS_CC);
							break;
						}
						zend_string_free(prot_name);
						php_var_serialize_string(buf, Z_STRVAL_P(name), Z_STRLEN_P(name));
						php_var_serialize_intern(buf, nvalp, var_hash TSRMLS_CC);
						php_error_docref(NULL TSRMLS_CC, E_NOTICE, "\"%s\" returned as member variable from __sleep() but does not exist", Z_STRVAL_P(name));
					} while (0);
				} else {
					php_var_serialize_string(buf, Z_STRVAL_P(name), Z_STRLEN_P(name));
					php_var_serialize_intern(buf, nvalp, var_hash TSRMLS_CC);
				}
			}
		} ZEND_HASH_FOREACH_END();
	}
	smart_str_appendc(buf, '}');
}
/* }}} */

static void php_var_serialize_intern(smart_str *buf, zval *struc, HashTable *var_hash TSRMLS_DC) /* {{{ */
{
	int i;
	zval var_already;
	HashTable *myht;

	if (EG(exception)) {
		return;
	}

	ZVAL_UNDEF(&var_already);

	if (var_hash &&
		php_add_var_hash(var_hash, struc, &var_already TSRMLS_CC) == FAILURE) {
		if (Z_ISREF_P(struc)) {
			smart_str_appendl(buf, "R:", 2);
			smart_str_append_long(buf, Z_LVAL(var_already));
			smart_str_appendc(buf, ';');
			return;
		} else if (Z_TYPE_P(struc) == IS_OBJECT) {
			smart_str_appendl(buf, "r:", 2);
			smart_str_append_long(buf, Z_LVAL(var_already));
			smart_str_appendc(buf, ';');
			return;
		}
	}

again:
	switch (Z_TYPE_P(struc)) {
		case IS_FALSE:
			smart_str_appendl(buf, "b:0;", 4);
			return;

		case IS_TRUE:
			smart_str_appendl(buf, "b:1;", 4);
			return;

		case IS_NULL:
			smart_str_appendl(buf, "N;", 2);
			return;

		case IS_LONG:
			php_var_serialize_long(buf, Z_LVAL_P(struc));
			return;

		case IS_DOUBLE: {
				char *s;

				smart_str_appendl(buf, "d:", 2);
				s = (char *) safe_emalloc(PG(serialize_precision), 1, MAX_LENGTH_OF_DOUBLE + 1);
				php_gcvt(Z_DVAL_P(struc), PG(serialize_precision), '.', 'E', s);
				smart_str_appends(buf, s);
				smart_str_appendc(buf, ';');
				efree(s);
				return;
			}

		case IS_STRING:
			php_var_serialize_string(buf, Z_STRVAL_P(struc), Z_STRLEN_P(struc));
			return;

		case IS_OBJECT: {
				zval retval;
				zval fname;
				int res;
				zend_class_entry *ce = NULL;

				if (Z_OBJ_HT_P(struc)->get_class_entry) {
					ce = Z_OBJCE_P(struc);
				}

				if (ce && ce->serialize != NULL) {
					/* has custom handler */
					unsigned char *serialized_data = NULL;
					uint32_t serialized_length;

					if (ce->serialize(struc, &serialized_data, &serialized_length, (zend_serialize_data *)var_hash TSRMLS_CC) == SUCCESS) {
						smart_str_appendl(buf, "C:", 2);
						smart_str_append_long(buf, (int)Z_OBJCE_P(struc)->name->len);
						smart_str_appendl(buf, ":\"", 2);
						smart_str_appendl(buf, Z_OBJCE_P(struc)->name->val, Z_OBJCE_P(struc)->name->len);
						smart_str_appendl(buf, "\":", 2);

						smart_str_append_long(buf, (int)serialized_length);
						smart_str_appendl(buf, ":{", 2);
						smart_str_appendl(buf, serialized_data, serialized_length);
						smart_str_appendc(buf, '}');
					} else {
						smart_str_appendl(buf, "N;", 2);
					}
					if (serialized_data) {
						efree(serialized_data);
					}
					return;
				}

				if (ce && ce != PHP_IC_ENTRY && zend_hash_str_exists(&ce->function_table, "__sleep", sizeof("__sleep")-1)) {
					ZVAL_STRINGL(&fname, "__sleep", sizeof("__sleep") - 1);
					BG(serialize_lock)++;
					res = call_user_function_ex(CG(function_table), struc, &fname, &retval, 0, 0, 1, NULL TSRMLS_CC);
					BG(serialize_lock)--;
					zval_dtor(&fname);
                    
					if (EG(exception)) {
						zval_ptr_dtor(&retval);
						return;
					}

					if (res == SUCCESS) {
						if (Z_TYPE(retval) != IS_UNDEF) {
							if (HASH_OF(&retval)) {
								php_var_serialize_class(buf, struc, &retval, var_hash TSRMLS_CC);
							} else {
								php_error_docref(NULL TSRMLS_CC, E_NOTICE, "__sleep should return an array only containing the names of instance-variables to serialize");
								/* we should still add element even if it's not OK,
								 * since we already wrote the length of the array before */
								smart_str_appendl(buf,"N;", 2);
							}
							zval_ptr_dtor(&retval);
						}
						return;
					}
					zval_ptr_dtor(&retval);
				}

				/* fall-through */
			}
		case IS_ARRAY: {
			zend_bool incomplete_class = 0;
			if (Z_TYPE_P(struc) == IS_ARRAY) {
				smart_str_appendl(buf, "a:", 2);
				myht = HASH_OF(struc);
			} else {
				incomplete_class = php_var_serialize_class_name(buf, struc TSRMLS_CC);
				myht = Z_OBJPROP_P(struc);
			}
			/* count after serializing name, since php_var_serialize_class_name
			 * changes the count if the variable is incomplete class */
			i = myht ? zend_hash_num_elements(myht) : 0;
			if (i > 0 && incomplete_class) {
				--i;
			}
			smart_str_append_long(buf, i);
			smart_str_appendl(buf, ":{", 2);
			if (i > 0) {
				zend_string *key;
				zval *data;
				zend_ulong index;

				ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, data) {

					if (incomplete_class && strcmp(key->val, MAGIC_MEMBER) == 0) {
						continue;
					}

					if (!key) {
						php_var_serialize_long(buf, index);
					} else {
						php_var_serialize_string(buf, key->val, key->len);
					}

					/* we should still add element even if it's not OK,
					 * since we already wrote the length of the array before */
					if ((Z_TYPE_P(data) == IS_ARRAY && Z_TYPE_P(struc) == IS_ARRAY && Z_ARR_P(data) == Z_ARR_P(struc))
						|| (Z_TYPE_P(data) == IS_ARRAY && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)
					) {
						smart_str_appendl(buf, "N;", 2);
					} else {
						if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data))) {
							Z_ARRVAL_P(data)->u.v.nApplyCount++;
						}
						php_var_serialize_intern(buf, data, var_hash TSRMLS_CC);
						if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data))) {
							Z_ARRVAL_P(data)->u.v.nApplyCount--;
						}
					}
				} ZEND_HASH_FOREACH_END();
			}
			smart_str_appendc(buf, '}');
			return;
		}
		case IS_REFERENCE:
			struc = Z_REFVAL_P(struc);
			goto again;
		default:
			smart_str_appendl(buf, "i:0;", 4);
			return;
	}
}
/* }}} */

PHPAPI void php_var_serialize(smart_str *buf, zval *struc, php_serialize_data_t *var_hash TSRMLS_DC) /* {{{ */
{
	php_var_serialize_intern(buf, struc, *var_hash TSRMLS_CC);
	smart_str_0(buf);
}
/* }}} */

/* {{{ proto string serialize(mixed variable)
   Returns a string representation of variable (which can later be unserialized) */
PHP_FUNCTION(serialize)
{
	zval *struc;
	php_serialize_data_t var_hash;
	smart_str buf = {0};

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &struc) == FAILURE) {
		return;
	}

	PHP_VAR_SERIALIZE_INIT(var_hash);
	php_var_serialize(&buf, struc, &var_hash TSRMLS_CC);
	PHP_VAR_SERIALIZE_DESTROY(var_hash);

	if (EG(exception)) {
		smart_str_free(&buf);
		RETURN_FALSE;
	}

	if (buf.s) {
		RETURN_STR(buf.s);
	} else {
		RETURN_NULL();
	}
}
/* }}} */

/* {{{ proto mixed unserialize(string variable_representation)
   Takes a string representation of variable and recreates it */
PHP_FUNCTION(unserialize)
{
	char *buf = NULL;
	size_t buf_len;
	const unsigned char *p;
	php_unserialize_data_t var_hash;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &buf, &buf_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (buf_len == 0) {
		RETURN_FALSE;
	}

	p = (const unsigned char*) buf;
	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	if (!php_var_unserialize(return_value, &p, p + buf_len, &var_hash TSRMLS_CC)) {
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
		zval_dtor(return_value);
		if (!EG(exception)) {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Error at offset " ZEND_LONG_FMT " of %d bytes", (zend_long)((char*)p - buf), buf_len);
		}
		RETURN_FALSE;
	}
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
}
/* }}} */

/* {{{ proto int memory_get_usage([real_usage])
   Returns the allocated by PHP memory */
PHP_FUNCTION(memory_get_usage) {
	zend_bool real_usage = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &real_usage) == FAILURE) {
		RETURN_FALSE;
	}

	RETURN_LONG(zend_memory_usage(real_usage TSRMLS_CC));
}
/* }}} */

/* {{{ proto int memory_get_peak_usage([real_usage])
   Returns the peak allocated by PHP memory */
PHP_FUNCTION(memory_get_peak_usage) {
	zend_bool real_usage = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &real_usage) == FAILURE) {
		RETURN_FALSE;
	}

	RETURN_LONG(zend_memory_peak_usage(real_usage TSRMLS_CC));
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
