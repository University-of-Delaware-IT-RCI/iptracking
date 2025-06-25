/*
 * iptracking
 * ipset_helper.h
 *
 * Version-agnostic API for ipset changes.
 *
 */

#ifndef __IPSET_HELPER_H__
#define __IPSET_HELPER_H__

#include "iptracking.h"

/*
 * There are a variety of macros dictating libipset
 * API availability in the source for ipset_helper.
 * These are primarily determined by the version of
 * the in-use library.  The version is parsed from
 * `ipset --version` output with the major version
 * multiplied by 100 and the minor version added to
 * it:  e.g. 7.1 => 710.
 *
 * In the 7.x and newer versions of the library the
 * higher-level ipset API can be used to submit text-
 * based commands to be handled just as they would
 * with the `ipset` CLI utility.  Use of that API
 * can be enabled by the builder's setting
 * USE_IPSET_IPSET_API=ON in the CMake configuration
 * for the build.
 *
 */
#if IPSET_VERSION > 700
#   undef HAVE_IPSET_SESSION_ERROR
#   define HAVE_IPSET_SESSION_INIT_ARGS_2
#   define HAVE_IPSET_ENVOPT_SET
#elif IPSET_VERSION > 600
#   undef USE_IPSET_IPSET_API
#   define HAVE_IPSET_SESSION_ERROR
#   undef HAVE_IPSET_SESSION_INIT_ARGS_2
#   undef HAVE_IPSET_ENVOPT_SET
#else
#   error Unsupported or missing libipset version
#endif

//

#ifdef USE_IPSET_IPSET_API
/*!
 * @typedef ipset_helper_t
 *
 * If the ipset API is used, we alias our type to the type of
 * ipset objects.  Otherwise, it's a type defined in the source
 * side of this package.  Either way, it's pointers to these
 * objects that get passed around in the helper API.
 */
typedef struct ipset ipset_helper_t;
#else
typedef struct ipset_helper ipset_helper_t;
#endif

/*!
 * @function ipset_helper_init
 *
 * Create a new ipset helper interface and return a pointer to
 * it.  It is up to the caller to ultimately terminate the interface
 * and reclaim all resources by calling <ipset_helper_fini()> on the
 * returned pointer.
 */
ipset_helper_t* ipset_helper_init(void);

/*!
 * @function ipset_helper_fini
 *
 * Terminate the helper interface and reclaim all resources associated
 * with <an_ipset>.
 */
int ipset_helper_fini(ipset_helper_t *an_ipset);

/*!
 * @function ipset_helper_create
 *
 * Create a new ipset named <set_name_rebuild>.
 *
 * @return Zero on success, non-zero on failure.
 */
int ipset_helper_create(ipset_helper_t *an_ipset, const char *set_name_rebuild);
int ipset_helper_add(ipset_helper_t *an_ipset, const char *set_name_rebuild, const char *an_ip_entity);
int ipset_helper_activate(ipset_helper_t *an_ipset, const char *set_name_rebuild, const char *set_name_prod);
int ipset_helper_destroy(ipset_helper_t *an_ipset, const char *set_name);


const char* ipset_helper_last_error_message(ipset_helper_t *an_ipset);

#endif /* __IPSET_HELPER_H__ */

