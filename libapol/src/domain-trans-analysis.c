/**
 * @file
 *
 * Routines to perform a domain transition analysis.
 *
 * @author Jeremy A. Mowery jmowery@tresys.com
 * @author Jason Tang  jtang@tresys.com
 *
 * Copyright (C) 2005-2007 Tresys Technology, LLC
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

#include "policy-query-internal.h"
#include "domain-trans-analysis-internal.h"
#include <apol/domain-trans-analysis.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

/* private data structure definitions */

/** rule container with meta data */
typedef struct apol_domain_trans_rule
{
	/** relavant type :
	 * for domain nodes either the transition target or the entrypoint type (target)
	 * for exec nodes either the entered or calling domain (source) */
	const qpol_type_t *type;
	/** default type, only for type_transition rules */
	const qpol_type_t *dflt;
	/** vector of rules (of type qpol_avrule_t or qpol_terule_t) */
	apol_vector_t *rules;
	/** used flag, marks that a rule has previously been returned, not used for setexec rules */
	bool used;
	/** for exec_rules domain also has execute_no_trans permission */
	bool has_no_trans;
} apol_domain_trans_rule_t;

/** node representing a domain and all rules contributing to its transitions */
typedef struct apol_domain_trans_dom_node
{
	/** vector of allow process transition rules (of type apol_domain_trans_rule_t w/ qpol_avrule_t) */
	apol_vector_t *proc_trans_rules;
	/** vector of allow file entrypoint rules (of type apol_domain_trans_rule_t w/ qpol_avrule_t) */
	apol_vector_t *ep_rules;
	/** vector of allow self process setexec rules (of type apol_domain_trans_rule_t w/ qpol_avrule_t) */
	apol_vector_t *setexec_rules;
	/** vector of type transition rules (of type apol_domain_trans_rule_t w/ qpol_terule_t) */
	apol_vector_t *type_trans_rules;
} apol_domain_trans_dom_node_t;

/** node representing an executable type and all rules allowing its use in transitions */
typedef struct apol_domain_trans_exec_node
{
	/** vector of allow file execute rules (of type apol_domain_trans_rule_t w/ qpol_avrule_t) */
	apol_vector_t *exec_rules;
	/** vector of allow file entrypoint rules (of type apol_domain_trans_rule_t w/ qpol_avrule_t) */
	apol_vector_t *ep_rules;
} apol_domain_trans_exec_node_t;

/** internal representation of a potential transition */
typedef struct apol_domain_trans
{
	const qpol_type_t *start_type;
	const qpol_type_t *ep_type;
	const qpol_type_t *end_type;
	apol_vector_t *proc_trans_rules;
	apol_vector_t *ep_rules;
	apol_vector_t *exec_rules;
	apol_vector_t *setexec_rules;
	apol_vector_t *type_trans_rules;
	/** flag indicating that the transition is possible */
	bool valid;
	/** used for access filtering, this is only populated on demand */
	apol_vector_t *access_rules;
	struct apol_domain_trans *next;
} apol_domain_trans_t;

/* public data structure definitions */
struct apol_domain_trans_analysis
{
	unsigned char direction;
	unsigned char valid;
	char *start_type;
	char *result;
	apol_vector_t *access_types;
	apol_vector_t *access_class_perms;
	regex_t *result_regex;
};

struct apol_domain_trans_result
{
	const qpol_type_t *start_type;
	const qpol_type_t *ep_type;
	const qpol_type_t *end_type;
	apol_vector_t *proc_trans_rules;
	apol_vector_t *ep_rules;
	apol_vector_t *exec_rules;
	apol_vector_t *setexec_rules;
	apol_vector_t *type_trans_rules;
	bool valid;
	/** if access filters used list of rules that satisfy
	 * the filter criteria (of type qpol_avrule_t) */
	apol_vector_t *access_rules;
};

struct apol_domain_trans_table
{
	size_t size;		       /* size == number of types in policy (including attributes) */
	apol_domain_trans_dom_node_t *dom_list;	/* these two arrays are indexed by type value -1 */
	apol_domain_trans_exec_node_t *exec_list;	/* there will be holes for attributes (which are expanded) */
};

/* private functions */

static void apol_domain_trans_rule_free(void *r)
{
	apol_domain_trans_rule_t *rule = r;

	if (!r)
		return;

	apol_vector_destroy(&rule->rules);
	free(r);
}

static apol_domain_trans_table_t *apol_domain_trans_table_new(apol_policy_t * policy)
{
	apol_domain_trans_table_t *new_table = NULL;
	apol_vector_t *v = NULL;
	size_t size = 0, i;
	int error;

	if (!policy) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return NULL;
	}

	new_table = (apol_domain_trans_table_t *) calloc(1, sizeof(apol_domain_trans_table_t));
	if (!new_table) {
		ERR(policy, "%s", strerror(ENOMEM));
		error = ENOMEM;
		goto cleanup;
	}

	apol_type_get_by_query(policy, NULL, &v);
	size += apol_vector_get_size(v);
	apol_vector_destroy(&v);
	apol_attr_get_by_query(policy, NULL, &v);
	size += apol_vector_get_size(v);
	apol_vector_destroy(&v);

	new_table->size = size;

	new_table->dom_list = (apol_domain_trans_dom_node_t *) calloc(new_table->size, sizeof(apol_domain_trans_dom_node_t));
	if (!new_table->dom_list) {
		ERR(policy, "%s", strerror(ENOMEM));
		error = ENOMEM;
		goto cleanup;
	}
	new_table->exec_list = (apol_domain_trans_exec_node_t *) calloc(new_table->size, sizeof(apol_domain_trans_exec_node_t));
	if (!new_table->exec_list) {
		ERR(policy, "%s", strerror(ENOMEM));
		error = ENOMEM;
		goto cleanup;
	}

	for (i = 0; i < new_table->size; i++) {
		/* create all the vectors for each side of the table, return error if any fails */
		if (!(new_table->dom_list[i].proc_trans_rules = apol_vector_create(apol_domain_trans_rule_free)) ||
		    !(new_table->dom_list[i].ep_rules = apol_vector_create(apol_domain_trans_rule_free)) ||
		    !(new_table->dom_list[i].setexec_rules = apol_vector_create(apol_domain_trans_rule_free)) ||
		    !(new_table->dom_list[i].type_trans_rules = apol_vector_create(apol_domain_trans_rule_free)) ||
		    !(new_table->exec_list[i].exec_rules = apol_vector_create(apol_domain_trans_rule_free)) ||
		    !(new_table->exec_list[i].ep_rules = apol_vector_create(apol_domain_trans_rule_free))) {
			ERR(policy, "%s", strerror(ENOMEM));
			error = ENOMEM;
			goto cleanup;
		}
	}

	return new_table;
      cleanup:
	domain_trans_table_destroy(&new_table);
	errno = error;
	return NULL;
}

static apol_domain_trans_t *apol_domain_trans_new(void)
{
	apol_domain_trans_t *new_trans = NULL;
	int error;

	new_trans = (apol_domain_trans_t *) calloc(1, sizeof(apol_domain_trans_t));
	if (!new_trans) {
		return NULL;
	}

	new_trans->access_rules = apol_vector_create(NULL);
	if (!new_trans->access_rules) {
		error = errno;
		free(new_trans);
		errno = error;
		return NULL;
	}

	return new_trans;
}

static void apol_domain_trans_dom_node_free(apol_domain_trans_dom_node_t * node)
{
	if (!node)
		return;

	apol_vector_destroy(&node->proc_trans_rules);
	apol_vector_destroy(&node->ep_rules);
	apol_vector_destroy(&node->setexec_rules);
	apol_vector_destroy(&node->type_trans_rules);
}

