/*
 * iptracking
 * yaml-helpers.h
 *
 * Utility routines for the libyaml API.
 *
 */
 
#ifndef __YAML_HELPERS_H__
#define __YAML_HELPERS_H__

#include "iptracking-daemon.h"

/*!
 * @function yaml_helper_doc_node_at_path
 *
 * Given a <path> specification, descend from the given <node> in the
 * <config_doc> and return the terminal node associated with the
 * path.
 *
 * A path consists of dot-separated keys and bracket-delimited
 * indices in sequences.  E.g.
 *
 *     ipv4.addr[2]
 *
 * would select "10.10.10.30" in this document:
 *
 *     ipv4:
 *         addr:
 *             - 10.10.10.10
 *             - 10.10.10.20
 *             - 10.10.10.30
 *             - 10.10.10.40
 *
 * Returns NULL if the path could not be followed.
 */
yaml_node_t* yaml_helper_doc_node_at_path(
    yaml_document_t *config_doc,
    yaml_node_t     *node,
    const char      *path);

/*!
 * @function yaml_helper_get_scalar_value
 *
 * If <node> is a scalar node, return the string value.  Otherwise, returns
 * NULL.
 */
const char* yaml_helper_get_scalar_value(yaml_node_t *node);

/*!
 * @function yaml_helper_get_scalar_int_value
 *
 * If <node> is a scalar node, attempt to parse its value as a C int and
 * set *<value> to the parsed value and return true.  Otherwise *<value>
 * is not modified and false is returned.
 */
bool yaml_helper_get_scalar_int_value(yaml_node_t *node, int *value);

/*!
 * @function yaml_helper_get_scalar_uint32_value
 *
 * If <node> is a scalar node, attempt to parse its value as a uint32_t and
 * set *<value> to the parsed value and return true.  Otherwise *<value>
 * is not modified and false is returned.
 */
bool yaml_helper_get_scalar_uint32_value(yaml_node_t *node, uint32_t *value);

#endif /* __YAML_HELPERS_H__ */
