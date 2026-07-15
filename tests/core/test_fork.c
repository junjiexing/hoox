/* POSIX fork coordination regression. */

#include "hoox.h"

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct _TestState TestState;

struct _TestState
{
  HooxInterceptor * interceptor;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int owner_locked;
  int release_owner;
  int owner_unlocked;
  int finish_owner;
  int forker_started;
  int prepare_returned;
  int allow_fork;
  int fork_result;
};

static void hold_interceptor_lock (hx_pointer user_data);
static void * owner_thread (void * user_data);
static void * forker_thread (void * user_data);

static void
hold_interceptor_lock (hx_pointer user_data)
{
  TestState * state = user_data;

  pthread_mutex_lock (&state->mutex);
  state->owner_locked = 1;
  pthread_cond_broadcast (&state->cond);
  while (!state->release_owner)
    pthread_cond_wait (&state->cond, &state->mutex);
  pthread_mutex_unlock (&state->mutex);
}

static void *
owner_thread (void * user_data)
{
  TestState * state = user_data;

  /* Leave a live per-thread interceptor context behind at fork time. */
  hoox_interceptor_ignore_current_thread (state->interceptor);
  hoox_interceptor_unignore_current_thread (state->interceptor);

  hoox_interceptor_with_lock_held (state->interceptor,
      hold_interceptor_lock, state);

  pthread_mutex_lock (&state->mutex);
  state->owner_unlocked = 1;
  pthread_cond_broadcast (&state->cond);
  while (!state->finish_owner)
    pthread_cond_wait (&state->cond, &state->mutex);
  pthread_mutex_unlock (&state->mutex);

  /* The parent's context registry must remain intact after recovery. */
  hoox_interceptor_ignore_current_thread (state->interceptor);
  hoox_interceptor_unignore_current_thread (state->interceptor);

  return NULL;
}

static void *
forker_thread (void * user_data)
{
  TestState * state = user_data;
  pid_t pid;
  int status;

  pthread_mutex_lock (&state->mutex);
  state->forker_started = 1;
  pthread_cond_broadcast (&state->cond);
  pthread_mutex_unlock (&state->mutex);

  hoox_prepare_to_fork ();

  pthread_mutex_lock (&state->mutex);
  state->prepare_returned = 1;
  pthread_cond_broadcast (&state->cond);
  while (!state->allow_fork)
    pthread_cond_wait (&state->cond, &state->mutex);
  pthread_mutex_unlock (&state->mutex);

  pid = fork ();
  if (pid == 0)
  {
    alarm (5);
    hoox_recover_from_fork_in_child ();

    /* This creates the child's sole context after vanished-thread contexts
     * have been pruned, and also proves the inherited instance lock is usable. */
    hoox_interceptor_ignore_current_thread (state->interceptor);
    hoox_interceptor_unignore_current_thread (state->interceptor);
    if (!hoox_interceptor_flush (state->interceptor))
      _exit (2);

    _exit (0);
  }

  if (pid < 0)
  {
    hoox_recover_from_fork_in_parent ();
    state->fork_result = 3;
    return NULL;
  }

  hoox_recover_from_fork_in_parent ();

  if (waitpid (pid, &status, 0) != pid ||
      !WIFEXITED (status) || WEXITSTATUS (status) != 0)
    state->fork_result = 4;

  return NULL;
}

int
main (void)
{
  TestState state = { 0, };
  pthread_t owner;
  pthread_t forker;
  int result = 0;

  pthread_mutex_init (&state.mutex, NULL);
  pthread_cond_init (&state.cond, NULL);

  hoox_init ();
  state.interceptor = hoox_interceptor_obtain ();

  if (pthread_create (&owner, NULL, owner_thread, &state) != 0)
  {
    result = 10;
    goto beach;
  }

  pthread_mutex_lock (&state.mutex);
  while (!state.owner_locked)
    pthread_cond_wait (&state.cond, &state.mutex);
  pthread_mutex_unlock (&state.mutex);

  if (pthread_create (&forker, NULL, forker_thread, &state) != 0)
  {
    result = 11;
    pthread_mutex_lock (&state.mutex);
    state.release_owner = 1;
    state.finish_owner = 1;
    pthread_cond_broadcast (&state.cond);
    pthread_mutex_unlock (&state.mutex);
    pthread_join (owner, NULL);
    goto beach;
  }

  pthread_mutex_lock (&state.mutex);
  while (!state.forker_started)
    pthread_cond_wait (&state.cond, &state.mutex);
  pthread_mutex_unlock (&state.mutex);

  /* prepare_to_fork() must wait for an in-progress interceptor operation. */
  usleep (250000);
  pthread_mutex_lock (&state.mutex);
  if (state.prepare_returned)
    result = 12;
  state.release_owner = 1;
  pthread_cond_broadcast (&state.cond);
  while (!state.owner_unlocked || !state.prepare_returned)
    pthread_cond_wait (&state.cond, &state.mutex);
  state.allow_fork = 1;
  pthread_cond_broadcast (&state.cond);
  pthread_mutex_unlock (&state.mutex);

  pthread_join (forker, NULL);

  pthread_mutex_lock (&state.mutex);
  state.finish_owner = 1;
  pthread_cond_broadcast (&state.cond);
  pthread_mutex_unlock (&state.mutex);
  pthread_join (owner, NULL);

  if (result == 0)
    result = state.fork_result;

beach:
  hoox_interceptor_unref (state.interceptor);
  hoox_deinit ();

  pthread_cond_destroy (&state.cond);
  pthread_mutex_destroy (&state.mutex);

  return result;
}
