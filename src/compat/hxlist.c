/*
 * hoox nano-glib: GSList / GList / GQueue implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxlist.h"
#include "hxmem.h"

/* ---- GSList ------------------------------------------------------------- */

GSList *
g_slist_append (GSList * list,
                gpointer data)
{
  GSList * node = g_slice_new (GSList);
  node->data = data;
  node->next = NULL;

  if (list == NULL)
    return node;

  GSList * last = g_slist_last (list);
  last->next = node;
  return list;
}

GSList *
g_slist_prepend (GSList * list,
                 gpointer data)
{
  GSList * node = g_slice_new (GSList);
  node->data = data;
  node->next = list;
  return node;
}

GSList *
g_slist_insert (GSList * list,
                gpointer data,
                gint position)
{
  GSList * prev;
  GSList * node;

  if (position < 0 || list == NULL)
    return g_slist_append (list, data);
  if (position == 0)
    return g_slist_prepend (list, data);

  prev = list;
  while (--position > 0 && prev->next != NULL)
    prev = prev->next;

  node = g_slice_new (GSList);
  node->data = data;
  node->next = prev->next;
  prev->next = node;

  return list;
}

GSList *
g_slist_remove (GSList * list,
                gconstpointer data)
{
  GSList * prev = NULL;
  GSList * cur = list;

  while (cur != NULL)
  {
    if (cur->data == data)
    {
      if (prev != NULL)
        prev->next = cur->next;
      else
        list = cur->next;
      g_slice_free (GSList, cur);
      break;
    }
    prev = cur;
    cur = cur->next;
  }

  return list;
}

GSList *
g_slist_remove_link (GSList * list,
                     GSList * link_)
{
  GSList * prev = NULL;
  GSList * cur = list;

  while (cur != NULL)
  {
    if (cur == link_)
    {
      if (prev != NULL)
        prev->next = cur->next;
      else
        list = cur->next;
      cur->next = NULL;
      break;
    }
    prev = cur;
    cur = cur->next;
  }

  return list;
}

GSList *
g_slist_delete_link (GSList * list,
                     GSList * link_)
{
  list = g_slist_remove_link (list, link_);
  g_slice_free (GSList, link_);
  return list;
}

GSList *
g_slist_find (GSList * list,
              gconstpointer data)
{
  while (list != NULL)
  {
    if (list->data == data)
      return list;
    list = list->next;
  }
  return NULL;
}

GSList *
g_slist_last (GSList * list)
{
  if (list == NULL)
    return NULL;
  while (list->next != NULL)
    list = list->next;
  return list;
}

GSList *
g_slist_nth (GSList * list,
             guint n)
{
  while (n-- > 0 && list != NULL)
    list = list->next;
  return list;
}

gpointer
g_slist_nth_data (GSList * list,
                  guint n)
{
  GSList * node = g_slist_nth (list, n);
  return (node != NULL) ? node->data : NULL;
}

guint
g_slist_length (GSList * list)
{
  guint n = 0;
  while (list != NULL)
  {
    n++;
    list = list->next;
  }
  return n;
}

GSList *
g_slist_reverse (GSList * list)
{
  GSList * prev = NULL;

  while (list != NULL)
  {
    GSList * next = list->next;
    list->next = prev;
    prev = list;
    list = next;
  }

  return prev;
}

void
g_slist_foreach (GSList * list,
                 GFunc func,
                 gpointer user_data)
{
  while (list != NULL)
  {
    GSList * next = list->next;
    func (list->data, user_data);
    list = next;
  }
}

void
g_slist_free_1 (GSList * list)
{
  g_slice_free (GSList, list);
}

void
g_slist_free (GSList * list)
{
  while (list != NULL)
  {
    GSList * next = list->next;
    g_slice_free (GSList, list);
    list = next;
  }
}

void
g_slist_free_full (GSList * list,
                   GDestroyNotify free_func)
{
  while (list != NULL)
  {
    GSList * next = list->next;
    if (free_func != NULL)
      free_func (list->data);
    g_slice_free (GSList, list);
    list = next;
  }
}

/* ---- GList -------------------------------------------------------------- */

GList *
g_list_last (GList * list)
{
  if (list == NULL)
    return NULL;
  while (list->next != NULL)
    list = list->next;
  return list;
}

GList *
g_list_first (GList * list)
{
  if (list == NULL)
    return NULL;
  while (list->prev != NULL)
    list = list->prev;
  return list;
}

GList *
g_list_append (GList * list,
               gpointer data)
{
  GList * node = g_slice_new (GList);
  GList * last;

  node->data = data;
  node->next = NULL;

  if (list == NULL)
  {
    node->prev = NULL;
    return node;
  }

  last = g_list_last (list);
  last->next = node;
  node->prev = last;

  return list;
}

GList *
g_list_prepend (GList * list,
                gpointer data)
{
  GList * node = g_slice_new (GList);

  node->data = data;
  node->next = list;
  node->prev = NULL;

  if (list != NULL)
    list->prev = node;

  return node;
}

GList *
g_list_insert (GList * list,
               gpointer data,
               gint position)
{
  GList * tmp;
  GList * node;

  if (position < 0 || list == NULL)
    return g_list_append (list, data);
  if (position == 0)
    return g_list_prepend (list, data);

  tmp = g_list_nth (list, (guint) position);
  if (tmp == NULL)
    return g_list_append (list, data);

  node = g_slice_new (GList);
  node->data = data;
  node->prev = tmp->prev;
  node->next = tmp;
  if (tmp->prev != NULL)
    tmp->prev->next = node;
  tmp->prev = node;

  return (node->prev == NULL) ? node : list;
}

