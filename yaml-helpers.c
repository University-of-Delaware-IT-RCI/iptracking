/*
 * iptracking
 * yaml-helpers.c
 *
 * Utility routines for the libyaml API.
 *
 */
 
#include "iptracking-daemon.h"
#include "yaml-helpers.h"

//

yaml_node_t*
yaml_helper_doc_node_at_path(
    yaml_document_t *config_doc,
    yaml_node_t     *node,
    int             level,
    const char      *path
)
{
    /* Do we actually have a node? */
    if ( ! node ) return NULL;
    
    /* Return this node? */
    if ( ! *path ) return node;
    
    switch ( node->type ) {
        case YAML_NO_NODE:
            return NULL;
            
        case YAML_SCALAR_NODE:
            if ( ! *path ) return node;
            break;
        
        case YAML_SEQUENCE_NODE: {
            long                idx;
            char                *endptr;
            yaml_node_item_t    *item = node->data.sequence.items.start;
            
            if ( *path != '[' ) return NULL;
            idx = strtol(++path, &endptr, 0);
            if ( (idx < 0) || (endptr == path) ) return NULL;
            if ( *path != ']' ) return NULL;
            path++;
            item += idx;
            if ( item < node->data.sequence.items.top ) {
                return yaml_helper_doc_node_at_path(config_doc, yaml_document_get_node(config_doc, *item), level + 1, path);
            }
            break;
        }
        
        case YAML_MAPPING_NODE: {
            yaml_node_pair_t    *ps, *pe;
            const char          *endptr = path;
            
            if ( level > 0 ) {
                if ( *path != '.' ) return NULL;
                endptr = ++path;
            }
            while ( *endptr && (*endptr != '.') && (*endptr != '[') ) endptr++;
            if ( endptr == path ) return NULL;
            ps = node->data.mapping.pairs.start;
            pe = node->data.mapping.pairs.top;
            while ( ps < pe ) {
                yaml_node_t     *k = yaml_document_get_node(config_doc, ps->key);
                
                if ( k && (k->type == YAML_SCALAR_NODE) ) {
                    size_t      s_len = endptr - path;
                    
                    if ( (s_len == k->data.scalar.length) && (strncmp(path, (const char*)k->data.scalar.value, s_len) == 0) ) {
                        return yaml_helper_doc_node_at_path(config_doc, yaml_document_get_node(config_doc, ps->value), level + 1, endptr);
                    }
                }
                ps++;
            }
            break;
        }
    
    }
    return NULL;
}

//

const char*
yaml_helper_get_scalar_value(
    yaml_node_t *node
)
{
    if ( node->type == YAML_SCALAR_NODE ) return (const char*)node->data.scalar.value;
    return NULL;
}

//

bool
yaml_helper_get_scalar_int_value(
    yaml_node_t *node,
    int         *value
)
{
    if ( node->type == YAML_SCALAR_NODE ) {
        long    int_val;
        char    *endptr;
        
        int_val = strtol((const char*)node->data.scalar.value, &endptr, 0);
        if ( (endptr > (char*)node->data.scalar.value) && !*endptr ) {
            if ( (int_val >= INT_MIN) && (int_val <= INT_MAX) ) {
                *value = (int)int_val;
                return true;
            }
        }
    }
    return false;
}

//

bool
yaml_helper_get_scalar_uint32_value(
    yaml_node_t *node,
    uint32_t    *value
)
{
    if ( node->type == YAML_SCALAR_NODE ) {
        long long   int_val;
        char        *endptr;
        
        int_val = strtoll((const char*)node->data.scalar.value, &endptr, 0);
        if ( (endptr > (char*)node->data.scalar.value) && !*endptr ) {
            if ( (int_val >= 0) && (int_val <= (long long)UINT32_MAX) ) {
                *value = (uint32_t)int_val;
                return true;
            }
        }
    }
    return false;
}
