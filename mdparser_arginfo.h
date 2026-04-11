/* This is a generated file, edit mdparser.stub.php instead.
 * Stub hash: a41000b1112eac6a5762bc2f78699eaa20d47364 */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_MdParser_Options___construct, 0, 0, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, sourcepos, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, hardbreaks, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, nobreaks, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, smart, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, unsafe, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, validateUtf8, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, githubPreLang, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, liberalHtmlTag, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, footnotes, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strikethroughDoubleTilde, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, tablePreferStyleAttributes, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, fullInfoString, _IS_BOOL, 0, "false")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, tables, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strikethrough, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, tasklist, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, autolink, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, tagfilter, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_MdParser_Options_strict, 0, 0, MdParser\\Options, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_MdParser_Options_github arginfo_class_MdParser_Options_strict

#define arginfo_class_MdParser_Options_permissive arginfo_class_MdParser_Options_strict

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_MdParser_Parser___construct, 0, 0, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, options, MdParser\\Options, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_MdParser_Parser_toHtml, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, source, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_MdParser_Parser_toXml arginfo_class_MdParser_Parser_toHtml

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_MdParser_Parser_toAst, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, source, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(MdParser_Options, __construct);
ZEND_METHOD(MdParser_Options, strict);
ZEND_METHOD(MdParser_Options, github);
ZEND_METHOD(MdParser_Options, permissive);
ZEND_METHOD(MdParser_Parser, __construct);
ZEND_METHOD(MdParser_Parser, toHtml);
ZEND_METHOD(MdParser_Parser, toXml);
ZEND_METHOD(MdParser_Parser, toAst);