GList *
g_list_remove_link (GList * list,
                    GList * llink)
{
  if (llink == NULL)
    return list;

  if (llink->prev != NULL)
    llink->prev->next = llink->next;
  if (llink->next != NULL)
    llink->next->prev = llink->prev;

  if (llink == list)
    list = list->next;

  llink->next = NULL;
  llink->prev = NULL;

  return list;
}

GList *
g_list_delete_link (GList * list,
                    GList * link_)
{
  list = g_list_remove_link (list, link_);
  g_slice_free (GList, link_);
  return list;
}

GList *
g_list_remove (GList * list,
               gconstpointer data)
{
  GList * cur = list;

  while (cur != NULL)
  {
    if (cur->data == data)
    {
      list = g_list_delete_link (list, cur);
      break;
    }
    cur = cur->next;
  }

  return list;
}

GList *
g_list_find (GList * list,
             gconstpointer data)
{
  while (list != NULL)
  {
    if (list->data == data)
      return list;
    list = list->next;
  }
  return NULL;
}

GList *
g_list_nth (GList * list,
            guint n)
{
  while (n-- > 0 && list != NULL)
    list = list->next;
  return list;
}

gpointer
g_list_nth_data (GList * list,
                 guint n)
{
  GList * node = g_list_nth (list, n);
  return (node != NULL) ? node->data : NULL;
}

guint
g_list_length (GList * list)
{
  guint n = 0;
  while (list != NULL)
  {
    n++;
    list = list->next;
  }
  return n;
}

GList *
g_list_reverse (GList * list)
{
  GList * last = NULL;

  while (list != NULL)
  {
    last = list;
    list = last->next;
    last->next = last->prev;
    last->prev = list;
  }

  return last;
}

void
g_list_foreach (GList * list,
                GFunc func,
                gpointer user_data)
{
  while (list != NULL)
  {
    GList * next = list->next;
    func (list->data, user_data);
    list = next;
  }
}

void
g_list_free (GList * list)
{
  while (list != NULL)
  {
    GList * next = list->next;
    g_slice_free (GList, list);
    list = next;
  }
}

void
g_list_free_full (GList * list,
                  GDestroyNotify free_func)
{
  while (list != NULL)
  {
    GList * next = list->next;
    if (free_func != NULL)
      free_func (list->data);
    g_slice_free (GList, list);
    list = next;
  }
}

/* ---- GQueue ------------------------------------------------------------- */

GQueue *
g_queue_new (void)
{
  return g_new0 (GQueue, 1);
}

void
g_queue_init (GQueue * queue)
{
  queue->head = NULL;
  queue->tail = NULL;
  queue->length = 0;
}

void
g_queue_clear (GQueue * queue)
{
  g_list_free (queue->head);
  g_queue_init (queue);
}

void
g_queue_free (GQueue * queue)
{
  if (queue == NULL)
    return;
  g_list_free (queue->head);
  g_free (queue);
}

gboolean
g_queue_is_empty (GQueue * queue)
{
  return queue->head == NULL;
}

guint
g_queue_get_length (GQueue * queue)
{
  return queue->length;
}

void
g_queue_push_head (GQueue * queue,
                   gpointer data)
{
  queue->head = g_list_prepend (queue->head, data);
  if (queue->tail == NULL)
    queue->tail = queue->head;
  queue->length++;
}

void
g_queue_push_tail (GQueue * queue,
                   gpointer data)
{
  GList * node = g_slice_new (GList);

  node->data = data;
  node->next = NULL;
  node->prev = queue->tail;

  if (queue->tail != NULL)
    queue->tail->next = node;
  else
    queue->head = node;

  queue->tail = node;
  queue->length++;
}

gpointer
g_queue_pop_head (GQueue * queue)
{
  GList * node;
  gpointer data;

  if (queue->head == NULL)
    return NULL;

  node = queue->head;
  data = node->data;

  queue->head = node->next;
  if (queue->head != NULL)
    queue->head->prev = NULL;
  else
    queue->tail = NULL;

  g_slice_free (GList, node);
  queue->length--;

  return data;
}

gpointer
g_queue_pop_tail (GQueue * queue)
{
  GList * node;
  gpointer data;

  if (queue->tail == NULL)
    return NULL;

  node = queue->tail;
  data = node->data;

  queue->tail = node->prev;
  if (queue->tail != NULL)
    queue->tail->next = NULL;
  else
    queue->head = NULL;

  g_slice_free (GList, node);
  queue->length--;

  return data;
}

gpointer
g_queue_peek_head (GQueue * queue)
{
  return (queue->head != NULL) ? queue->head->data : NULL;
}

gpointer
g_queue_peek_tail (GQueue * queue)
{
  return (queue->tail != NULL) ? queue->tail->data : NULL;
}

void
g_queue_foreach (GQueue * queue,
                 GFunc func,
                 gpointer user_data)
{
  GList * node = queue->head;
  while (node != NULL)
  {
    GList * next = node->next;
    func (node->data, user_data);
    node = next;
  }
}

gboolean
g_queue_remove (GQueue * queue,
                gconstpointer data)
{
  GList * node = g_list_find (queue->head, data);

  if (node == NULL)
    return FALSE;

  if (node == queue->tail)
    queue->tail = node->prev;
  queue->head = g_list_delete_link (queue->head, node);
  queue->length--;

  return TRUE;
}
