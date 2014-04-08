/**
 * @file
 *
 * Routines to query policy capabilities in policy.
 *
 *  @author Richard Haines richard_c_haines@btinternet.com
 *
 * Copyright (C) 2006-2007 Tresys Technology, LLC
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef APOL_BOUNDS_QUERY_H
#define APOL_BOUNDS_QUERY_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include "policy.h"
#include "vector.h"
#include <qpol/policy.h>

			/************ TYPEBOUNDS *************/
	typedef struct apol_typebounds_query apol_typebounds_query_t;
/**
 * Execute a query against all policy capabilities within the policy.
 *
 * @param p Policy within which to look up policy capabilities.
 * @param t Structure containing parameters for query.	If this is
 * NULL then return all policy capabilities.
 * @param v Reference to a vector of qpol_typebounds_t.  The vector will be
 * allocated by this function.  The caller must call
 * apol_vector_destroy() afterwards.  This will be set to NULL upon no
 * results or upon error.
 *
 * @return 0 on success (including none found), negative on error.
 */
	extern int apol_typebounds_get_by_query(const apol_policy_t * p, apol_typebounds_query_t * t, apol_vector_t ** v);

/**
 * Allocate and return a new typebounds query structure.  All fields are
 * initialized, such that running this blank query results in
 * returning all policy capabilities within the policy.  The caller must call
 * apol_typebounds_query_destroy() upon the return value afterwards.
 *
 * @return An initialized typebounds query structure, or NULL upon error.
 */
	extern apol_typebounds_query_t *apol_typebounds_query_create(void);

/**
 * Deallocate all memory associated with the referenced typebounds query,
 * and then set it to NULL.  This function does nothing if the query
 * is already NULL.
 *
 * @param t Reference to a typebounds query structure to destroy.
 */
	extern void apol_typebounds_query_destroy(apol_typebounds_query_t ** t);

/**
 * Set a typebounds query to return only policy capabilities that match this name. This function
 * duplicates the incoming name.
 *
 * @param p Policy handler, to report errors.
 * @param t typebounds query to set.
 * @param name Limit query to only policy capabilities with this name, or
 * NULL to unset this field.
 *
 * @return 0 on success, negative on error.
 */
	extern int apol_typebounds_query_set_name(const apol_policy_t * p, apol_typebounds_query_t * t, const char *name);

/**
 * Set a typebounds query to use regular expression searching for all of its
 * fields.  Strings will be treated as regexes instead of literals.
 * Matching will occur against the policy capability name.
 *
 * @param p Policy handler, to report errors.
 * @param t typebounds query to set.
 * @param is_regex Non-zero to enable regex searching, 0 to disable.
 *
 * @return Always 0.
 */
	extern int apol_typebounds_query_set_regex(const apol_policy_t * p, apol_typebounds_query_t * t, int is_regex);


			/************ ROLEBOUNDS *************/
	typedef struct apol_rolebounds_query apol_rolebounds_query_t;
/**
 * Execute a query against all policy capabilities within the policy.
 *
 * @param p Policy within which to look up policy capabilities.
 * @param t Structure containing parameters for query.	If this is
 * NULL then return all policy capabilities.
 * @param v Reference to a vector of qpol_rolebounds_t.  The vector will be
 * allocated by this function.  The caller must call
 * apol_vector_destroy() afterwards.  This will be set to NULL upon no
 * results or upon error.
 *
 * @return 0 on success (including none found), negative on error.
 */
	extern int apol_rolebounds_get_by_query(const apol_policy_t * p, apol_rolebounds_query_t * t, apol_vector_t ** v);

/**
 * Allocate and return a new rolebounds query structure.  All fields are
 * initialized, such that running this blank query results in
 * returning all policy capabilities within the policy.  The caller must call
 * apol_rolebounds_query_destroy() upon the return value afterwards.
 *
 * @return An initialized rolebounds query structure, or NULL upon error.
 */
	extern apol_rolebounds_query_t *apol_rolebounds_query_create(void);

/**
 * Deallocate all memory associated with the referenced rolebounds query,
 * and then set it to NULL.  This function does nothing if the query
 * is already NULL.
 *
 * @param t Reference to a rolebounds query structure to destroy.
 */
	extern void apol_rolebounds_query_destroy(apol_rolebounds_query_t ** t);

			/************ USERBOUNDS *************/
	typedef struct apol_userbounds_query apol_userbounds_query_t;
/**
 * Execute a query against all policy capabilities within the policy.
 *
 * @param p Policy within which to look up policy capabilities.
 * @param t Structure containing parameters for query.	If this is
 * NULL then return all policy capabilities.
 * @param v Reference to a vector of qpol_userbounds_t.  The vector will be
 * allocated by this function.  The caller must call
 * apol_vector_destroy() afterwards.  This will be set to NULL upon no
 * results or upon error.
 *
 * @return 0 on success (including none found), negative on error.
 */
	extern int apol_userbounds_get_by_query(const apol_policy_t * p, apol_userbounds_query_t * t, apol_vector_t ** v);

/**
 * Allocate and return a new userbounds query structure.  All fields are
 * initialized, such that running this blank query results in
 * returning all policy capabilities within the policy.  The caller must call
 * apol_userbounds_query_destroy() upon the return value afterwards.
 *
 * @return An initialized userbounds query structure, or NULL upon error.
 */
	extern apol_userbounds_query_t *apol_userbounds_query_create(void);

/**
 * Deallocate all memory associated with the referenced userbounds query,
 * and then set it to NULL.  This function does nothing if the query
 * is already NULL.
 *
 * @param t Reference to a userbounds query structure to destroy.
 */
	extern void apol_userbounds_query_destroy(apol_userbounds_query_t ** t);


#ifdef	__cplusplus
}
#endif

#endif
