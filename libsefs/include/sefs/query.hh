/**
 *  @file
 *  Defines the public interface for file context queries.
 *
 *  @author Jeremy A. Mowery jmowery@tresys.com
 *  @author Jason Tang jtang@tresys.com
 *
 *  Copyright (C) 2007 Tresys Technology, LLC
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

#ifndef SEFS_QUERY_H
#define SEFS_QUERY_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <sys/types.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <apol/context-query.h>
#include <apol/mls-query.h>
#include <apol/policy-query.h>
#include <apol/vector.h>

#ifdef __cplusplus
}

#include <stdexcept>

/**
 * This class represents a query into a (subclass of) fclist.  Create
 * a query, fill in all accessors are needed, and then run the query.
 */
class sefs_query
{
	friend class sefs_db;
	friend class sefs_fcfile;
	friend class sefs_filesystem;

      public:

	/**
	 * Allocate and return a new sefs query structure.  All fields
	 * are initialized, such that running this blank query results
	 * in returning all entries within a fclist.
	 */
	 sefs_query();

	~sefs_query();

	/**
	 * Set a sefs query to match only entries with contexts with
	 * the user \a name.
	 * @param name Limit query to only contexts with this user, or
	 * NULL to clear this field.  The string will be duplicated.
	 * @exception std::bad_alloc if out of memory
	 */
	void user(const char *name) throw(std::bad_alloc);

	/**
	 * Set a sefs query to match only entries with contexts with
	 * the role \a name.
	 * @param name Limit query to only contexts with this role, or
	 * NULL to clear this field.  The string will be duplicated.
         * @exception std::bad_alloc if out of memory
	 */
	void role(const char *name) throw(std::bad_alloc);

	/**
	 * Set a sefs query to match only entries with contexts with
	 * the type \a name.
	 * @param name Limit query to only contexts with this type, or
	 * NULL to clear this field.  The string will be duplicated.
	 * @param indirect If the fclist queried has access to a
	 * policy, also match contexts with types in attribute \a name
	 * or types which are an alias for \a name.  If a policy is
	 * not available, this field is ignored, and exact string
	 * matching is used instead.  This paramater is ignored if \a
	 * name is NULL.
	 * @exception std::bad_alloc if out of memory
	 * @see sefs_fclist::associatePolicy() to associate a policy
	 * with a fclist.
	 */
	void type(const char *name, bool indirect) throw(std::bad_alloc);

	/**
	 * Set a sefs query to match only entries with contexts with a
	 * range of \a range.
	 * @param range Limit query to only contexts matching this
	 * string representing the MLS range, or NULL to clear this
	 * field.  The string will be duplicated.
	 * @param match If non-zero and the fclist queried has access
	 * to a policy, match the range using the specified semantics;
	 * this should be one of APOL_QUERY_SUB, APOL_QUERY_SUPER, or
	 * APOL_QUERY_EXACT.  (The range string will be converted
	 * automatically into an apol_mls_range_t object.)  If a
	 * policy is not available or \a match is zero, exact string
	 * matching is used instead.  Note, if a policy is available
	 * the regex flag is ignored if \a match is non-zero.  This
	 * parameter is ignored if \a range is NULL.
	 * @exception std::bad_alloc if out of memory
	 * @see sefs_fclist::associatePolicy() to associate a policy
	 * with a fclist.
	 */
	void range(const char *range, int match) throw(std::bad_alloc);

	/**
	 * Set a sefs query to match only entries with object class \a
	 * objclass.
	 * @param Numeric identifier for an objclass, one of
	 * QPOL_CLASS_FILE, QPOL_CLASS_DIR, etc., as defined in
	 * <qpol/genfscon_query.h>.  Use QPOL_CLASS_ALL to match all
	 * object classes.
	 */
	void objectClass(uint32_t objclass);

	/**
	 * Set a sefs query to match only entries with object class \a
	 * name.
	 * @param name Limit query to only entries with this object
	 * class, or NULL to clear this field.  The incoming string
	 * must be legal according to apol_str_to_objclass().
	 */
	void objectClass(const char *name);

	/**
	 * Set a sefs query to match only entries with path \a path.
	 * @param path Limit query to only entries with this path, or
	 * NULL to clear this field.  The string will be duplicated.
	 * @exception std::bad_alloc if out of memory
	 */
	void path(const char *path) throw(std::bad_alloc);

	/**
	 * Set a sefs query to match only entries with a given inode
	 * number.
	 * @param inode Limit query to only entries with this inode
	 * number, or 0 to clear this field.
	 */
	void inode(ino64_t inode);