static const zend_function_entry class_MdParser_Options_methods[] = {
	ZEND_ME(MdParser_Options, __construct, arginfo_class_MdParser_Options___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(MdParser_Options, strict, arginfo_class_MdParser_Options_strict, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(MdParser_Options, github, arginfo_class_MdParser_Options_github, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(MdParser_Options, permissive, arginfo_class_MdParser_Options_permissive, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static const zend_function_entry class_MdParser_Parser_methods[] = {
	ZEND_ME(MdParser_Parser, __construct, arginfo_class_MdParser_Parser___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(MdParser_Parser, toHtml, arginfo_class_MdParser_Parser_toHtml, ZEND_ACC_PUBLIC)
	ZEND_ME(MdParser_Parser, toXml, arginfo_class_MdParser_Parser_toXml, ZEND_ACC_PUBLIC)
	ZEND_ME(MdParser_Parser, toAst, arginfo_class_MdParser_Parser_toAst, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_MdParser_Exception(zend_class_entry *class_entry_RuntimeException)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "MdParser", "Exception", NULL);
	class_entry = zend_register_internal_class_with_flags(&ce, class_entry_RuntimeException, ZEND_ACC_FINAL);

	return class_entry;
}

static zend_class_entry *register_class_MdParser_Options(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "MdParser", "Options", class_MdParser_Options_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_READONLY_CLASS);

	zval property_sourcepos_default_value;
	ZVAL_UNDEF(&property_sourcepos_default_value);
	zend_string *property_sourcepos_name = zend_string_init("sourcepos", sizeof("sourcepos") - 1, true);
	zend_declare_typed_property(class_entry, property_sourcepos_name, &property_sourcepos_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_sourcepos_name, true);

	zval property_hardbreaks_default_value;
	ZVAL_UNDEF(&property_hardbreaks_default_value);
	zend_string *property_hardbreaks_name = zend_string_init("hardbreaks", sizeof("hardbreaks") - 1, true);
	zend_declare_typed_property(class_entry, property_hardbreaks_name, &property_hardbreaks_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_hardbreaks_name, true);

	zval property_nobreaks_default_value;
	ZVAL_UNDEF(&property_nobreaks_default_value);
	zend_string *property_nobreaks_name = zend_string_init("nobreaks", sizeof("nobreaks") - 1, true);
	zend_declare_typed_property(class_entry, property_nobreaks_name, &property_nobreaks_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_nobreaks_name, true);

	zval property_smart_default_value;
	ZVAL_UNDEF(&property_smart_default_value);
	zend_string *property_smart_name = zend_string_init("smart", sizeof("smart") - 1, true);
	zend_declare_typed_property(class_entry, property_smart_name, &property_smart_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_smart_name, true);

	zval property_unsafe_default_value;
	ZVAL_UNDEF(&property_unsafe_default_value);
	zend_string *property_unsafe_name = zend_string_init("unsafe", sizeof("unsafe") - 1, true);
	zend_declare_typed_property(class_entry, property_unsafe_name, &property_unsafe_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_unsafe_name, true);

	zval property_validateUtf8_default_value;
	ZVAL_UNDEF(&property_validateUtf8_default_value);
	zend_string *property_validateUtf8_name = zend_string_init("validateUtf8", sizeof("validateUtf8") - 1, true);
	zend_declare_typed_property(class_entry, property_validateUtf8_name, &property_validateUtf8_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_validateUtf8_name, true);

	zval property_githubPreLang_default_value;
	ZVAL_UNDEF(&property_githubPreLang_default_value);
	zend_string *property_githubPreLang_name = zend_string_init("githubPreLang", sizeof("githubPreLang") - 1, true);
	zend_declare_typed_property(class_entry, property_githubPreLang_name, &property_githubPreLang_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_githubPreLang_name, true);

	zval property_liberalHtmlTag_default_value;
	ZVAL_UNDEF(&property_liberalHtmlTag_default_value);
	zend_string *property_liberalHtmlTag_name = zend_string_init("liberalHtmlTag", sizeof("liberalHtmlTag") - 1, true);
	zend_declare_typed_property(class_entry, property_liberalHtmlTag_name, &property_liberalHtmlTag_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_liberalHtmlTag_name, true);

	zval property_footnotes_default_value;
	ZVAL_UNDEF(&property_footnotes_default_value);
	zend_string *property_footnotes_name = zend_string_init("footnotes", sizeof("footnotes") - 1, true);
	zend_declare_typed_property(class_entry, property_footnotes_name, &property_footnotes_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_footnotes_name, true);

	zval property_strikethroughDoubleTilde_default_value;
	ZVAL_UNDEF(&property_strikethroughDoubleTilde_default_value);
	zend_string *property_strikethroughDoubleTilde_name = zend_string_init("strikethroughDoubleTilde", sizeof("strikethroughDoubleTilde") - 1, true);
	zend_declare_typed_property(class_entry, property_strikethroughDoubleTilde_name, &property_strikethroughDoubleTilde_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_strikethroughDoubleTilde_name, true);

	zval property_tablePreferStyleAttributes_default_value;
	ZVAL_UNDEF(&property_tablePreferStyleAttributes_default_value);
	zend_string *property_tablePreferStyleAttributes_name = zend_string_init("tablePreferStyleAttributes", sizeof("tablePreferStyleAttributes") - 1, true);
	zend_declare_typed_property(class_entry, property_tablePreferStyleAttributes_name, &property_tablePreferStyleAttributes_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_tablePreferStyleAttributes_name, true);

	zval property_fullInfoString_default_value;
	ZVAL_UNDEF(&property_fullInfoString_default_value);
	zend_string *property_fullInfoString_name = zend_string_init("fullInfoString", sizeof("fullInfoString") - 1, true);
	zend_declare_typed_property(class_entry, property_fullInfoString_name, &property_fullInfoString_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_fullInfoString_name, true);

	zval property_tables_default_value;
	ZVAL_UNDEF(&property_tables_default_value);
	zend_string *property_tables_name = zend_string_init("tables", sizeof("tables") - 1, true);
	zend_declare_typed_property(class_entry, property_tables_name, &property_tables_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_tables_name, true);

	zval property_strikethrough_default_value;
	ZVAL_UNDEF(&property_strikethrough_default_value);
	zend_string *property_strikethrough_name = zend_string_init("strikethrough", sizeof("strikethrough") - 1, true);
	zend_declare_typed_property(class_entry, property_strikethrough_name, &property_strikethrough_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_strikethrough_name, true);

	zval property_tasklist_default_value;
	ZVAL_UNDEF(&property_tasklist_default_value);
	zend_string *property_tasklist_name = zend_string_init("tasklist", sizeof("tasklist") - 1, true);
	zend_declare_typed_property(class_entry, property_tasklist_name, &property_tasklist_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_tasklist_name, true);

	zval property_autolink_default_value;
	ZVAL_UNDEF(&property_autolink_default_value);
	zend_string *property_autolink_name = zend_string_init("autolink", sizeof("autolink") - 1, true);
	zend_declare_typed_property(class_entry, property_autolink_name, &property_autolink_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_autolink_name, true);

	zval property_tagfilter_default_value;
	ZVAL_UNDEF(&property_tagfilter_default_value);
	zend_string *property_tagfilter_name = zend_string_init("tagfilter", sizeof("tagfilter") - 1, true);
	zend_declare_typed_property(class_entry, property_tagfilter_name, &property_tagfilter_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL));
	zend_string_release_ex(property_tagfilter_name, true);

	return class_entry;
}

static zend_class_entry *register_class_MdParser_Parser(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "MdParser", "Parser", class_MdParser_Parser_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL);

	zval property_options_default_value;
	ZVAL_UNDEF(&property_options_default_value);
	zend_string *property_options_name = zend_string_init("options", sizeof("options") - 1, true);
	zend_string *property_options_class_MdParser_Options = zend_string_init("MdParser\\Options", sizeof("MdParser\\Options")-1, 1);
	zend_declare_typed_property(class_entry, property_options_name, &property_options_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_CLASS(property_options_class_MdParser_Options, 0, 0));
	zend_string_release_ex(property_options_name, true);

	return class_entry;
}