static void apol_domain_trans_exec_node_free(apol_domain_trans_exec_node_t * node)
{
	if (!node)
		return;

	apol_vector_destroy(&node->exec_rules);
	apol_vector_destroy(&node->ep_rules);
}

static void apol_domain_trans_destroy(apol_domain_trans_t ** trans)
{
	apol_domain_trans_t *trx = NULL, *next = NULL;

	if (!trans || !(*trans))
		return;

	for (trx = *trans; trx; trx = next) {
		apol_vector_destroy(&trx->proc_trans_rules);
		apol_vector_destroy(&trx->ep_rules);
		apol_vector_destroy(&trx->exec_rules);
		apol_vector_destroy(&trx->setexec_rules);
		apol_vector_destroy(&trx->type_trans_rules);
		apol_vector_destroy(&trx->access_rules);
		next = trx->next;
		free(trx);
	}

	*trans = NULL;
}

static apol_domain_trans_rule_t *apol_domain_trans_find_rule_for_type(const apol_policy_t * policy, const apol_vector_t * rule_list,
								      const qpol_type_t * type)
{
	int list_sz = apol_vector_get_size(rule_list);
	int left = 0, right = list_sz - 1;
	int i = list_sz / 2;
	apol_domain_trans_rule_t *rule = NULL;
	uint32_t type_val, rule_type_val;
	unsigned char isattr = 0;

	qpol_type_get_isattr(policy->p, type, &isattr);
	if (isattr) {
		ERR(policy, "%s", "Attributes are not valid here.");
		errno = EINVAL;
		return NULL;
	}

	if (!rule_list || list_sz == 0)
		return NULL;	       /* empty list, not necessarily an error */

	qpol_type_get_value(policy->p, type, &type_val);

	/* potentially a lot of entries but list is sorted so
	 * we can do a binary search */
	do {
		rule = apol_vector_get_element(rule_list, i);
		qpol_type_get_value(policy->p, rule->type, &rule_type_val);
		if (rule_type_val == type_val) {
			return rule;
		} else if (rule_type_val < type_val) {
			left = i + 1;
			i += ((right - i) / 2 + (right - i) % 2);
		} else {
			right = i - 1;
			i -= ((i - left) / 2 + (i - left) % 2);
		}
	} while (right >= left);

	return NULL;
}

static apol_domain_trans_rule_t *apol_domain_trans_find_rule_for_dflt(const apol_policy_t * policy, const apol_vector_t * rule_list,
								      const qpol_type_t * dflt)
{
	apol_domain_trans_rule_t *rule = NULL;
	uint32_t dflt_val, rule_type_val;
	unsigned char isattr = 0;

	if (!rule_list)
		return NULL;	       /* empty list, not necessarily an error */

	qpol_type_get_isattr(policy->p, dflt, &isattr);
	if (isattr) {
		ERR(policy, "%s", "Attributes are not valid here.");
		errno = EINVAL;
		return NULL;
	}

	qpol_type_get_value(policy->p, dflt, &dflt_val);
	size_t list_sz = apol_vector_get_size(rule_list), i;
	for (i = 0; i < list_sz; i++) {
		rule = apol_vector_get_element(rule_list, i);
		if (rule->dflt) {
			qpol_type_get_value(policy->p, rule->dflt, &rule_type_val);
			if (rule_type_val == dflt_val) {
				return rule;
			}
		}
	}

	return NULL;
}

static int apol_domain_trans_rule_compare(const void *a, const void *b, void *policy)
{
	const apol_domain_trans_rule_t *rule_a = (const apol_domain_trans_rule_t *)a;
	const apol_domain_trans_rule_t *rule_b = (const apol_domain_trans_rule_t *)b;
	apol_policy_t *p = (apol_policy_t *) policy;
	uint32_t a_val = 0, b_val = 0;

	qpol_type_get_value(p->p, rule_a->type, &a_val);
	qpol_type_get_value(p->p, rule_b->type, &b_val);

	return (int)a_val - (int)b_val;
}

static int apol_domain_trans_add_rule_to_list(const apol_policy_t * policy, apol_vector_t * rule_list, const qpol_type_t * type,
					      const qpol_type_t * dflt, void *rule, bool has_no_trans)
{
	apol_domain_trans_rule_t *tmp_rule = NULL;
	unsigned char isattr = 0;

	if (!rule_list || !type || !rule) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	qpol_type_get_isattr(policy->p, type, &isattr);
	if (isattr) {
		ERR(policy, "%s", "Attributes are not valid here.");
		errno = EINVAL;
		return -1;
	}

	tmp_rule = apol_domain_trans_find_rule_for_type(policy, rule_list, type);
	if (tmp_rule) {
		if (apol_vector_append(tmp_rule->rules, (void *)rule)) {
			ERR(policy, "%s", strerror(errno));
			return -1;
		}
		if (has_no_trans)
			tmp_rule->has_no_trans = true;
	} else {
		tmp_rule = calloc(1, sizeof(apol_domain_trans_rule_t));
		if (!tmp_rule) {
			ERR(policy, "%s", strerror(errno));
			return -1;
		}
		tmp_rule->type = type;
		tmp_rule->dflt = dflt;
		tmp_rule->has_no_trans = (has_no_trans ? true : false);
		if (!(tmp_rule->rules = apol_vector_create(NULL))) {
			ERR(policy, "%s", strerror(errno));
			free(tmp_rule);
			return -1;
		}
		if (apol_vector_append(tmp_rule->rules, (void *)rule) < 0 || apol_vector_append(rule_list, (void *)tmp_rule) < 0) {
			ERR(policy, "%s", strerror(errno));
			apol_vector_destroy(&tmp_rule->rules);
			free(tmp_rule);
			return -1;
		}
	}

	/* vector's arbitrary data is non-const so explicit cast here. */
	apol_vector_sort(rule_list, apol_domain_trans_rule_compare, (void *)policy);

	return 0;
}