	/**
	 * Set a sefs query to match only entries with a given device
	 * number.
	 * @param dev Limit query to only entries with this device
	 * number, or 0 to clear this field.
	 */
	void dev(dev_t dev);

	/**
	 * Set a sefs query to use regular expression matching for
	 * string fields.
	 * @param regex If true then use regular expression matching;
	 * otherwise use only exact string matching.
	 */
	void regex(bool regex);

	/**
	 * Set a sefs query to operate starting at directory \a root.
	 * By default, the query will operate at the topmost of the
	 * fclist and will recurse.
	 * @param root Directory from which to begin the query, or
	 * NULL to clear this field.  This field is not affected by
	 * the sefs_query_set_regex() option.
	 * @param recursive If true operate recursively on all
	 * sub-directories of \a root; otherwise only operate on \a
	 * root not its sub-directories.  This parameter is ignored if
	 * \a root is NULL.
	 * @exception std::bad_alloc if out of memory
	 */
	void rootDir(const char *root, bool recursive) throw(std::bad_alloc);

      private:
	char *_user, *_role, *_type, *_range, *_path, *_root;
	uint32_t _objclass;
	bool _indirect, _regex, _recursive;
	int _rangeMatch;
	ino64_t _inode;
	dev_t _dev;
};

extern "C"
{
#endif

//we do not want to wrap two copies of everything so have SWIG ignore
//the compatibility section.
#ifndef SWIG

	typedef struct sefs_query sefs_query_t;

/**
 * Allocate and return a new sefs query structure.
 * @see sefs_query::sefs_query()
 */
	extern sefs_query_t *sefs_query_create();

/**
 * Deallocate all memory associated with the referenced sefs query,
 * and then set it to NULL.  This function does nothing if the query
 * is already NULL.
 * @param query Reference to a sefs query structure to destroy.
 */
	extern void sefs_query_destroy(sefs_query_t ** query);

/**
 * Set a sefs query to match only entries with contexts with the user
 * \a name.
 * @see sefs_query::user()
 */
	extern int sefs_query_set_user(sefs_query_t * query, const char *name);

/**
 * Set a sefs query to match only entries with contexts with the role
 * \a name.
 * @see sefs_query::role()
 */
	extern int sefs_query_set_role(sefs_query_t * query, const char *name);

/**
 * Set a sefs query to match only entries with contexts with the type
 * \a name.
 * @see sefs_query::type()
 * @see sefs_fclist_associate_policy() to associate a policy with a
 * fclist.
 */
	extern int sefs_query_set_type(sefs_query_t * query, const char *name, bool indirect);

/**
 * Set a sefs query to match only entries with contexts with a range
 * of \a range.
 * @see sefs_query::range()
 * @see sefs_fclist_associate_policy() to associate a policy with a
 * fclist.
 */
	extern int sefs_query_set_range(sefs_query_t * query, const char *range, int match);

/**
 * Set a sefs query to match only entries with object class \a
 * objclass.
 * @return Always 0.
 * @see sefs_query::objectClass(uint32_t)
 */
	extern int sefs_query_set_object_class(sefs_query_t * query, uint32_t objclass);

/**
 * Set a sefs query to match only entries with object class \a name.
 * @return Always 0.
 * @see sefs_query::objectClass(const char *)
 */
	extern int sefs_query_set_object_class_str(sefs_query_t * query, const char *name);

/**
 * Set a sefs query to match only entries with path \a path.
 * @see sefs_query::path()
 */
	extern int sefs_query_set_path(sefs_query_t * query, const char *path);

/**
 * Set a sefs query to match only entries with a given inode number.
 * @return Always 0.
 * @see sefs_query::inode()
 */
	extern int sefs_query_set_inode(sefs_query_t * query, ino64_t inode);

/**
 * Set a sefs query to match only entries with a given device number.
 * @return Always 0.
 * @see sefs_query::dev()
 */
	extern int sefs_query_set_dev(sefs_query_t * query, dev_t dev);

/**
 * Set a sefs query to use regular expression matching for string
 * fields.
 * @return Always 0.
 * @see sefs_query::regex()
 */
	extern int sefs_query_set_regex(sefs_query_t * query, bool regex);

/**
 * Set a sefs query to operate starting at directory \a root.
 * @see sefs_query::rootDir()
 */
	extern int sefs_query_set_root_dir(sefs_query_t * query, const char *root, bool recursive);

#endif				       /* SWIG */

#ifdef __cplusplus
}
#endif

#endif				       /* SEFS_QUERY_H */
