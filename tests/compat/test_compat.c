/*
 * Unit tests for the hoox nano-glib compatibility layer.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxglib.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(expr) \
    G_STMT_START { \
      if (!(expr)) \
      { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
      } \
    } G_STMT_END

static void
test_mem (void)
{
  gint * p = g_new0 (gint, 4);
  CHECK (p[0] == 0 && p[3] == 0);
  p[2] = 42;
  CHECK (p[2] == 42);
  g_free (p);

  gchar * d = g_strdup ("hello");
  CHECK (strcmp (d, "hello") == 0);
  g_free (d);

  gchar * f = g_strdup_printf ("%d-%s", 7, "x");
  CHECK (strcmp (f, "7-x") == 0);
  g_free (f);
}

static void
test_atomic (void)
{
  gint v = 0;
  g_atomic_int_inc (&v);
  g_atomic_int_inc (&v);
  CHECK (g_atomic_int_get (&v) == 2);
  CHECK (!g_atomic_int_dec_and_test (&v));
  CHECK (g_atomic_int_dec_and_test (&v));
  CHECK (g_atomic_int_get (&v) == 0);

  gpointer p = NULL;
  gpointer newp = &v;
  CHECK (g_atomic_pointer_compare_and_exchange (&p, NULL, newp));
  CHECK (g_atomic_pointer_get (&p) == newp);
  CHECK (!g_atomic_pointer_compare_and_exchange (&p, NULL, NULL));
}

static void
test_mutex (void)
{
  GMutex m;
  g_mutex_init (&m);
  g_mutex_lock (&m);
  g_mutex_unlock (&m);
  g_mutex_clear (&m);

  GRecMutex r;
  g_rec_mutex_init (&r);
  g_rec_mutex_lock (&r);
  g_rec_mutex_lock (&r); /* recursive */
  g_rec_mutex_unlock (&r);
  g_rec_mutex_unlock (&r);
  g_rec_mutex_clear (&r);
}

static void
test_array (void)
{
  GArray * a = g_array_new (FALSE, FALSE, sizeof (gint));
  gint i;
  for (i = 0; i != 100; i++)
    g_array_append_val (a, i);
  CHECK (a->len == 100);
  CHECK (g_array_index (a, gint, 0) == 0);
  CHECK (g_array_index (a, gint, 99) == 99);
  g_array_remove_index (a, 0);
  CHECK (a->len == 99);
  CHECK (g_array_index (a, gint, 0) == 1);
  g_array_remove_index_fast (a, 0);
  CHECK (a->len == 98);
  CHECK (g_array_index (a, gint, 0) == 99);
  g_array_free (a, TRUE);
}

static gint g_freed_count = 0;
static void
count_free (gpointer p)
{
  (void) p;
  g_freed_count++;
}

static void
test_ptrarray (void)
{
  GPtrArray * a = g_ptr_array_new_with_free_func (count_free);
  g_freed_count = 0;
  g_ptr_array_add (a, g_strdup ("a"));
  g_ptr_array_add (a, g_strdup ("b"));
  g_ptr_array_add (a, g_strdup ("c"));
  CHECK (a->len == 3);
  g_ptr_array_remove_index (a, 1);
  CHECK (a->len == 2);
  CHECK (g_freed_count == 0); /* remove_index does not free */
  g_ptr_array_free (a, TRUE);
  CHECK (g_freed_count == 2);
}

static void
test_hash (void)
{
  GHashTable * h = g_hash_table_new (g_direct_hash, g_direct_equal);
  gint i;
  for (i = 1; i <= 200; i++)
    g_hash_table_insert (h, GINT_TO_POINTER (i), GINT_TO_POINTER (i * 10));
  CHECK (g_hash_table_size (h) == 200);
  CHECK (GPOINTER_TO_INT (g_hash_table_lookup (h, GINT_TO_POINTER (5))) == 50);
  CHECK (g_hash_table_contains (h, GINT_TO_POINTER (200)));
  CHECK (g_hash_table_remove (h, GINT_TO_POINTER (5)));
  CHECK (!g_hash_table_contains (h, GINT_TO_POINTER (5)));
  CHECK (g_hash_table_size (h) == 199);

  /* iterate and count */
  GHashTableIter it;
  gpointer k, v;
  guint count = 0;
  g_hash_table_iter_init (&it, h);
  while (g_hash_table_iter_next (&it, &k, &v))
    count++;
  CHECK (count == 199);

  /* iterate + remove all even keys */
  g_hash_table_iter_init (&it, h);
  while (g_hash_table_iter_next (&it, &k, &v))
  {
    if ((GPOINTER_TO_INT (k) & 1) == 0)
      g_hash_table_iter_remove (&it);
  }
  count = 0;
  g_hash_table_iter_init (&it, h);
  while (g_hash_table_iter_next (&it, &k, &v))
  {
    CHECK ((GPOINTER_TO_INT (k) & 1) == 1);
    count++;
  }
  CHECK (count == 99); /* odd keys 1..199 (100), minus key 5 removed earlier */
  g_hash_table_destroy (h);
}

static void
test_list (void)
{
  GSList * sl = NULL;
  sl = g_slist_append (sl, GINT_TO_POINTER (1));
  sl = g_slist_append (sl, GINT_TO_POINTER (2));
  sl = g_slist_prepend (sl, GINT_TO_POINTER (0));
  CHECK (g_slist_length (sl) == 3);
  CHECK (GPOINTER_TO_INT (g_slist_nth_data (sl, 0)) == 0);
  CHECK (GPOINTER_TO_INT (g_slist_nth_data (sl, 2)) == 2);
  sl = g_slist_remove (sl, GINT_TO_POINTER (1));
  CHECK (g_slist_length (sl) == 2);
  g_slist_free (sl);

  GQueue q = G_QUEUE_INIT;
  g_queue_push_tail (&q, GINT_TO_POINTER (1));
  g_queue_push_tail (&q, GINT_TO_POINTER (2));
  g_queue_push_head (&q, GINT_TO_POINTER (0));
  CHECK (q.length == 3);
  CHECK (GPOINTER_TO_INT (g_queue_pop_head (&q)) == 0);
  CHECK (GPOINTER_TO_INT (g_queue_pop_tail (&q)) == 2);
  CHECK (GPOINTER_TO_INT (g_queue_pop_head (&q)) == 1);
  CHECK (g_queue_is_empty (&q));
}

static void
test_string (void)
{
  GString * s = g_string_new ("a");
  g_string_append (s, "bc");
  g_string_append_c (s, 'd');
  g_string_append_printf (s, "-%d", 42);
  CHECK (strcmp (s->str, "abcd-42") == 0);
  CHECK (s->len == 7);
  g_string_prepend (s, ">>");
  CHECK (strcmp (s->str, ">>abcd-42") == 0);
  g_string_free (s, TRUE);
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

  if (g_failures == 0)
  {
    printf ("compat: all tests passed\n");
    return 0;
  }

  printf ("compat: %d failure(s)\n", g_failures);
  return 1;
}
