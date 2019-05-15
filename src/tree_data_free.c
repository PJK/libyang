/**
 * @file tree_data_free.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief Freeing functions for data tree structures
 *
 * Copyright (c) 2019 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "common.h"

#include <assert.h>
#include <stdlib.h>

#include "hash_table.h"
#include "log.h"
#include "tree.h"
#include "tree_data.h"
#include "tree_schema.h"
#include "tree_data_internal.h"

API LY_ERR
lyd_unlink_tree(struct lyd_node *node)
{
    struct lyd_node *iter;
    struct lyd_node **first_sibling;

    LY_CHECK_ARG_RET(NULL, node, LY_EINVAL);

    if (node->parent) {
        first_sibling = lyd_node_children_p((struct lyd_node*)node->parent);
    }

    /* unlink from siblings */
    if (node->prev->next) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        /* unlinking the last node */
        if (node->parent) {
            iter = *first_sibling;
        } else {
            iter = node->prev;
            while (iter->prev != node) {
                iter = iter->prev;
            }
        }
        /* update the "last" pointer from the first node */
        iter->prev = node->prev;
    }

    /* unlink from parent */
    if (node->parent) {
        if (*first_sibling == node) {
            /* the node is the first child */
            *first_sibling = node->next;
        }
        lyd_unlink_hash(node);
        node->parent = NULL;
    }

    node->next = NULL;
    node->prev = node;

    return EXIT_SUCCESS;
}

/**
 * @brief Free the YANG data value content.
 * @param[in] ctx libyang context
 * @param[in] value Data value structure to clean - since it is mostly part of other structure, only content is freed.
 */
static void
lyd_free_value(struct ly_ctx *ctx, struct lyd_value *value)
{
    FREE_STRING(ctx, value->canonized);
}

API void
lyd_free_attr(struct ly_ctx *ctx, struct lyd_attr *attr, int recursive)
{
    struct lyd_attr *iter;

    LY_CHECK_ARG_RET(NULL, ctx, );
    if (!attr) {
        return;
    }

    if (attr->parent) {
        if (attr->parent->attr == attr) {
            if (recursive) {
                attr->parent->attr = NULL;
            } else {
                attr->parent->attr = attr->next;
            }
        } else {
            for (iter = attr->parent->attr; iter->next != attr; iter = iter->next);
            if (iter->next) {
                if (recursive) {
                    iter->next = NULL;
                } else {
                    iter->next = attr->next;
                }
            }
        }
    }

    if (!recursive) {
        attr->next = NULL;
    }

    for(iter = attr; iter; ) {
        attr = iter;
        iter = iter->next;

        FREE_STRING(ctx, attr->name);
        lyd_free_value(ctx, &attr->value);
        free(attr);
    }
}

/**
 * @brief Free Data (sub)tree.
 * @param[in] ctx libyang context.
 * @param[in] node Data node to be freed.
 * @param[in] top Recursion flag to unlink the root of the subtree being freed.
 */
static void
lyd_free_subtree(struct ly_ctx *ctx, struct lyd_node *node, int top)
{
    struct lyd_node *iter, *next;
    struct lyd_node *children;

    assert(node);

    /* remove children hash table in case of inner data node */
    if (node->schema->nodetype & LYD_NODE_INNER) {
        lyht_free(((struct lyd_node_inner*)node)->children_ht);
        ((struct lyd_node_inner*)node)->children_ht = NULL;

        /* free the children */
        children = (struct lyd_node*)lyd_node_children(node);
        LY_LIST_FOR_SAFE(children, next, iter) {
            lyd_free_subtree(ctx, iter, 0);
        }
    } else if (node->schema->nodetype & LYD_NODE_ANY) {
        /* TODO anydata */
#if 0
        switch (((struct lyd_node_anydata *)node)->value_type) {
        case LYD_ANYDATA_CONSTSTRING:
        case LYD_ANYDATA_SXML:
        case LYD_ANYDATA_JSON:
            FREE_STRING(node->schema->module->ctx, ((struct lyd_node_anydata *)node)->value.str);
            break;
        case LYD_ANYDATA_DATATREE:
            lyd_free_withsiblings(((struct lyd_node_anydata *)node)->value.tree);
            break;
        case LYD_ANYDATA_XML:
            lyxml_free_withsiblings(node->schema->module->ctx, ((struct lyd_node_anydata *)node)->value.xml);
            break;
        case LYD_ANYDATA_LYB:
            free(((struct lyd_node_anydata *)node)->value.mem);
            break;
        case LYD_ANYDATA_STRING:
        case LYD_ANYDATA_SXMLD:
        case LYD_ANYDATA_JSOND:
        case LYD_ANYDATA_LYBD:
            /* dynamic strings are used only as input parameters */
            assert(0);
            break;
        }
#endif
    } else if (node->schema->nodetype & LYD_NODE_TERM) {
        lyd_free_value(ctx, &((struct lyd_node_term*)node)->value);
    }

    /* free the node's attributes */
    lyd_free_attr(ctx, node->attr, 1);

    /* unlink only the nodes from the first level, nodes in subtree are freed all, so no unlink is needed */
    if (top) {
        lyd_unlink_tree(node);
    }

    free(node);
}

API void
lyd_free_tree(struct lyd_node *node)
{
    if (!node) {
        return;
    }

    lyd_free_subtree(node->schema->module->ctx, node, 1);
}

API void
lyd_free_all(struct lyd_node *node)
{
    struct lyd_node *iter, *next;

    if (!node) {
        return;
    }

    /* get the first top-level sibling */
    for (; node->parent; node = (struct lyd_node*)node->parent);
    while (node->prev->next) {
        node = node->prev;
    }

    LY_LIST_FOR_SAFE(node, next, iter) {
        /* in case of the top-level nodes (node->parent is NULL), no unlinking needed */
        lyd_free_subtree(node->schema->module->ctx, node, node->parent ? 1 : 0);
    }
}
