/*
 * Unit tests for the hoox nano-glib compatibility layer.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxglib.h"

#include <stdio.h>
#include <string.h>

static int hx_failures = 0;

#define CHECK(expr) \
    HX_STMT_START { \
      if (!(expr)) \
      { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } HX_STMT_END

static void
test_mem (void)
{
  hx_int * p = hx_new0 (hx_int, 4);
  CHECK (p[0] == 0 && p[3] == 0);
  p[2] = 42;
  CHECK (p[2] == 42);
  hx_free (p);

  hx_char * d = hx_strdup ("hello");
  CHECK (strcmp (d, "hello") == 0);
  hx_free (d);

  hx_char * f = hx_strdup_printf ("%d-%s", 7, "x");
  CHECK (strcmp (f, "7-x") == 0);
  hx_free (f);
}

static void
test_atomic (void)
{
  hx_int v = 0;
  hx_size wide = 0;
  hx_atomic_int_inc (&v);
  hx_atomic_int_inc (&v);
  CHECK (hx_atomic_int_get (&v) == 2);
  CHECK (!hx_atomic_int_dec_and_test (&v));
  CHECK (hx_atomic_int_dec_and_test (&v));
  CHECK (hx_atomic_int_get (&v) == 0);

  hx_pointer p = NULL;
  hx_pointer newp = &v;
  CHECK (hx_atomic_pointer_compare_and_exchange (&p, NULL, newp));
  CHECK (hx_atomic_pointer_get (&p) == newp);
  CHECK (!hx_atomic_pointer_compare_and_exchange (&p, NULL, NULL));

  hx_atomic_size_set (&wide, HX_MAXSIZE);
  CHECK (hx_atomic_size_get (&wide) == HX_MAXSIZE);
}

static void
test_mutex (void)
{
  HxMutex m;
  hx_mutex_init (&m);
  hx_mutex_lock (&m);
  hx_mutex_unlock (&m);
  hx_mutex_clear (&m);

  HxRecMutex r;
  hx_rec_mutex_init (&r);
  hx_rec_mutex_lock (&r);
  hx_rec_mutex_lock (&r); /* recursive */
  hx_rec_mutex_unlock (&r);
  hx_rec_mutex_unlock (&r);
  hx_rec_mutex_clear (&r);
}

static void
test_array (void)
{
  HxArray * a = hx_array_new (FALSE, FALSE, sizeof (hx_int));
  hx_int i;
  for (i = 0; i != 100; i++)
    hx_array_append_val (a, i);
  CHECK (a->len == 100);
  CHECK (hx_array_index (a, hx_int, 0) == 0);
  CHECK (hx_array_index (a, hx_int, 99) == 99);
  hx_array_remove_index (a, 0);
  CHECK (a->len == 99);
  CHECK (hx_array_index (a, hx_int, 0) == 1);
  hx_array_remove_index_fast (a, 0);
  CHECK (a->len == 98);
  CHECK (hx_array_index (a, hx_int, 0) == 99);
  hx_array_free (a, TRUE);
}

static hx_int hx_freed_count = 0;
static void
count_free (hx_pointer p)
{
  (void) p;
  hx_freed_count++;
}

static void
test_ptrarray (void)
{
  HxPtrArray * a = hx_ptr_array_new_with_free_func (count_free);
  hx_freed_count = 0;
  hx_ptr_array_add (a, hx_strdup ("a"));
  hx_ptr_array_add (a, hx_strdup ("b"));
  hx_ptr_array_add (a, hx_strdup ("c"));
  CHECK (a->len == 3);
  hx_ptr_array_remove_index (a, 1);
  CHECK (a->len == 2);
  CHECK (hx_freed_count == 0); /* remove_index does not free */
  hx_ptr_array_free (a, TRUE);
  CHECK (hx_freed_count == 2);
}

