/*
 * iptracking
 * ipset_helper.c
 *
 * Version-agnostic API for ipset changes.
 *
 */
 
#include "ipset_helper.h"

//

static int __ipset_helper_types_loaded = 0;
    
//
    
#ifndef HAVE_IPSET_SESSION_ERROR
#   define ipset_session_error(S)  ipset_session_report_msg((S))
#endif

//

#ifdef USE_IPSET_IPSET_API

#   include <libipset/ipset.h>
    
    //

    #define ipset_session_error(I)      ipset_session_report_msg(ipset_session((I)))

    //
    
    int
    __ipset_helper_custom_errorfn(
        struct ipset    *ipset,
        void            *p,
        int             status,
        const char      *msg,
        ...
    )
    {
        return status;
    }
    
    int
    __ipset_helper_standard_errorfn(
        struct ipset    *ipset,
        void            *p
    )
    {
        return -1;
    }
    
    int
    __ipset_print_outfn(
        struct ipset_session    *session,
        void                    *p,
        const char              *fmt,
        ...
    )
    {
        return 0;
    }

    //

    ipset_helper_t*
    ipset_helper_init(void)
    {
        ipset_helper_t  *new_set = NULL;
        
        if ( ! __ipset_helper_types_loaded ) {
            ipset_load_types();
            __ipset_helper_types_loaded = 1;
        }
        new_set = ipset_init();
        if ( new_set ) {
            ipset_custom_printf(new_set, __ipset_helper_custom_errorfn, __ipset_helper_standard_errorfn, __ipset_print_outfn, NULL);
        }
        return new_set;
    }
    
    //
    
    int
    ipset_helper_fini(
        ipset_helper_t  *an_ipset
    )
    {
        return ipset_fini(an_ipset);
    }
    
    //
    
    int
    ipset_helper_create(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild
    )
    {
        const char*     argv[] = { "ipset", "create", set_name_rebuild, "hash:net" };
        int             argc = sizeof(argv) / sizeof(const char*);
        int             rc;
        
        return ipset_parse_argv(an_ipset, argc, (char**)argv);
    }
    
    //
    
    int
    ipset_helper_add(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild,
        const char      *an_ip_entity
    )
    {
        const char*     argv[] = { "ipset", "add", set_name_rebuild, an_ip_entity, "-exist" };
        int             argc = sizeof(argv) / sizeof(const char*);
        
        return ipset_parse_argv(an_ipset, argc, (char**)argv);
    }
    
    //
    
    int
    ipset_helper_activate(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild,
        const char      *set_name_prod
    )
    {
        const char*     argv[] = { "ipset", "swap", set_name_rebuild, set_name_prod };
        int             argc = sizeof(argv) / sizeof(const char*);
        int             rc;
        bool            should_destroy = true;
        
        rc = ipset_parse_argv(an_ipset, argc, (char**)argv);
        if ( rc != 0 ) {
            argv[1] = "rename";
            rc = ipset_parse_argv(an_ipset, argc, (char**)argv);
            if ( rc == 0 ) should_destroy = false;
        }
        if ( should_destroy) {
            const char* argv[] = { "ipset", "destroy", set_name_rebuild };
            int         argc = sizeof(argv) / sizeof(const char*);
            
            rc = ipset_parse_argv(an_ipset, argc, (char**)argv);
        }
        return rc;
    }
    
    //
    
    int
    ipset_helper_destroy(
        ipset_helper_t  *an_ipset,
        const char      *set_name
    )
    {
        const char* argv[] = { "ipset", "destroy", set_name };
        int         argc = sizeof(argv) / sizeof(const char*);
        
        return ipset_parse_argv(an_ipset, argc, (char**)argv);
    }

#else

#   include <libipset/linux_ip_set.h>		/* IPSET_CMD_* */
#   include <libipset/data.h>			    /* enum ipset_data */
#   include <libipset/types.h>			    /* IPSET_*_ARG */
#   include <libipset/session.h>			/* ipset_envopt_parse */
#   include <libipset/parse.h>			    /* ipset_parse_family */

    //
    
    struct ipset_helper {
        struct ipset_session    *session;
        const struct ipset_type *set_type;
    };
    
    //
    
#if IPSET_VERSION > 700
    int
    __ipset_helper_print_outfn(
        struct ipset_session    *session,
        void                    *p,
        const char              *fmt,
        ...
    )
    {
        return 0;
    }
#else
    int
    __ipset_helper_print_outfn(
        const char  *fmt,
        ...
    )
    {
        return -1;
    }