static int apol_domain_trans_table_add_rule(apol_policy_t * policy, unsigned char rule_type, void *rule)
{
	int retv, error = 0;
	apol_domain_trans_table_t *table = NULL;
	const qpol_terule_t *terule = NULL;
	const qpol_avrule_t *avrule = NULL;
	const qpol_type_t *src = NULL, *tgt = NULL, *dflt = NULL;
	uint32_t src_val = 0, tgt_val = 0;
	apol_vector_t *src_types = NULL, *tgt_types = NULL;
	size_t i, j;

	if (!policy || !policy->domain_trans_table || !rule_type || !rule) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	table = policy->domain_trans_table;

	if (rule_type & APOL_DOMAIN_TRANS_RULE_TYPE_TRANS) {
		terule = rule;
		qpol_terule_get_source_type(policy->p, terule, &src);
		qpol_terule_get_target_type(policy->p, terule, &tgt);
		qpol_terule_get_default_type(policy->p, terule, &dflt);
	} else {
		avrule = rule;
		qpol_avrule_get_source_type(policy->p, avrule, &src);
		qpol_avrule_get_target_type(policy->p, avrule, &tgt);
	}

	/* expand attributes */
	if ((src_types = apol_query_expand_type(policy, src)) == NULL || (tgt_types = apol_query_expand_type(policy, tgt)) == NULL) {
		error = errno;
		ERR(policy, "%s", strerror(error));
		goto err;
	}

	if (rule_type & APOL_DOMAIN_TRANS_RULE_PROC_TRANS) {
		for (j = 0; j < apol_vector_get_size(src_types); j++) {
			src = apol_vector_get_element(src_types, j);
			qpol_type_get_value(policy->p, src, &src_val);
			for (i = 0; i < apol_vector_get_size(tgt_types); i++) {
				tgt = apol_vector_get_element(tgt_types, i);
				retv = apol_domain_trans_add_rule_to_list(policy, table->dom_list[src_val - 1].proc_trans_rules,
									  tgt, NULL, rule, 0);
				if (retv) {
					error = errno;
					goto err;
				}
			}
		}
	}
	if (rule_type & APOL_DOMAIN_TRANS_RULE_EXEC) {
		for (j = 0; j < apol_vector_get_size(tgt_types); j++) {
			tgt = apol_vector_get_element(tgt_types, j);
			qpol_type_get_value(policy->p, tgt, &tgt_val);
			for (i = 0; i < apol_vector_get_size(src_types); i++) {
				src = apol_vector_get_element(src_types, i);
				retv = apol_domain_trans_add_rule_to_list(policy, table->exec_list[tgt_val - 1].exec_rules,
									  src, NULL, rule,
									  (rule_type & APOL_DOMAIN_TRANS_RULE_EXEC_NO_TRANS));
				if (retv) {
					error = errno;
					goto err;
				}
			}
		}
	}
	if (rule_type & APOL_DOMAIN_TRANS_RULE_ENTRYPOINT) {
		for (i = 0; i < apol_vector_get_size(tgt_types); i++) {
			tgt = apol_vector_get_element(tgt_types, i);
			qpol_type_get_value(policy->p, tgt, &tgt_val);
			for (j = 0; j < apol_vector_get_size(src_types); j++) {
				src = apol_vector_get_element(src_types, j);
				qpol_type_get_value(policy->p, src, &src_val);
				retv = apol_domain_trans_add_rule_to_list(policy, table->dom_list[src_val - 1].ep_rules,
									  tgt, NULL, rule, 0);
				if (retv) {
					error = errno;
					goto err;
				}
				retv = apol_domain_trans_add_rule_to_list(policy, table->exec_list[tgt_val - 1].ep_rules,
									  src, NULL, rule, 0);
				if (retv) {
					error = errno;
					goto err;
				}
			}
		}
	}
	if (rule_type & APOL_DOMAIN_TRANS_RULE_TYPE_TRANS) {
		for (i = 0; i < apol_vector_get_size(tgt_types); i++) {
			tgt = apol_vector_get_element(tgt_types, i);
			for (j = 0; j < apol_vector_get_size(src_types); j++) {
				src = apol_vector_get_element(src_types, j);
				qpol_type_get_value(policy->p, src, &src_val);
				retv = apol_domain_trans_add_rule_to_list(policy, table->dom_list[src_val - 1].type_trans_rules,
									  tgt, dflt, rule, 0);
				if (retv) {
					error = errno;
					goto err;
				}
			}
		}
	}
	if (rule_type & APOL_DOMAIN_TRANS_RULE_SETEXEC) {
		for (i = 0; i < apol_vector_get_size(tgt_types); i++) {
			tgt = apol_vector_get_element(tgt_types, i);
			qpol_type_get_value(policy->p, tgt, &tgt_val);
			for (j = 0; j < apol_vector_get_size(src_types); j++) {
				src = apol_vector_get_element(src_types, j);
				qpol_type_get_value(policy->p, src, &src_val);
				if (src_val != tgt_val)
					continue;	/* only care about allow start self : processes setexec; */
				retv = apol_domain_trans_add_rule_to_list(policy, table->dom_list[src_val - 1].setexec_rules,
									  tgt, NULL, rule, 0);
				if (retv) {
					error = errno;
					goto err;
				}
			}
		}
	}

	apol_vector_destroy(&src_types);
	apol_vector_destroy(&tgt_types);
	return 0;

      err:
	apol_vector_destroy(&src_types);
	apol_vector_destroy(&tgt_types);
	errno = error;
	return -1;
}

