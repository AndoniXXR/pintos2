
/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
  {
    list_insert_ordered (&sema->waiters, &thread_current ()->elem, compare_priority, 0);
    thread_block ();
  }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
// Libera un semáforo, permitiendo que uno o más hilos que estaban esperando puedan continuar.
void
sema_up (struct semaphore *sema)
{
  // Deshabilita las interrupciones para sincronizar los accesos al semáforo:
  enum intr_level old_level = intr_disable ();

  // Comprueba que el semáforo sea válido:
  ASSERT (sema != NULL);

  // Si hay hilos esperando en la lista del semáforo:
  if (!list_empty (&sema->waiters)) {
    // Ordena la lista de espera por prioridad de los hilos (para despertar primero a los más prioritarios):
    list_sort(&sema->waiters, compare_priority, 0);

    // Despierta al primer hilo de la lista, permitiéndole continuar su ejecución:
    thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  }

  // Incrementa el valor del semáforo para indicar que ahora está disponible:
  sema->value++;

  // Sugiere al planificador que ceda el procesador a otro hilo (si es oportuno):
  thread_yield();

  // Restaura el nivel de interrupciones anterior:
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->is_donated=false;
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
// Función para adquirir un lock.
void
lock_acquire (struct lock *lock)
{

  // Asegura de que el argumento 'lock' sea válido.
  ASSERT (lock != NULL);

  // Asegura de que no se llame desde un contexto de interrupción.
  ASSERT (!intr_context ());

  // Asegura de que el hilo actual no posea el lock.
  ASSERT (!lock_held_by_current_thread (lock));

  // Si el lock ya está en posesión de otro hilo:
  if(lock->holder != NULL)
  {
    // Indica que el hilo actual está esperando este lock.
    thread_current()->waiting_for= lock;

    // Si la prioridad del hilo actual es mayor que la del poseedor del lock:
    if(lock->holder->priority < thread_current()->priority)
    {
      // Recorre la cadena de locks que está esperando el hilo actual.
      struct thread *temp=thread_current();
      while(temp->waiting_for!=NULL)
      {
        // Obtén el lock actual en la cadena.
        struct lock *cur_lock=temp->waiting_for;

        // Actualiza la prioridad del poseedor del lock actual.
        cur_lock->holder->priorities[cur_lock->holder->size] = temp->priority;
        cur_lock->holder->size+=1;
        cur_lock->holder->priority = temp->priority;

        // Si el poseedor del lock actual está listo para ejecutarse:
        if(cur_lock->holder->status == THREAD_READY)
        {
          // Se resolvió la inversión de prioridad, sal del bucle.
          break;
        }

        // Avanza al siguiente lock en la cadena.
        temp=cur_lock->holder;
      }

      // Si no se resolvió la inversión de prioridad:
      if(!lock->is_donated)
      {
        // Implementa la herencia de prioridad.
        lock->holder->donation_no +=1;
      }

      // Marca el lock como recibido una donación.
      lock->is_donated = true;

      // Reordena la lista de listos.
      sort_ready_list();
    }
  }

  // Adquiere el semáforo del lock.
  sema_down (&lock->semaphore);

  // Indica que el hilo actual ahora posee el lock.
  lock->holder = thread_current ();

  // Indica que el hilo actual ya no está esperando ningún lock.
  lock->holder->waiting_for=NULL;
}


/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
// Intenta adquirir el lock 'lock' sin esperar.

bool
lock_try_acquire (struct lock *lock)
{
  // Indica si se pudo adquirir el lock con éxito.
  bool success;

  // Asegura de que el argumento 'lock' sea válido.
  ASSERT (lock != NULL);

  // Asegura de que el hilo actual no posea el lock actualmente.
  ASSERT (!lock_held_by_current_thread (lock));

  // Intenta decrementar el semáforo asociado al lock para adquirirlo.
  success = sema_try_down (&lock->semaphore);

  // Si se pudo adquirir el lock:
  if (success) {
    // Indica que el hilo actual ahora posee el lock.
    lock->holder = thread_current ();
  }

  // Devuelve `true` si se pudo adquirir el lock, `false` en caso contrario.
  return success;
}


/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
// Libera el lock 'lock' y permite que otro hilo lo adquiera.

void
lock_release (struct lock *lock)
{
  // Asegura de que el argumento 'lock' sea válido.
  ASSERT (lock != NULL);

  // Asegura de que el hilo actual posea el lock.
  ASSERT (lock_held_by_current_thread (lock));

  // Obten el semáforo interno del lock para acceder a su lista de espera.
  struct semaphore *lock_sema = &lock->semaphore;

  // Ordena la lista de espera del semáforo por prioridad de los hilos.
  list_sort(&lock_sema->waiters, compare_priority, 0);

  // Si se estaba utilizando donación de prioridad:
  if (lock->is_donated) {
    // Reduce el número de donaciones del hilo actual.
    thread_current()->donation_no -= 1;

    // Obtén la prioridad del primer hilo en la lista de espera.
    int elem = list_entry (list_front (&lock_sema->waiters), struct thread, elem)->priority;

    // Busca la donación que corresponda al hilo actual y restablece su prioridad.
    search_array(thread_current(), elem);
    thread_current()->priority = thread_current()->priorities[(thread_current()->size) - 1];

    // Indica que la donación de prioridad ya no está activa.
    lock->is_donated = false;
  }

  // Si el hilo actual no tiene donaciones pendientes:
  if (thread_current()->donation_no == 0) {
    // Restaura la prioridad original del hilo.
    thread_current()->size = 1;
    thread_current()->priority = thread_current()->priorities[0];
  }

  // Indica que el lock ya no está siendo utilizado por ningún hilo.
  lock->holder = NULL;

  // Despierta a un hilo de la lista de espera del semáforo para que pueda adquirir el lock.
  sema_up (&lock->semaphore);
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
// Hace que el hilo actual espere en la condición 'cond', liberando temporalmente el lock 'lock'.
void
cond_wait (struct condition *cond, struct lock *lock)
{
  // Crea un elemento de semáforo para representar a este hilo en la lista de espera.
  struct semaphore_elem waiter;

  // Asegura de que los argumentos sean válidos.
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  // Asegura de que no se llame desde un manejador de interrupciones.
  ASSERT (!intr_context ());

  // Asegura de que el hilo actual tenga el lock asociado a la condición.
  ASSERT (lock_held_by_current_thread (lock));

  // Inicializa el semáforo del elemento de espera en 0 (para que el hilo se bloquee).
  sema_init (&waiter.semaphore, 0);

  // Agrega el elemento de espera a la lista de espera de la condición, ordenada por prioridad.
  list_insert_ordered (&cond->waiters, &waiter.elem, compare_priority, 0);

  // Libera temporalmente el lock para permitir que otros hilos puedan acceder a los recursos compartidos.
  lock_release (lock);

  // El hilo actual se bloquea en el semáforo del elemento de espera.
  sema_down (&waiter.semaphore);

  // Vuelve a adquirir el lock una vez que el hilo sea despertado.
  lock_acquire (lock);
}


/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// Despierta a un hilo que está esperando en la condición 'cond'.
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  // Asegura de que los argumentos sean válidos.
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  // Asegura  de que no se llame desde un manejador de interrupciones.
  ASSERT (!intr_context ());

  // Asegura de que el hilo actual posea el lock asociado a la condición.
  ASSERT (lock_held_by_current_thread (lock));

  // Ordena la lista de espera de la condición por prioridad de los hilos.
  list_sort(&cond->waiters, compare_sema, 0);

  // Si hay hilos esperando en la condición:
  if (!list_empty (&cond->waiters)) {
    // Despierta al hilo con mayor prioridad de la lista.
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                           struct semaphore_elem, elem)->semaphore);
  }
}


