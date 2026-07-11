/*
 * hoox nano-glib: HxSList / HxList / HxQueue implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxlist.h"
#include "hxmem.h"

/* ---- HxSList ------------------------------------------------------------- */

HxSList *
hx_slist_append (HxSList * list,
                hx_pointer data)
{
  HxSList * node = hx_slice_new (HxSList);
  node->data = data;
  node->next = NULL;

  if (list == NULL)
    return node;

  HxSList * last = hx_slist_last (list);
  last->next = node;
  return list;
}

HxSList *
hx_slist_prepend (HxSList * list,
                 hx_pointer data)
{
  HxSList * node = hx_slice_new (HxSList);
  node->data = data;
  node->next = list;
  return node;
}

HxSList *
hx_slist_insert (HxSList * list,
                hx_pointer data,
                hx_int position)
{
  HxSList * prev;
  HxSList * node;

  if (position < 0 || list == NULL)
    return hx_slist_append (list, data);
  if (position == 0)
    return hx_slist_prepend (list, data);

  prev = list;
  while (--position > 0 && prev->next != NULL)
    prev = prev->next;

  node = hx_slice_new (HxSList);
  node->data = data;
  node->next = prev->next;
  prev->next = node;

  return list;
}

HxSList *
hx_slist_remove (HxSList * list,
                hx_constpointer data)
{
  HxSList * prev = NULL;
  HxSList * cur = list;

  while (cur != NULL)
  {
    if (cur->data == data)
    {
      if (prev != NULL)
        prev->next = cur->next;
      else
        list = cur->next;
      hx_slice_free (HxSList, cur);
      break;
    }
    prev = cur;
    cur = cur->next;
  }

  return list;
}