static int apol_domain_trans_table_get_all_forward_trans(apol_policy_t * policy, apol_domain_trans_t ** trans,
							 const qpol_type_t * start)
{
	apol_domain_trans_table_t *table = NULL;
	apol_domain_trans_t *entry = NULL, *cur_head = NULL, *cur_tail = NULL;
	const qpol_type_t *ep = NULL, *end = NULL;
	uint32_t start_val, ep_val, end_val;
	size_t i, j;
	apol_domain_trans_rule_t *rule_entry = NULL, *tmp_rule = NULL, *tmp_rule2 = NULL;
	int error = 0, is_modular;
	unsigned char isattr = 0;
	unsigned int policy_version = 0;

	if (!policy || !policy->domain_trans_table || !trans || !start) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	table = policy->domain_trans_table;
	qpol_policy_get_policy_version(policy->p, &policy_version);
	is_modular = qpol_policy_has_capability(policy->p, QPOL_CAP_MODULES);
	qpol_type_get_isattr(policy->p, start, &isattr);
	if (isattr) {
		ERR(policy, "%s", "Attributes are not valid here.");
		errno = EINVAL;
		return -1;
	}

	qpol_type_get_value(policy->p, start, &start_val);

	/* verify type transition rules */
	for (i = 0; i < apol_vector_get_size(table->dom_list[start_val - 1].type_trans_rules); i++) {
		rule_entry = apol_vector_get_element(table->dom_list[start_val - 1].type_trans_rules, i);
		rule_entry->used = true;
		if (!(entry = apol_domain_trans_new())) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			goto exit_error;
		}
		entry->start_type = start;
		entry->ep_type = rule_entry->type;
		entry->end_type = rule_entry->dflt;
		if (!(entry->type_trans_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL))) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			goto exit_error;
		}
		qpol_type_get_value(policy->p, entry->ep_type, &ep_val);
		qpol_type_get_value(policy->p, entry->end_type, &end_val);
		tmp_rule = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].proc_trans_rules,
								entry->end_type);
		if (tmp_rule) {
			tmp_rule->used = true;
			if (!(entry->proc_trans_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
		}
		tmp_rule = apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].exec_rules, entry->start_type);
		if (tmp_rule) {
			tmp_rule->used = true;
			if (!(entry->exec_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
		}
		tmp_rule = apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].ep_rules, entry->end_type);
		if (tmp_rule) {
			tmp_rule->used = true;
			if (!(entry->ep_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
		}
		/* find a setexec rule if there is one */
		tmp_rule = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].setexec_rules, start);
		if (tmp_rule) {
			if (!(entry->setexec_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
		}
		if (apol_vector_get_size(entry->exec_rules) &&
		    apol_vector_get_size(entry->ep_rules) &&
		    apol_vector_get_size(entry->proc_trans_rules) &&
		    (policy_version >= 15 || is_modular ?
		     (apol_vector_get_size(entry->type_trans_rules) || apol_vector_get_size(entry->setexec_rules)) : 1))
			entry->valid = true;
		entry->next = cur_head;
		cur_head = entry;
		if (!cur_tail)
			cur_tail = entry;
		entry = NULL;
	}

	/* follow process transition rules */
	for (i = 0; i < apol_vector_get_size(table->dom_list[start_val - 1].proc_trans_rules); i++) {
		rule_entry = apol_vector_get_element(table->dom_list[start_val - 1].proc_trans_rules, i);
		if (rule_entry->used)
			continue;      /* we already found this transition */
		end = rule_entry->type;
		qpol_type_get_value(policy->p, end, &end_val);
		if (end_val == start_val)
			continue;      /* if start is same as end no transition occurs */
		rule_entry->used = true;
		/* follow each entrypoint of end */
		for (j = 0; j < apol_vector_get_size(table->dom_list[end_val - 1].ep_rules); j++) {
			tmp_rule = apol_vector_get_element(table->dom_list[end_val - 1].ep_rules, j);
			tmp_rule->used = true;
			ep = tmp_rule->type;
			qpol_type_get_value(policy->p, ep, &ep_val);
			tmp_rule2 = apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].ep_rules, end);
			if (tmp_rule2->used)
				continue;	/* we already found this transition */
			tmp_rule2->used = true;
			if (!(entry = apol_domain_trans_new())) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->start_type = start;
			entry->ep_type = ep;
			entry->end_type = end;
			if (!(entry->proc_trans_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			if (!(entry->ep_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			/* find an execute rule if there is one */
			tmp_rule2 = apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].exec_rules, start);
			if (tmp_rule2) {
				if (!(entry->exec_rules = apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
			}
			/* find a setexec rule if there is one */
			tmp_rule2 =
				apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].setexec_rules, start);
			if (tmp_rule2) {
				if (!(entry->setexec_rules = apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
			}
			if (apol_vector_get_size(entry->exec_rules) &&
			    apol_vector_get_size(entry->ep_rules) &&
			    apol_vector_get_size(entry->proc_trans_rules) &&
			    (policy_version >= 15 || is_modular ?
			     (apol_vector_get_size(entry->type_trans_rules) || apol_vector_get_size(entry->setexec_rules)) : 1))
				entry->valid = true;
			entry->next = cur_head;
			cur_head = entry;
			if (!cur_tail)
				cur_tail = entry;
			entry = NULL;
		}
		/* if no entrypoint add an entry for the existing rule */
		if (!apol_vector_get_size(table->dom_list[end_val - 1].ep_rules)) {
			if (!(entry = apol_domain_trans_new())) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->start_type = start;
			entry->end_type = end;
			if (!(entry->proc_trans_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->next = cur_head;
			cur_head = entry;
			if (!cur_tail)
				cur_tail = entry;
			entry = NULL;
		}
	}

	/* add results to list if found */
	if (cur_head) {
		cur_tail->next = *trans;
		*trans = cur_head;
	}

	return 0;

      exit_error:
	apol_domain_trans_destroy(&entry);
	apol_domain_trans_destroy(&cur_head);
	errno = error;
	return -1;
}

static int apol_domain_trans_table_get_all_reverse_trans(apol_policy_t * policy, apol_domain_trans_t ** trans,
							 const qpol_type_t * end)
{
	apol_domain_trans_table_t *table = NULL;
	apol_domain_trans_t *entry = NULL, *cur_head = NULL, *cur_tail = NULL;
	const qpol_type_t *ep = NULL, *start = NULL, *dflt = NULL;
	uint32_t start_val, ep_val, end_val, dflt_val;
	size_t i, j;
	apol_domain_trans_rule_t *rule_entry = NULL, *tmp_rule = NULL, *tmp_rule2 = NULL;
	int error = 0, dead = 0, is_modular;
	unsigned char isattr = 0;
	qpol_iterator_t *iter = NULL;
	apol_vector_t *v = NULL;
	unsigned int policy_version = 0;

	if (!policy || !policy->domain_trans_table || !trans || !end) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	table = policy->domain_trans_table;
	qpol_policy_get_policy_version(policy->p, &policy_version);
	is_modular = qpol_policy_has_capability(policy->p, QPOL_CAP_MODULES);
	qpol_type_get_isattr(policy->p, end, &isattr);
	if (isattr) {
		ERR(policy, "%s", "Attributes are not valid here.");
		errno = EINVAL;
		return -1;
	}

	qpol_type_get_value(policy->p, end, &end_val);

	/* follow entrypoints */
	for (i = 0; i < apol_vector_get_size(table->dom_list[end_val - 1].ep_rules); i++) {
		rule_entry = apol_vector_get_element(table->dom_list[end_val - 1].ep_rules, i);
		ep = rule_entry->type;
		qpol_type_get_value(policy->p, ep, &ep_val);
		rule_entry->used = true;
		/* follow each execute rule of ep */
		for (j = 0; j < apol_vector_get_size(table->exec_list[ep_val - 1].exec_rules); j++) {
			tmp_rule = apol_vector_get_element(table->exec_list[ep_val - 1].exec_rules, j);
			start = tmp_rule->type;
			qpol_type_get_value(policy->p, start, &start_val);
			if (start_val == end_val) {
				if (apol_vector_get_size(table->exec_list[ep_val - 1].exec_rules) == 1)
					dead = 1;	/* if there is only on execute rule for this entrypoint and its source the same as end the entrypoint is dead */
				continue;	/* if start is same as end no transition occurs */
			}
			if (tmp_rule->used)
				continue;	/* we already found this transition */
			tmp_rule->used = true;
			if (!(entry = apol_domain_trans_new())) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->end_type = end;
			entry->ep_type = ep;
			entry->start_type = tmp_rule->type;
			if (!(entry->ep_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			if (!(entry->exec_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			/* find a process transition rule if there is one */
			tmp_rule2 =
				apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].proc_trans_rules, end);
			if (tmp_rule2) {
				if (!(entry->proc_trans_rules = apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
				tmp_rule2->used = true;
			}
			/* find a type transition rule if there is one */
			tmp_rule2 =
				apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].type_trans_rules, ep);
			if (tmp_rule2) {
				dflt = tmp_rule2->dflt;
				qpol_type_get_value(policy->p, dflt, &dflt_val);
				if (dflt_val == end_val) {
					tmp_rule2->used = true;
					if (!
					    (entry->type_trans_rules =
					     apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
						error = errno;
						ERR(policy, "%s", strerror(error));
						goto exit_error;
					}
				}
			}
			/* find a setexec rule if there is one */
			tmp_rule2 =
				apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].setexec_rules, start);
			if (tmp_rule2) {
				if (!(entry->setexec_rules = apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
			}
			if (apol_vector_get_size(entry->exec_rules) &&
			    apol_vector_get_size(entry->ep_rules) &&
			    apol_vector_get_size(entry->proc_trans_rules) &&
			    (policy_version >= 15 || is_modular ?
			     (apol_vector_get_size(entry->type_trans_rules) || apol_vector_get_size(entry->setexec_rules)) : 1))
				entry->valid = true;
			entry->next = cur_head;
			cur_head = entry;
			if (!cur_tail)
				cur_tail = entry;
			entry = NULL;
		}
		/* if no execute rule add an entry for the existing rule */
		if (!apol_vector_get_size(table->exec_list[ep_val - 1].exec_rules) || dead) {
			if (!(entry = apol_domain_trans_new())) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->end_type = end;
			entry->ep_type = ep;
			if (!(entry->ep_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->next = cur_head;
			cur_head = entry;
			if (!cur_tail)
				cur_tail = entry;
			entry = NULL;
		}
	}

	/* find unused process transitions and type_transition rules to end */
	for (i = 0; i < table->size; i++) {
		rule_entry = NULL;
		tmp_rule = NULL;
		start = NULL;
		ep = NULL;
		ep_val = 0;
		if (i == end_val)
			continue;      /* no transition would occur */
		rule_entry = apol_domain_trans_find_rule_for_type(policy, table->dom_list[i].proc_trans_rules, end);
		if (rule_entry && rule_entry->used) {
			rule_entry = NULL;
		}
		tmp_rule = apol_domain_trans_find_rule_for_dflt(policy, table->dom_list[i].type_trans_rules, end);
		if (tmp_rule && tmp_rule->used) {
			tmp_rule = NULL;
		}
		if (!rule_entry && !tmp_rule)
			continue;      /* either used or none exists */
		if (tmp_rule) {
			tmp_rule->used = true;
			qpol_terule_get_source_type(policy->p, apol_vector_get_element(tmp_rule->rules, 0), &start);
			ep = tmp_rule->type;
			qpol_type_get_value(policy->p, ep, &ep_val);
		}
		if (rule_entry) {
			rule_entry->used = true;
			qpol_avrule_get_source_type(policy->p, apol_vector_get_element(rule_entry->rules, 0), &start);
		}
		qpol_type_get_isattr(policy->p, start, &isattr);
		if (isattr) {
			if (qpol_type_get_type_iter(policy->p, start, &iter)) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			if (!(v = apol_vector_create_from_iter(iter, NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			qpol_iterator_destroy(&iter);
		} else {
			if (!(v = apol_vector_create(NULL))) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			if (apol_vector_append(v, (void *)start)) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
		}
		for (j = 0; j < apol_vector_get_size(v); j++) {
			if (!(entry = apol_domain_trans_new())) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto exit_error;
			}
			entry->start_type = apol_vector_get_element(v, j);
			qpol_type_get_value(policy->p, entry->start_type, &start_val);
			entry->ep_type = ep;
			entry->end_type = end;
			if (rule_entry) {
				if (!
				    (entry->proc_trans_rules = apol_vector_create_from_vector(rule_entry->rules, NULL, NULL, NULL)))
				{
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
			}
			if (tmp_rule) {
				if (!(entry->type_trans_rules = apol_vector_create_from_vector(tmp_rule->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
				/* attempt to find an execute rule */
				tmp_rule2 =
					apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].exec_rules,
									     start);
				if (tmp_rule2) {
					tmp_rule2->used = true;
					if (!
					    (entry->exec_rules =
					     apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
						error = errno;
						ERR(policy, "%s", strerror(error));
						goto exit_error;
					}
				}
			}
			/* find a setexec rule if there is one */
			tmp_rule2 = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].setexec_rules,
									 entry->start_type);
			if (tmp_rule2) {
				if (!(entry->setexec_rules = apol_vector_create_from_vector(tmp_rule2->rules, NULL, NULL, NULL))) {
					error = errno;
					ERR(policy, "%s", strerror(error));
					goto exit_error;
				}
			}
			entry->next = cur_head;
			cur_head = entry;
			if (!cur_tail)
				cur_tail = entry;
			entry = NULL;
		}
		apol_vector_destroy(&v);
	}

	/* add results to list if found */
	if (cur_head) {
		cur_tail->next = *trans;
		*trans = cur_head;
	}

	return 0;

      exit_error:
	apol_vector_destroy(&v);
	apol_domain_trans_destroy(&entry);
	apol_domain_trans_destroy(&cur_head);
	errno = error;
	return -1;
}

/* removes all nodes in the linked list pointed to by trans
 * which do not have the same validity as the valid argument */
static int apol_domain_trans_filter_valid(apol_domain_trans_t ** trans, bool valid)
{
	apol_domain_trans_t *cur = NULL, *prev = NULL;

	if (!trans) {
		errno = EINVAL;
		return -1;
	}

	if (valid)
		valid = 1;

	for (cur = *trans; cur;) {
		if (cur->valid == valid) {
			prev = cur;
			cur = cur->next;
			continue;
		}
		if (prev) {
			prev->next = cur->next;
		} else {
			*trans = cur->next;
		}
		cur->next = NULL;
		apol_domain_trans_destroy(&cur);
		if (prev) {
			cur = prev->next;
		} else {
			cur = *trans;
		}
	}

	return 0;
}

/* filter list of transitions to include only transitions
 * with a result type in the provided list */
static int apol_domain_trans_filter_result_types(const apol_policy_t * policy,
						 apol_domain_trans_analysis_t * dta, apol_domain_trans_t ** trans)
{
	apol_domain_trans_t *cur = NULL, *prev = NULL;
	const qpol_type_t *type;
	int compval;

	for (cur = *trans; cur;) {
		if (dta->direction == APOL_DOMAIN_TRANS_DIRECTION_REVERSE) {
			type = cur->start_type;
		} else {
			type = cur->end_type;
		}
		compval = apol_compare_type(policy, type, dta->result, APOL_QUERY_REGEX, &dta->result_regex);
		if (compval < 0) {
			return -1;
		}
		if (compval > 0) {
			prev = cur;
			cur = cur->next;
			continue;
		}
		if (prev) {
			prev->next = cur->next;
		} else {
			*trans = cur->next;
		}
		cur->next = NULL;
		apol_domain_trans_destroy(&cur);
		if (prev) {
			cur = prev->next;
		} else {
			cur = *trans;
		}
	}

	return 0;
}

/* filter list of transitions to include only transitions
 * with an end type that has access to at least one of the provided
 * access_types for at least one of the object & permission sets */
static int apol_domain_trans_filter_access(apol_domain_trans_t ** trans, const apol_vector_t * access_types,
					   const apol_vector_t * obj_perm_sets, const apol_policy_t * policy)
{
	apol_domain_trans_t *cur = NULL, *prev = NULL;
	size_t i, j, k;
	int error = 0;
	const qpol_type_t *type = NULL;
	const char *tmp = NULL;
	apol_avrule_query_t *avq = NULL;
	apol_obj_perm_t *op = NULL;
	apol_vector_t *v = NULL;

	if (!trans || !access_types || !obj_perm_sets || !policy) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (!(*trans))
		return 0;	       /* list already empty */

	avq = apol_avrule_query_create();
	if (!avq) {
		error = errno;
		ERR(policy, "%s", strerror(error));
		goto exit_error;
	}
	apol_avrule_query_set_rules(policy, avq, QPOL_RULE_ALLOW);

	for (cur = *trans; cur;) {
		qpol_type_get_name(policy->p, cur->end_type, &tmp);
		apol_avrule_query_set_source(policy, avq, tmp, 1);
		for (i = 0; i < apol_vector_get_size(access_types); i++) {
			type = apol_vector_get_element(access_types, i);
			qpol_type_get_name(policy->p, type, &tmp);
			apol_avrule_query_set_target(policy, avq, tmp, 1);
			for (j = 0; j < apol_vector_get_size(obj_perm_sets); j++) {
				apol_avrule_query_append_class(policy, avq, NULL);
				op = apol_vector_get_element(obj_perm_sets, j);
				apol_avrule_query_append_class(policy, avq, apol_obj_perm_get_obj_name(op));
				apol_avrule_query_append_perm(policy, avq, NULL);
				for (k = 0; k < apol_vector_get_size(apol_obj_perm_get_perm_vector(op)); k++) {
					tmp = apol_vector_get_element(apol_obj_perm_get_perm_vector(op), k);
					apol_avrule_query_append_perm(policy, avq, tmp);
				}
				apol_avrule_get_by_query(policy, avq, &v);
				apol_vector_cat(cur->access_rules, v);
				apol_vector_destroy(&v);
			}
		}
		if (apol_vector_get_size(cur->access_rules)) {
			prev = cur;
			cur = cur->next;
			continue;
		}
		if (prev) {
			prev->next = cur->next;
		} else {
			*trans = cur->next;
		}
		cur->next = NULL;
		apol_domain_trans_destroy(&cur);
		if (prev) {
			cur = prev->next;
		} else {
			cur = *trans;
		}
	}

	return 0;

      exit_error:
	apol_avrule_query_destroy(&avq);
	errno = error;
	return -1;
}

/* public functions */
int apol_policy_build_domain_trans_table(apol_policy_t * policy)
{
	size_t i;
	unsigned char rule_type = 0x00;
	apol_avrule_query_t *avq = NULL;
	apol_terule_query_t *teq = NULL;
	qpol_avrule_t *avrule = NULL;
	qpol_terule_t *terule = NULL;
	qpol_iterator_t *iter = NULL;
	char *tmp = NULL;
	apol_vector_t *v = NULL;
	int error = 0, is_modular;
	unsigned int policy_version = 0;

	if (!policy) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (policy->domain_trans_table) {
		return 0;	       /* already built */
	}

	policy->domain_trans_table = apol_domain_trans_table_new(policy);
	if (!policy->domain_trans_table) {
		error = errno;
		goto err;
	}

	qpol_policy_get_policy_version(policy->p, &policy_version);
	is_modular = qpol_policy_has_capability(policy->p, QPOL_CAP_MODULES);
	avq = apol_avrule_query_create();
	apol_avrule_query_set_rules(policy, avq, QPOL_RULE_ALLOW);
	apol_avrule_query_append_class(policy, avq, "process");
	apol_avrule_query_append_perm(policy, avq, "transition");
	apol_avrule_get_by_query(policy, avq, &v);
	for (i = 0; i < apol_vector_get_size(v); i++) {
		avrule = apol_vector_get_element(v, i);
		if (apol_domain_trans_table_add_rule(policy, APOL_DOMAIN_TRANS_RULE_PROC_TRANS, (void *)avrule)) {
			error = errno;
			goto err;
		}
	}
	apol_vector_destroy(&v);
	if (policy_version >= 15 || is_modular) {
		apol_avrule_query_append_perm(policy, avq, NULL);
		apol_avrule_query_append_perm(policy, avq, "setexec");
		apol_avrule_get_by_query(policy, avq, &v);
		for (i = 0; i < apol_vector_get_size(v); i++) {
			avrule = apol_vector_get_element(v, i);
			if (apol_domain_trans_table_add_rule(policy, APOL_DOMAIN_TRANS_RULE_SETEXEC, (void *)avrule)) {
				error = errno;
				goto err;
			}
		}
		apol_vector_destroy(&v);
	}
	apol_avrule_query_append_class(policy, avq, NULL);
	apol_avrule_query_append_perm(policy, avq, NULL);

	apol_avrule_query_append_class(policy, avq, "file");
	apol_avrule_get_by_query(policy, avq, &v);
	for (i = 0; i < apol_vector_get_size(v); i++) {
		avrule = apol_vector_get_element(v, i);
		if (qpol_avrule_get_perm_iter(policy->p, avrule, &iter)) {
			error = errno;
			goto err;
		}
		rule_type = 0x00;
		for (; !qpol_iterator_end(iter); qpol_iterator_next(iter)) {
			qpol_iterator_get_item(iter, (void **)&tmp);
			if (!strcmp(tmp, "execute")) {
				rule_type |= APOL_DOMAIN_TRANS_RULE_EXEC;
			} else if (!strcmp(tmp, "entrypoint")) {
				rule_type |= APOL_DOMAIN_TRANS_RULE_ENTRYPOINT;
			} else if (!strcmp(tmp, "execute_no_trans")) {
				rule_type |= APOL_DOMAIN_TRANS_RULE_EXEC_NO_TRANS;
			}
			free(tmp);
			tmp = NULL;
		}
		qpol_iterator_destroy(&iter);
		if (rule_type) {
			if (apol_domain_trans_table_add_rule(policy, rule_type, (void *)avrule)) {
				error = errno;
				goto err;
			}
		}
	}
	apol_vector_destroy(&v);
	apol_avrule_query_destroy(&avq);

	teq = apol_terule_query_create();
	apol_terule_query_set_rules(policy, teq, QPOL_RULE_TYPE_TRANS);
	apol_terule_query_append_class(policy, teq, "process");
	apol_terule_get_by_query(policy, teq, &v);
	for (i = 0; i < apol_vector_get_size(v); i++) {
		terule = apol_vector_get_element(v, i);
		if (apol_domain_trans_table_add_rule(policy, APOL_DOMAIN_TRANS_RULE_TYPE_TRANS, (void *)terule)) {
			error = errno;
			goto err;
		}
	}
	apol_vector_destroy(&v);
	apol_terule_query_destroy(&teq);

	return 0;

      err:
	apol_vector_destroy(&v);
	apol_terule_query_destroy(&teq);
	apol_avrule_query_destroy(&avq);
	qpol_iterator_destroy(&iter);
	domain_trans_table_destroy(&policy->domain_trans_table);
	errno = error;
	return -1;
}

int apol_policy_domain_trans_table_build(apol_policy_t * policy)
{
	return apol_policy_build_domain_trans_table(policy);
}

void domain_trans_table_destroy(apol_domain_trans_table_t ** table)
{
	size_t i;

	if (!table || !(*table))
		return;

	for (i = 0; i < (*table)->size; i++) {
		apol_domain_trans_dom_node_free(&((*table)->dom_list[i]));
		apol_domain_trans_exec_node_free(&((*table)->exec_list[i]));
	}

	free((*table)->dom_list);
	free((*table)->exec_list);
	free(*table);
	*table = NULL;
}

void apol_policy_reset_domain_trans_table(apol_policy_t * policy)
{
	size_t i, j;
	apol_domain_trans_rule_t *rule = NULL;
	apol_domain_trans_table_t *table = NULL;

	if (!policy) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return;
	}

	table = policy->domain_trans_table;
	if (!table) {
		return;
	}

	for (i = 0; i < table->size; i++) {
		for (j = 0; j < apol_vector_get_size(table->dom_list[i].proc_trans_rules); j++) {
			rule = apol_vector_get_element(table->dom_list[i].proc_trans_rules, j);
			rule->used = false;
		}
		for (j = 0; j < apol_vector_get_size(table->dom_list[i].type_trans_rules); j++) {
			rule = apol_vector_get_element(table->dom_list[i].type_trans_rules, j);
			rule->used = false;
		}
		for (j = 0; j < apol_vector_get_size(table->dom_list[i].ep_rules); j++) {
			rule = apol_vector_get_element(table->dom_list[i].ep_rules, j);
			rule->used = false;
		}
		/* setexec rules do not use the used flag */
		for (j = 0; j < apol_vector_get_size(table->exec_list[i].ep_rules); j++) {
			rule = apol_vector_get_element(table->exec_list[i].ep_rules, j);
			rule->used = false;
		}
		for (j = 0; j < apol_vector_get_size(table->exec_list[i].exec_rules); j++) {
			rule = apol_vector_get_element(table->exec_list[i].exec_rules, j);
			rule->used = false;
		}
	}
}

void apol_domain_trans_table_reset(apol_policy_t * policy)
{
	apol_policy_reset_domain_trans_table(policy);
}

apol_domain_trans_analysis_t *apol_domain_trans_analysis_create(void)
{
	apol_domain_trans_analysis_t *new_dta = NULL;
	int error = 0;

	if (!(new_dta = calloc(1, sizeof(apol_domain_trans_analysis_t)))) {
		error = errno;
		goto err;
	}

	new_dta->valid = APOL_DOMAIN_TRANS_SEARCH_VALID;	/* by default search only valid transitions */

	return new_dta;

      err:
	apol_domain_trans_analysis_destroy(&new_dta);
	errno = error;
	return NULL;
}

void apol_domain_trans_analysis_destroy(apol_domain_trans_analysis_t ** dta)
{
	if (!dta || !(*dta))
		return;

	free((*dta)->start_type);
	free((*dta)->result);
	apol_vector_destroy(&((*dta)->access_types));
	apol_vector_destroy(&((*dta)->access_class_perms));
	apol_regex_destroy(&((*dta)->result_regex));
	free(*dta);
	*dta = NULL;
}

int apol_domain_trans_analysis_set_direction(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta,
					     unsigned char direction)
{
	if (!dta || (direction != APOL_DOMAIN_TRANS_DIRECTION_FORWARD && direction != APOL_DOMAIN_TRANS_DIRECTION_REVERSE)) {
		ERR(policy, "Error setting analysis direction: %s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	dta->direction = direction;

	return 0;
}

int apol_domain_trans_analysis_set_valid(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta, unsigned char valid)
{
	if (!dta || valid & ~(APOL_DOMAIN_TRANS_SEARCH_BOTH)) {
		ERR(policy, "Error setting analysis validity flag: %s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	dta->valid = valid;

	return 0;
}

int apol_domain_trans_analysis_set_start_type(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta,
					      const char *type_name)
{
	char *tmp = NULL;
	int error = 0;

	if (!dta || !type_name) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (!(tmp = strdup(type_name))) {
		error = errno;
		ERR(policy, "%s", strerror(error));
		errno = error;
		return -1;
	}

	free(dta->start_type);
	dta->start_type = tmp;

	return 0;
}

int apol_domain_trans_analysis_set_result_regex(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta, const char *regex)
{
	if (!dta) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (!regex) {
		apol_regex_destroy(&dta->result_regex);
		return 0;
	}

	return apol_query_set(policy, &dta->result, &dta->result_regex, regex);
}

int apol_domain_trans_analysis_append_access_type(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta,
						  const char *type_name)
{
	char *tmp = NULL;
	int error = 0;

	if (!dta) {
		ERR(policy, "Error appending type to analysis: %s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (!dta->access_types) {
		if (!(dta->access_types = apol_vector_create(free))) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			errno = error;
			return -1;
		}
	}

	if (!type_name) {
		apol_vector_destroy(&dta->access_types);
		return 0;
	}

	if (!(tmp = strdup(type_name))) {
		error = errno;
		ERR(policy, "%s", strerror(error));
		errno = error;
		return -1;
	}

	if (apol_vector_append(dta->access_types, tmp)) {
		error = errno;
		free(tmp);
		ERR(policy, "%s", strerror(error));
		errno = error;
		return -1;
	}

	return 0;
}

static int compare_class_perm_by_class_name(const void *in_op, const void *class_name, void *unused __attribute__ ((unused)))
{
	const apol_obj_perm_t *op = (const apol_obj_perm_t *)in_op;
	const char *name = (const char *)class_name;

	return strcmp(apol_obj_perm_get_obj_name(op), name);
}

int apol_domain_trans_analysis_append_class_perm(const apol_policy_t * policy, apol_domain_trans_analysis_t * dta,
						 const char *class_name, const char *perm_name)
{
	int error = 0;
	apol_obj_perm_t *op = NULL;
	size_t i;

	if (!dta) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	if (!class_name) {
		apol_vector_destroy(&dta->access_class_perms);
		return 0;
	}

	if (!(dta->access_class_perms)) {
		if ((dta->access_class_perms = apol_vector_create(apol_obj_perm_free)) == NULL) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			errno = error;
			return -1;
		}
	}

	if (apol_vector_get_index(dta->access_class_perms, (void *)class_name, compare_class_perm_by_class_name, NULL, &i) < 0) {
		if (perm_name) {
			if ((op = apol_obj_perm_create()) == NULL) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				errno = error;
				return -1;
			}
			if (apol_obj_perm_set_obj_name(op, class_name) ||
			    apol_obj_perm_append_perm(op, perm_name) || apol_vector_append(dta->access_class_perms, op)) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				apol_obj_perm_free(op);
				errno = error;
				return -1;
			}
		} else {
			return 0;      /* noting to clear; done */
		}
	} else {
		op = apol_vector_get_element(dta->access_class_perms, i);
		if (apol_obj_perm_append_perm(op, perm_name)) {
			error = errno;
			ERR(policy, "Error adding class and permission to analysis: %s", strerror(error));
			errno = error;
			return -1;
		}
	}

	return 0;
}

int apol_domain_trans_analysis_do(apol_policy_t * policy, apol_domain_trans_analysis_t * dta, apol_vector_t ** results)
{
	int error = 0;
	apol_domain_trans_t *trans_list = NULL, *cur = NULL, *next = NULL;
	apol_domain_trans_result_t *tmp_result = NULL;
	apol_vector_t *type_v = NULL;
	size_t i;
	const qpol_type_t *start_type = NULL, *tmp_type = NULL;

	if (!policy || !dta || !results) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	/* build table if not already present */
	if (!(policy->domain_trans_table)) {
		if (apol_policy_build_domain_trans_table(policy))
			return -1;     /* errors already reported by build function */
	}

	/* validate analysis options */
	if (dta->direction == 0 ||
	    dta->valid & ~(APOL_DOMAIN_TRANS_SEARCH_BOTH) ||
	    (apol_vector_get_size(dta->access_types) && !apol_vector_get_size(dta->access_class_perms)) ||
	    (!apol_vector_get_size(dta->access_types) && apol_vector_get_size(dta->access_class_perms)) ||
	    (apol_vector_get_size(dta->access_types) && apol_vector_get_size(dta->access_class_perms) &&
	     dta->direction == APOL_DOMAIN_TRANS_DIRECTION_REVERSE) || !(dta->start_type)) {
		error = EINVAL;
		ERR(policy, "%s", strerror(EINVAL));
		goto err;
	}

	/* get starting type */
	if (qpol_policy_get_type_by_name(policy->p, dta->start_type, &start_type)) {
		error = errno;
		ERR(policy, "Unable to perform analysis: Invalid starting type %s", dta->start_type);
		goto err;
	}

	/* get all transitions for the requested direction */
	if (dta->direction == APOL_DOMAIN_TRANS_DIRECTION_REVERSE) {
		if (apol_domain_trans_table_get_all_reverse_trans(policy, &trans_list, start_type)) {
			error = errno;
			goto err;
		}
	} else {
		if (apol_domain_trans_table_get_all_forward_trans(policy, &trans_list, start_type)) {
			error = errno;
			goto err;
		}
	}

	/* if filtering by validity do that first */
	if (dta->valid != APOL_DOMAIN_TRANS_SEARCH_BOTH) {
		if (apol_domain_trans_filter_valid(&trans_list, (dta->valid & APOL_DOMAIN_TRANS_SEARCH_VALID))) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			goto err;
		}
	}

	/* next filtering by result type if requested */
	if (dta->result && apol_domain_trans_filter_result_types(policy, dta, &trans_list)) {
		error = errno;
		goto err;
	}

	/* if access filtering is requested do it last */
	if (apol_vector_get_size(dta->access_types)) {
		if ((type_v = apol_vector_create(NULL)) == NULL) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			goto err;
		}
		for (i = 0; i < apol_vector_get_size(dta->access_types); i++) {
			if (qpol_policy_get_type_by_name(policy->p, apol_vector_get_element(dta->access_types, i), &tmp_type)) {
				error = errno;
				goto err;
			}
			if (apol_vector_append_unique(type_v, (void *)tmp_type, NULL, NULL)) {
				error = errno;
				ERR(policy, "%s", strerror(error));
				goto err;
			}
		}
		if (apol_domain_trans_filter_access(&trans_list, type_v, dta->access_class_perms, policy)) {
			error = errno;
			goto err;
		}
		apol_vector_destroy(&type_v);
	}

	/* build result vector */
	if (!(*results = apol_vector_create(domain_trans_result_free))) {
		error = errno;
		ERR(policy, "Error compiling results: %s", strerror(error));
		goto err;
	}
	for (cur = trans_list; cur; cur = next) {
		next = cur->next;
		if (!(tmp_result = calloc(1, sizeof(apol_domain_trans_result_t)))) {
			error = errno;
			ERR(policy, "Error compiling results: %s", strerror(error));
			goto err;
		}
		tmp_result->start_type = cur->start_type;
		tmp_result->ep_type = cur->ep_type;
		tmp_result->end_type = cur->end_type;
		/* since cur will be destroyed no need to copy just assign and set to NULL */
		tmp_result->proc_trans_rules = cur->proc_trans_rules;
		cur->proc_trans_rules = NULL;
		tmp_result->ep_rules = cur->ep_rules;
		cur->ep_rules = NULL;
		tmp_result->exec_rules = cur->exec_rules;
		cur->exec_rules = NULL;
		tmp_result->setexec_rules = cur->setexec_rules;
		cur->setexec_rules = NULL;
		tmp_result->type_trans_rules = cur->type_trans_rules;
		cur->type_trans_rules = NULL;
		tmp_result->valid = cur->valid;
		tmp_result->access_rules = cur->access_rules;
		cur->access_rules = NULL;
		if (apol_vector_append(*results, tmp_result)) {
			error = errno;
			ERR(policy, "%s", strerror(error));
			goto err;
		}
		cur->next = NULL;
		apol_domain_trans_destroy(&cur);
	}

	return 0;

      err:
	apol_domain_trans_destroy(&trans_list);
	apol_vector_destroy(&type_v);
	apol_vector_destroy(results);
	errno = error;
	return -1;
}

const qpol_type_t *apol_domain_trans_result_get_start_type(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->start_type;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const qpol_type_t *apol_domain_trans_result_get_entrypoint_type(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->ep_type;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const qpol_type_t *apol_domain_trans_result_get_end_type(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->end_type;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const apol_vector_t *apol_domain_trans_result_get_proc_trans_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->proc_trans_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const apol_vector_t *apol_domain_trans_result_get_entrypoint_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->ep_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const apol_vector_t *apol_domain_trans_result_get_exec_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->exec_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const apol_vector_t *apol_domain_trans_result_get_setexec_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->setexec_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

const apol_vector_t *apol_domain_trans_result_get_type_trans_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->type_trans_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

int apol_domain_trans_result_is_trans_valid(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->valid;
	} else {
		errno = EINVAL;
		return 0;
	}
}

const apol_vector_t *apol_domain_trans_result_get_access_rules(const apol_domain_trans_result_t * dtr)
{
	if (dtr) {
		return dtr->access_rules;
	} else {
		errno = EINVAL;
		return NULL;
	}
}

int apol_domain_trans_table_verify_trans(apol_policy_t * policy, const qpol_type_t * start_dom, const qpol_type_t * ep_type,
					 const qpol_type_t * end_dom)
{
	apol_domain_trans_table_t *table = NULL;
	apol_domain_trans_rule_t *retv;
	int missing_rules = 0;
	uint32_t start_val = 0, ep_val = 0, end_val = 0, dflt_val = 0;
	unsigned int policy_version = 0;
	int is_modular;
	apol_domain_trans_rule_t *rule = NULL;

	if (!policy) {
		ERR(policy, "%s", strerror(EINVAL));
		errno = EINVAL;
		return -1;
	}

	qpol_policy_get_policy_version(policy->p, &policy_version);
	is_modular = qpol_policy_has_capability(policy->p, QPOL_CAP_MODULES);
	if (!start_dom || !ep_type || !end_dom) {
		missing_rules = APOL_DOMAIN_TRANS_RULE_TYPE_TRANS;
		if (!start_dom) {
			missing_rules |= (APOL_DOMAIN_TRANS_RULE_PROC_TRANS | APOL_DOMAIN_TRANS_RULE_EXEC);
			if (policy_version >= 15 || is_modular)
				missing_rules |= APOL_DOMAIN_TRANS_RULE_SETEXEC;
		}
		if (!ep_type)
			missing_rules |= (APOL_DOMAIN_TRANS_RULE_EXEC | APOL_DOMAIN_TRANS_RULE_ENTRYPOINT);
		if (!end_dom)
			missing_rules |= (APOL_DOMAIN_TRANS_RULE_PROC_TRANS | APOL_DOMAIN_TRANS_RULE_ENTRYPOINT);
		return missing_rules;
	}

	/* build table if not already present */
	if (!(policy->domain_trans_table)) {
		if (apol_policy_build_domain_trans_table(policy))
			return -1;     /* errors already reported by build function */
	}

	table = policy->domain_trans_table;

	qpol_type_get_value(policy->p, start_dom, &start_val);
	qpol_type_get_value(policy->p, ep_type, &ep_val);
	qpol_type_get_value(policy->p, end_dom, &end_val);

	retv = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].proc_trans_rules, end_dom);
	if (!retv)
		missing_rules |= APOL_DOMAIN_TRANS_RULE_PROC_TRANS;
	retv = apol_domain_trans_find_rule_for_type(policy, table->exec_list[ep_val - 1].exec_rules, start_dom);
	if (!retv)
		missing_rules |= APOL_DOMAIN_TRANS_RULE_EXEC;
	retv = apol_domain_trans_find_rule_for_type(policy, table->dom_list[end_val - 1].ep_rules, ep_type);
	if (!retv)
		missing_rules |= APOL_DOMAIN_TRANS_RULE_ENTRYPOINT;

	/* for version 15 and later or any modular policy, there must
	 * be either have a type_transition rule or setexec
	 * permission */
	if (policy_version >= 15 || is_modular) {
		rule = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].type_trans_rules, ep_type);
		if (rule) {
			qpol_type_get_value(policy->p, rule->dflt, &dflt_val);
		}
		if (!rule || dflt_val != end_val) {	/* no type_transition or different default */
			retv = apol_domain_trans_find_rule_for_type(policy, table->dom_list[start_val - 1].setexec_rules,
								    start_dom);
			if (!dflt_val)
				missing_rules |= APOL_DOMAIN_TRANS_RULE_TYPE_TRANS;	/* only missing if none was found, not if different default */
			if (!retv)
				missing_rules |= APOL_DOMAIN_TRANS_RULE_SETEXEC;
			else
				missing_rules = 0;
		}
	}

	return missing_rules;
}

apol_domain_trans_result_t *apol_domain_trans_result_create_from_domain_trans_result(const apol_domain_trans_result_t * result)
{
	apol_domain_trans_result_t *new_r = NULL;
	int retval = -1;
	if ((new_r = calloc(1, sizeof(*new_r))) == NULL) {
		goto cleanup;
	}
	if (result->proc_trans_rules != NULL &&
	    (new_r->proc_trans_rules = apol_vector_create_from_vector(result->proc_trans_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	if (result->ep_rules != NULL
	    && (new_r->ep_rules = apol_vector_create_from_vector(result->ep_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	if (result->exec_rules != NULL
	    && (new_r->exec_rules = apol_vector_create_from_vector(result->exec_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	if (result->setexec_rules != NULL
	    && (new_r->setexec_rules = apol_vector_create_from_vector(result->setexec_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	if (result->type_trans_rules != NULL &&
	    (new_r->type_trans_rules = apol_vector_create_from_vector(result->type_trans_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	if (result->access_rules != NULL
	    && (new_r->access_rules = apol_vector_create_from_vector(result->access_rules, NULL, NULL, NULL)) == NULL) {
		goto cleanup;
	}
	new_r->start_type = result->start_type;
	new_r->ep_type = result->ep_type;
	new_r->end_type = result->end_type;
	new_r->valid = result->valid;
	retval = 0;
      cleanup:
	if (retval != 0) {
		domain_trans_result_free(new_r);
		return NULL;
	}
	return new_r;
}

/******************** protected functions ********************/

void domain_trans_result_free(void *dtr)
{
	apol_domain_trans_result_t *res = (apol_domain_trans_result_t *) dtr;

	if (!res)
		return;

	apol_vector_destroy(&res->proc_trans_rules);
	apol_vector_destroy(&res->ep_rules);
	apol_vector_destroy(&res->exec_rules);
	apol_vector_destroy(&res->setexec_rules);
	apol_vector_destroy(&res->type_trans_rules);
	apol_vector_destroy(&res->access_rules);
	free(res);
}

void apol_domain_trans_result_destroy(apol_domain_trans_result_t ** res)
{
	if (!res || !(*res))
		return;
	domain_trans_result_free((void *)*res);
	*res = NULL;
}
