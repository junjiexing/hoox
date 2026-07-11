/*
 * hoox nano-glib: HxSList, HxList, HxQueue.
 *
 * Public node layout matches GLib so intrusive traversal in extracted hoox
 * code (node->data / node->next / node->prev) keeps working.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_LIST_H__
#define __HOOX_COMPAT_LIST_H__

#include "hxdefs.h"

HX_BEGIN_DECLS

typedef struct _HxSList HxSList;
typedef struct _HxList HxList;
typedef struct _HxQueue HxQueue;

struct _HxSList
{
  hx_pointer data;
  HxSList * next;
};

struct _HxList
{
  hx_pointer data;
  HxList * next;
  HxList * prev;
};

struct _HxQueue
{
  HxList * head;
  HxList * tail;
  hx_uint length;
};

#define HX_QUEUE_INIT { NULL, NULL, 0 }

#define hx_slist_next(slist) ((slist) ? (slist)->next : NULL)
#define hx_list_next(list) ((list) ? (list)->next : NULL)
#define hx_list_previous(list) ((list) ? (list)->prev : NULL)

/* ---- HxSList ------------------------------------------------------------- */

HxSList * hx_slist_append (HxSList * list, hx_pointer data);
HxSList * hx_slist_prepend (HxSList * list, hx_pointer data);
HxSList * hx_slist_remove (HxSList * list, hx_constpointer data);
HxSList * hx_slist_remove_link (HxSList * list, HxSList * link_);
HxSList * hx_slist_delete_link (HxSList * list, HxSList * link_);
HxSList * hx_slist_find (HxSList * list, hx_constpointer data);
HxSList * hx_slist_last (HxSList * list);
HxSList * hx_slist_nth (HxSList * list, hx_uint n);
/* hoox:test-only-begin */
hx_pointer hx_slist_nth_data (HxSList * list, hx_uint n);
/* hoox:test-only-end */
/* hoox:test-only-begin */
hx_uint hx_slist_length (HxSList * list);
/* hoox:test-only-end */
void hx_slist_foreach (HxSList * list, HxFunc func, hx_pointer user_data);
void hx_slist_free (HxSList * list);

/* ---- HxList -------------------------------------------------------------- */

HxList * hx_list_append (HxList * list, hx_pointer data);
HxList * hx_list_prepend (HxList * list, hx_pointer data);
HxList * hx_list_remove_link (HxList * list, HxList * llink);
HxList * hx_list_delete_link (HxList * list, HxList * link_);
HxList * hx_list_find (HxList * list, hx_constpointer data);
HxList * hx_list_last (HxList * list);
HxList * hx_list_nth (HxList * list, hx_uint n);
void hx_list_foreach (HxList * list, HxFunc func, hx_pointer user_data);
void hx_list_free (HxList * list);

/* ---- HxQueue ------------------------------------------------------------- */

HxQueue * hx_queue_new (void);
void hx_queue_init (HxQueue * queue);
void hx_queue_clear (HxQueue * queue);
void hx_queue_free (HxQueue * queue);
hx_boolean hx_queue_is_empty (HxQueue * queue);
/* hoox:test-only-begin */
void hx_queue_push_head (HxQueue * queue, hx_pointer data);
/* hoox:test-only-end */
void hx_queue_push_tail (HxQueue * queue, hx_pointer data);
hx_pointer hx_queue_pop_head (HxQueue * queue);
/* hoox:test-only-begin */
hx_pointer hx_queue_pop_tail (HxQueue * queue);
/* hoox:test-only-end */

HX_END_DECLS

#endif