HxSList *
hx_slist_remove_link (HxSList * list,
                     HxSList * link_)
{
  HxSList * prev = NULL;
  HxSList * cur = list;

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

HxSList *
hx_slist_delete_link (HxSList * list,
                     HxSList * link_)
{
  list = hx_slist_remove_link (list, link_);
  hx_slice_free (HxSList, link_);
  return list;
}

HxSList *
hx_slist_find (HxSList * list,
              hx_constpointer data)
{
  while (list != NULL)
  {
    if (list->data == data)
      return list;
    list = list->next;
  }
  return NULL;
}

HxSList *
hx_slist_last (HxSList * list)
{
  if (list == NULL)
    return NULL;
  while (list->next != NULL)
    list = list->next;
  return list;
}

HxSList *
hx_slist_nth (HxSList * list,
             hx_uint n)
{
  while (n-- > 0 && list != NULL)
    list = list->next;
  return list;
}

hx_pointer
hx_slist_nth_data (HxSList * list,
                  hx_uint n)
{
  HxSList * node = hx_slist_nth (list, n);
  return (node != NULL) ? node->data : NULL;
}

hx_uint
hx_slist_length (HxSList * list)
{
  hx_uint n = 0;
  while (list != NULL)
  {
    n++;
    list = list->next;
  }
  return n;
}

HxSList *
hx_slist_reverse (HxSList * list)
{
  HxSList * prev = NULL;

  while (list != NULL)
  {
    HxSList * next = list->next;
    list->next = prev;
    prev = list;
    list = next;
  }

  return prev;
}

void
hx_slist_foreach (HxSList * list,
                 HxFunc func,
                 hx_pointer user_data)
{
  while (list != NULL)
  {
    HxSList * next = list->next;
    func (list->data, user_data);
    list = next;
  }
}

void
hx_slist_free_1 (HxSList * list)
{
  hx_slice_free (HxSList, list);
}

void
hx_slist_free (HxSList * list)
{
  while (list != NULL)
  {
    HxSList * next = list->next;
    hx_slice_free (HxSList, list);
    list = next;
  }
}

void
hx_slist_free_full (HxSList * list,
                   HxDestroyNotify free_func)
{
  while (list != NULL)
  {
    HxSList * next = list->next;
    if (free_func != NULL)
      free_func (list->data);
    hx_slice_free (HxSList, list);
    list = next;
  }
}

/* ---- HxList -------------------------------------------------------------- */

HxList *
hx_list_last (HxList * list)
{
  if (list == NULL)
    return NULL;
  while (list->next != NULL)
    list = list->next;
  return list;
}

HxList *
hx_list_first (HxList * list)
{
  if (list == NULL)
    return NULL;
  while (list->prev != NULL)
    list = list->prev;
  return list;
}

HxList *
hx_list_append (HxList * list,
               hx_pointer data)
{
  HxList * node = hx_slice_new (HxList);
  HxList * last;

  node->data = data;
  node->next = NULL;

  if (list == NULL)
  {
    node->prev = NULL;
    return node;
  }

  last = hx_list_last (list);
  last->next = node;
  node->prev = last;

  return list;
}

HxList *
hx_list_prepend (HxList * list,
                hx_pointer data)
{
  HxList * node = hx_slice_new (HxList);

  node->data = data;
  node->next = list;
  node->prev = NULL;

  if (list != NULL)
    list->prev = node;

  return node;
}

HxList *
hx_list_insert (HxList * list,
               hx_pointer data,
               hx_int position)
{
  HxList * tmp;
  HxList * node;

  if (position < 0 || list == NULL)
    return hx_list_append (list, data);
  if (position == 0)
    return hx_list_prepend (list, data);

  tmp = hx_list_nth (list, (hx_uint) position);
  if (tmp == NULL)
    return hx_list_append (list, data);

  node = hx_slice_new (HxList);
  node->data = data;
  node->prev = tmp->prev;
  node->next = tmp;
  if (tmp->prev != NULL)
    tmp->prev->next = node;
  tmp->prev = node;

  return (node->prev == NULL) ? node : list;
}

HxList *
hx_list_remove_link (HxList * list,
                    HxList * llink)
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

HxList *
hx_list_delete_link (HxList * list,
                    HxList * link_)
{
  list = hx_list_remove_link (list, link_);
  hx_slice_free (HxList, link_);
  return list;
}

HxList *
hx_list_remove (HxList * list,
               hx_constpointer data)
{
  HxList * cur = list;

  while (cur != NULL)
  {
    if (cur->data == data)
    {
      list = hx_list_delete_link (list, cur);
      break;
    }
    cur = cur->next;
  }

  return list;
}

HxList *
hx_list_find (HxList * list,
             hx_constpointer data)
{
  while (list != NULL)
  {
    if (list->data == data)
      return list;
    list = list->next;
  }
  return NULL;
}

HxList *
hx_list_nth (HxList * list,
            hx_uint n)
{
  while (n-- > 0 && list != NULL)
    list = list->next;
  return list;
}

hx_pointer
hx_list_nth_data (HxList * list,
                 hx_uint n)
{
  HxList * node = hx_list_nth (list, n);
  return (node != NULL) ? node->data : NULL;
}

hx_uint
hx_list_length (HxList * list)
{
  hx_uint n = 0;
  while (list != NULL)
  {
    n++;
    list = list->next;
  }
  return n;
}

HxList *
hx_list_reverse (HxList * list)
{
  HxList * last = NULL;

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
hx_list_foreach (HxList * list,
                HxFunc func,
                hx_pointer user_data)
{
  while (list != NULL)
  {
    HxList * next = list->next;
    func (list->data, user_data);
    list = next;
  }
}

void
hx_list_free (HxList * list)
{
  while (list != NULL)
  {
    HxList * next = list->next;
    hx_slice_free (HxList, list);
    list = next;
  }
}

void
hx_list_free_full (HxList * list,
                  HxDestroyNotify free_func)
{
  while (list != NULL)
  {
    HxList * next = list->next;
    if (free_func != NULL)
      free_func (list->data);
    hx_slice_free (HxList, list);
    list = next;
  }
}

/* ---- HxQueue ------------------------------------------------------------- */

HxQueue *
hx_queue_new (void)
{
  return hx_new0 (HxQueue, 1);
}

void
hx_queue_init (HxQueue * queue)
{
  queue->head = NULL;
  queue->tail = NULL;
  queue->length = 0;
}

void
hx_queue_clear (HxQueue * queue)
{
  hx_list_free (queue->head);
  hx_queue_init (queue);
}

void
hx_queue_free (HxQueue * queue)
{
  if (queue == NULL)
    return;
  hx_list_free (queue->head);
  hx_free (queue);
}

hx_boolean
hx_queue_is_empty (HxQueue * queue)
{
  return queue->head == NULL;
}

hx_uint
hx_queue_get_length (HxQueue * queue)
{
  return queue->length;
}

void
hx_queue_push_head (HxQueue * queue,
                   hx_pointer data)
{
  queue->head = hx_list_prepend (queue->head, data);
  if (queue->tail == NULL)
    queue->tail = queue->head;
  queue->length++;
}

void
hx_queue_push_tail (HxQueue * queue,
                   hx_pointer data)
{
  HxList * node = hx_slice_new (HxList);

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

hx_pointer
hx_queue_pop_head (HxQueue * queue)
{
  HxList * node;
  hx_pointer data;

  if (queue->head == NULL)
    return NULL;

  node = queue->head;
  data = node->data;

  queue->head = node->next;
  if (queue->head != NULL)
    queue->head->prev = NULL;
  else
    queue->tail = NULL;

  hx_slice_free (HxList, node);
  queue->length--;

  return data;
}

hx_pointer
hx_queue_pop_tail (HxQueue * queue)
{
  HxList * node;
  hx_pointer data;

  if (queue->tail == NULL)
    return NULL;

  node = queue->tail;
  data = node->data;

  queue->tail = node->prev;
  if (queue->tail != NULL)
    queue->tail->next = NULL;
  else
    queue->head = NULL;

  hx_slice_free (HxList, node);
  queue->length--;

  return data;
}

hx_pointer
hx_queue_peek_head (HxQueue * queue)
{
  return (queue->head != NULL) ? queue->head->data : NULL;
}

hx_pointer
hx_queue_peek_tail (HxQueue * queue)
{
  return (queue->tail != NULL) ? queue->tail->data : NULL;
}

void
hx_queue_foreach (HxQueue * queue,
                 HxFunc func,
                 hx_pointer user_data)
{
  HxList * node = queue->head;
  while (node != NULL)
  {
    HxList * next = node->next;
    func (node->data, user_data);
    node = next;
  }
}

hx_boolean
hx_queue_remove (HxQueue * queue,
                hx_constpointer data)
{
  HxList * node = hx_list_find (queue->head, data);

  if (node == NULL)
    return FALSE;

  if (node == queue->tail)
    queue->tail = node->prev;
  queue->head = hx_list_delete_link (queue->head, node);
  queue->length--;

  return TRUE;
}
