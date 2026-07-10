/*
 * hoox nano-glib: GSList, GList, GQueue.
 *
 * Public node layout matches GLib so intrusive traversal in extracted gum
 * code (node->data / node->next / node->prev) keeps working.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_LIST_H__
#define __HOOX_COMPAT_LIST_H__

#include "hxdefs.h"

G_BEGIN_DECLS

typedef struct _GSList GSList;
typedef struct _GList GList;
typedef struct _GQueue GQueue;

struct _GSList
{
  gpointer data;
  GSList * next;
};

struct _GList
{
  gpointer data;
  GList * next;
  GList * prev;
};

struct _GQueue
{
  GList * head;
  GList * tail;
  guint length;
};

#define G_QUEUE_INIT { NULL, NULL, 0 }

#define g_slist_next(slist) ((slist) ? (slist)->next : NULL)
#define g_list_next(list) ((list) ? (list)->next : NULL)
#define g_list_previous(list) ((list) ? (list)->prev : NULL)

/* ---- GSList ------------------------------------------------------------- */

GSList * g_slist_append (GSList * list, gpointer data);
GSList * g_slist_prepend (GSList * list, gpointer data);
GSList * g_slist_insert (GSList * list, gpointer data, gint position);
GSList * g_slist_remove (GSList * list, gconstpointer data);
GSList * g_slist_remove_link (GSList * list, GSList * link_);
GSList * g_slist_delete_link (GSList * list, GSList * link_);
GSList * g_slist_find (GSList * list, gconstpointer data);
GSList * g_slist_last (GSList * list);
GSList * g_slist_nth (GSList * list, guint n);
gpointer g_slist_nth_data (GSList * list, guint n);
guint g_slist_length (GSList * list);
GSList * g_slist_reverse (GSList * list);
void g_slist_foreach (GSList * list, GFunc func, gpointer user_data);
void g_slist_free (GSList * list);
void g_slist_free_1 (GSList * list);
void g_slist_free_full (GSList * list, GDestroyNotify free_func);

/* ---- GList -------------------------------------------------------------- */

GList * g_list_append (GList * list, gpointer data);
GList * g_list_prepend (GList * list, gpointer data);
GList * g_list_insert (GList * list, gpointer data, gint position);
GList * g_list_remove (GList * list, gconstpointer data);
GList * g_list_remove_link (GList * list, GList * llink);
GList * g_list_delete_link (GList * list, GList * link_);
GList * g_list_find (GList * list, gconstpointer data);
GList * g_list_first (GList * list);
GList * g_list_last (GList * list);
GList * g_list_nth (GList * list, guint n);
gpointer g_list_nth_data (GList * list, guint n);
guint g_list_length (GList * list);
GList * g_list_reverse (GList * list);
void g_list_foreach (GList * list, GFunc func, gpointer user_data);
void g_list_free (GList * list);
void g_list_free_full (GList * list, GDestroyNotify free_func);

/* ---- GQueue ------------------------------------------------------------- */

GQueue * g_queue_new (void);
void g_queue_init (GQueue * queue);
void g_queue_clear (GQueue * queue);
void g_queue_free (GQueue * queue);
gboolean g_queue_is_empty (GQueue * queue);
guint g_queue_get_length (GQueue * queue);
void g_queue_push_head (GQueue * queue, gpointer data);
void g_queue_push_tail (GQueue * queue, gpointer data);
gpointer g_queue_pop_head (GQueue * queue);
gpointer g_queue_pop_tail (GQueue * queue);
gpointer g_queue_peek_head (GQueue * queue);
gpointer g_queue_peek_tail (GQueue * queue);
void g_queue_foreach (GQueue * queue, GFunc func, gpointer user_data);
gboolean g_queue_remove (GQueue * queue, gconstpointer data);

G_END_DECLS

#endif