#endif

    //

    ipset_helper_t*
    ipset_helper_init(void)
    {
        ipset_helper_t  *new_ipset = calloc(1, sizeof(ipset_helper_t));
        
        if ( ! __ipset_helper_types_loaded ) {
            ipset_load_types();
            __ipset_helper_types_loaded = 1;
        }
        
        if ( new_ipset ) {
#   ifdef HAVE_IPSET_SESSION_INIT_ARGS_2
            new_ipset->session = ipset_session_init(__ipset_helper_print_outfn, NULL);
#   else
            new_ipset->session = ipset_session_init(__ipset_helper_print_outfn);
#   endif
            if ( new_ipset->session ) {
                ipset_session_output(new_ipset->session, IPSET_LIST_NONE);
            } else {
                free((void*)new_ipset);
                new_ipset = NULL;
            }
        }
        return new_ipset;
    }
    
    //
    
    int
    ipset_helper_fini(
        ipset_helper_t  *an_ipset
    )
    {
        if ( an_ipset->session ) ipset_session_fini(an_ipset->session);
        free((void*)an_ipset);
        return 0;
    }
    
    //
    
    int
    ipset_helper_create(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild
    )
    {
        int             rc = -1;
        
        if ( an_ipset->session ) {
            ipset_data_reset(ipset_session_data(an_ipset->session));
            rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name_rebuild);
            if ( rc == 0 ) {
                rc = ipset_parse_typename(an_ipset->session, IPSET_OPT_TYPENAME, "hash:net");
                if ( rc == 0 ) {
                    an_ipset->set_type = ipset_type_get(an_ipset->session, IPSET_CMD_CREATE);
                    if ( an_ipset->set_type ) {
                        rc = ipset_cmd(an_ipset->session, IPSET_CMD_CREATE, 1);
                    } else {
                        rc = -22;
                    }
                }
            }
        }
        return rc;
    }
    
    //
    
    int
    ipset_helper_add(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild,
        const char      *an_ip_entity
    )
    {
        int             rc = -1;
        
        if ( an_ipset->session ) {
            ipset_data_reset(ipset_session_data(an_ipset->session));
            ipset_session_data_set(an_ipset->session, IPSET_OPT_TYPE, an_ipset->set_type);
            rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name_rebuild);
            if ( rc == 0 ) {
                rc = ipset_parse_elem(an_ipset->session, an_ipset->set_type->last_elem_optional, an_ip_entity);
                if ( rc == 0 ) {
#   ifdef HAVE_IPSET_ENVOPT_SET
                    ipset_envopt_set(an_ipset->session, IPSET_ENV_EXIST);
#   else
                    ipset_envopt_parse(an_ipset->session, IPSET_ENV_EXIST, NULL);
#   endif
                    rc = ipset_cmd(an_ipset->session, IPSET_CMD_ADD, 2);
                }
            }
        }
        return rc;
    }
    
    //
    
    int
    ipset_helper_activate(
        ipset_helper_t  *an_ipset,
        const char      *set_name_rebuild,
        const char      *set_name_prod
    )
    {
        int             rc = -1;
        bool            should_destroy = true;
        
        if ( an_ipset->session ) {
            ipset_data_reset(ipset_session_data(an_ipset->session));
            rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name_rebuild);
            if ( rc == 0 ) {
                rc = ipset_parse_setname(an_ipset->session, IPSET_OPT_SETNAME2, set_name_prod);
                if ( rc == 0 ) {
                    rc = ipset_cmd(an_ipset->session, IPSET_CMD_SWAP, 3);
                    if ( rc != 0 ) {
                        /* Rename: */
                        ipset_data_reset(ipset_session_data(an_ipset->session));
                        rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name_rebuild);
                        if ( rc == 0 ) {
                            rc = ipset_parse_setname(an_ipset->session, IPSET_OPT_SETNAME2, set_name_prod);
                            if ( rc == 0 ) {
                                rc = ipset_cmd(an_ipset->session, IPSET_CMD_RENAME, 4);
                                if ( rc == 0 ) should_destroy = false;
                            }
                        }
                    }
                }
            }
            if ( should_destroy ) {
                int     d_rc;
                
                ipset_data_reset(ipset_session_data(an_ipset->session));
                d_rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name_rebuild);
                d_rc = ipset_cmd(an_ipset->session, IPSET_CMD_DESTROY, 5);
            }
        }
        return rc;
    }
    
    //
    
    int
    ipset_helper_destroy(
        ipset_helper_t  *an_ipset,
        const char      *set_name
    )
    {
        int             rc = -1;
        
        if ( an_ipset->session ) {
            ipset_data_reset(ipset_session_data(an_ipset->session));
            rc = ipset_parse_setname(an_ipset->session, IPSET_SETNAME, set_name);
            if ( rc == 0 ) {
                rc = ipset_cmd(an_ipset->session, IPSET_CMD_DESTROY, 6);
            }
        }
        return rc;
    }

#endif
    
//

const char*
ipset_helper_last_error_message(
    ipset_helper_t  *an_ipset
)
{
    static char     __last_error_message[256];
    const char      *session_error_str = ipset_session_error(an_ipset->session);
    
    __last_error_message[0] = '\0';
    if ( session_error_str && *session_error_str ) {
        const char  *s = session_error_str, *e;
        
        // Skip any leading whitespace:
        while ( *s && isspace(*s) ) s++;
        
        // Find the end of the string:
        e = s;
        while ( *e ) e++;
        
        // e now points to the terminating NUL character; backtrack
        // to the first non-NUL, non-whitespace character:
        while ( e > s && (*e || isspace(*e)) ) e--;
        
        if ( e > s ) {
            int     nchar = e - s;
            
            if ( nchar >= sizeof(__last_error_message) ) {
                nchar = sizeof(__last_error_message) - 1;
            }
            memcpy(__last_error_message, s, nchar);
            __last_error_message[nchar] = '\0';
        }
    }
    return __last_error_message;
}