/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// Despierta a todos los hilos esperando en la condición 'cond'.

void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  // Asegura de que el argumento 'cond' sea válido.
  ASSERT (cond != NULL);

  // Asegura de que el argumento 'lock' sea válido.
  ASSERT (lock != NULL);

  // Mientras haya hilos esperando en la condición 'cond':
  while (!list_empty (&cond->waiters)) {
    // Llama a cond_signal para despertar a un hilo y liberar el lock temporalmente.
    cond_signal (cond, lock);
  }
}


// Compara dos elementos de la lista de espera de semáforos para ordenarlos.
bool compare_sema(struct list_elem *l1, struct list_elem *l2, void *aux)
{
  // Extrae los elementos de semáforo de los nodos de la lista.
  struct semaphore_elem *t1 = list_entry(l1, struct semaphore_elem, elem);
  struct semaphore_elem *t2 = list_entry(l2, struct semaphore_elem, elem);

  // Obtén los semáforos asociados a cada elemento.
  struct semaphore *s1 = &t1->semaphore;
  struct semaphore *s2 = &t2->semaphore;

  // Si el primer hilo en la lista de espera del semáforo s1 tiene mayor prioridad que el primer hilo en la lista de espera de s2, devuelve verdadero.
  if (list_entry(list_front(&s1->waiters), struct thread, elem)->priority > list_entry(list_front(&s2->waiters), struct thread, elem)->priority) {
    return true;
  }

  // Si llegan aquí, significa que el primer hilo en la lista de espera de s1 tiene menor o igual prioridad que el de s2. Devuelve falso para ordenar s2 antes que s1.
  return false;
}