static void
test_hash (void)
{
  HxHashTable * h = hx_hash_table_new (hx_direct_hash, hx_direct_equal);
  hx_int i;
  for (i = 1; i <= 200; i++)
    hx_hash_table_insert (h, HX_INT_TO_POINTER (i), HX_INT_TO_POINTER (i * 10));
  CHECK (hx_hash_table_size (h) == 200);
  CHECK (HX_POINTER_TO_INT (hx_hash_table_lookup (h, HX_INT_TO_POINTER (5))) == 50);
  CHECK (hx_hash_table_contains (h, HX_INT_TO_POINTER (200)));
  CHECK (hx_hash_table_remove (h, HX_INT_TO_POINTER (5)));
  CHECK (!hx_hash_table_contains (h, HX_INT_TO_POINTER (5)));
  CHECK (hx_hash_table_size (h) == 199);

  /* iterate and count */
  HxHashTableIter it;
  hx_pointer k, v;
  hx_uint count = 0;
  hx_hash_table_iter_init (&it, h);
  while (hx_hash_table_iter_next (&it, &k, &v))
    count++;
  CHECK (count == 199);

  /* iterate + remove all even keys */
  hx_hash_table_iter_init (&it, h);
  while (hx_hash_table_iter_next (&it, &k, &v))
  {
    if ((HX_POINTER_TO_INT (k) & 1) == 0)
      hx_hash_table_iter_remove (&it);
  }
  count = 0;
  hx_hash_table_iter_init (&it, h);
  while (hx_hash_table_iter_next (&it, &k, &v))
  {
    CHECK ((HX_POINTER_TO_INT (k) & 1) == 1);
    count++;
  }
  CHECK (count == 99); /* odd keys 1..199 (100), minus key 5 removed earlier */
  hx_hash_table_destroy (h);
}

static void
test_list (void)
{
  HxSList * sl = NULL;
  sl = hx_slist_append (sl, HX_INT_TO_POINTER (1));
  sl = hx_slist_append (sl, HX_INT_TO_POINTER (2));
  sl = hx_slist_prepend (sl, HX_INT_TO_POINTER (0));
  CHECK (hx_slist_length (sl) == 3);
  CHECK (HX_POINTER_TO_INT (hx_slist_nth_data (sl, 0)) == 0);
  CHECK (HX_POINTER_TO_INT (hx_slist_nth_data (sl, 2)) == 2);
  sl = hx_slist_remove (sl, HX_INT_TO_POINTER (1));
  CHECK (hx_slist_length (sl) == 2);
  hx_slist_free (sl);

  HxQueue q = HX_QUEUE_INIT;
  hx_queue_push_tail (&q, HX_INT_TO_POINTER (1));
  hx_queue_push_tail (&q, HX_INT_TO_POINTER (2));
  hx_queue_push_head (&q, HX_INT_TO_POINTER (0));
  CHECK (q.length == 3);
  CHECK (HX_POINTER_TO_INT (hx_queue_pop_head (&q)) == 0);
  CHECK (HX_POINTER_TO_INT (hx_queue_pop_tail (&q)) == 2);
  CHECK (HX_POINTER_TO_INT (hx_queue_pop_head (&q)) == 1);
  CHECK (hx_queue_is_empty (&q));
}

static void
test_string (void)
{
  HxString * s = hx_string_new ("a");
  hx_string_append (s, "bc");
  hx_string_append_c (s, 'd');
  hx_string_append_printf (s, "-%d", 42);
  CHECK (strcmp (s->str, "abcd-42") == 0);
  CHECK (s->len == 7);
  hx_string_prepend (s, ">>");
  CHECK (strcmp (s->str, ">>abcd-42") == 0);
  hx_string_free (s, TRUE);
}

int
main (void)
{
  test_mem ();
  test_atomic ();
  test_mutex ();
  test_array ();
  test_ptrarray ();
  test_hash ();
  test_list ();
  test_string ();

  if (hx_failures == 0)
  {
    printf ("compat: all tests passed\n");
    return 0;
  }

  printf ("compat: %d failure(s)\n", hx_failures);
  return 1;
}
