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

//

yaml_node_t* yaml_helper_doc_node_at_path(
    yaml_document_t *config_doc,
    yaml_node_t     *node,
    int             level,
    const char      *path);


const char* yaml_helper_get_scalar_value(yaml_node_t *node);
bool yaml_helper_get_scalar_int_value(yaml_node_t *node, int *value);
bool yaml_helper_get_scalar_uint32_value(yaml_node_t *node, uint32_t *value);

#endif /* __YAML_HELPERS_H__ */
