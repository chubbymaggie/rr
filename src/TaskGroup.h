/* -*- Mode: C++; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#ifndef RR_TASKGROUP_H_
#define RR_TASKGROUP_H_

#include <sched.h>
#include <stdint.h>

#include <memory>
#include <set>

#include "HasTaskSet.h"
#include "TaskishUid.h"
#include "WaitStatus.h"

namespace rr {

class Session;
class ThreadDb;

/**
 * Tracks a group of tasks with an associated ID, set from the
 * original "thread group leader", the child of |fork()| which became
 * the ancestor of all other threads in the group.  Each constituent
 * task must own a reference to this.
 */
class TaskGroup : public HasTaskSet {
public:
  TaskGroup(Session* session, TaskGroup* parent, pid_t tgid, pid_t real_tgid,
            uint32_t serial);
  ~TaskGroup();

  typedef std::shared_ptr<TaskGroup> shr_ptr;

  /**
   * Mark the members of this task group as "unstable",
   * meaning that even though a task may look runnable, it
   * actually might not be.  (And so |waitpid(-1)| should be
   * used to schedule the next task.)
   *
   * This is needed to handle the peculiarities of mass Task
   * death at exit_group() and upon receiving core-dumping
   * signals.  The reason it's needed is easier to understand if
   * you keep in mind that the "main loop" of ptrace tracers is
   * /supposed/ to look like
   *
   *   while (true) {
   *     int tid = waitpid(-1, ...);
   *     // do something with tid
   *     ptrace(tid, PTRACE_SYSCALL, ...);
   *   }
   *
   * That is, the tracer is supposed to let the kernel schedule
   * threads and then respond to notifications generated by the
   * kernel.
   *
   * Obviously this isn't how rr's recorder loop looks, because,
   * among other things, rr has to serialize thread execution.
   * Normally this isn't much of a problem.  However, mass task
   * death is an exception.  What happens at a mass task death
   * is a sequence of events like the following
   *
   *  1. A task calls exit_group() or is sent a core-dumping
   *     signal.
   *  2. rr receives a PTRACE_EVENT_EXIT notification for the
   *     task.
   *  3. rr detaches from the dying/dead task.
   *  4. Successive calls to waitpid(-1) generate additional
   *     PTRACE_EVENT_EXIT notifications for each also-dead task
   *     in the original task's thread group.  Repeat (2) / (3)
   *     for each notified task.
   *
   * So why destabilization?  After (2), rr can't block on the
   * task shutting down (|waitpid(tid)|), because the kernel
   * harvests the LWPs of the dying task group in an unknown
   * order (which we shouldn't assume, even if we could guess
   * it).  If rr blocks on the task harvest, it will (usually)
   * deadlock.
   *
   * And because rr doesn't know the order of tasks that will be
   * reaped, it doesn't know which of the dying tasks to
   * "schedule".  If it guesses and blocks on another task in
   * the group's status-change, it will (usually) deadlock.
   *
   * So destabilizing a task group, from rr's perspective, means
   * handing scheduling control back to the kernel and not
   * trying to harvest tasks before detaching from them.
   *
   * NB: an invariant of rr scheduling is that all process
   * status changes happen as a result of rr resuming the
   * execution of a task.  This is required to keep tracees in
   * known states, preventing events from happening "behind rr's
   * back".  However, destabilizing a task group means that
   * these kinds of changes are possible, in theory.
   *
   * Currently, instability is a one-way street; it's only used
   * needed for death signals and exit_group().
   */
  void destabilize();

  const pid_t tgid;
  const pid_t real_tgid;

  WaitStatus exit_status;

  Session* session() const { return session_; }
  void forget_session() { session_ = nullptr; }

  TaskGroup* parent() { return parent_; }
  const std::set<TaskGroup*>& children() { return children_; }

  TaskGroupUid tguid() const { return TaskGroupUid(tgid, serial); }

  // We don't allow tasks to make themselves undumpable. If they try,
  // record that here and lie about it if necessary.
  bool dumpable;

  // Whether this task group has execed
  bool execed;

  // True when a task in the task-group received a SIGSEGV because we
  // couldn't push a signal handler frame. Only used during recording.
  bool received_sigframe_SIGSEGV;

  ThreadDb* thread_db();

private:
  TaskGroup(const TaskGroup&) = delete;
  TaskGroup operator=(const TaskGroup&) = delete;

  Session* session_;
  /** Parent TaskGroup, or nullptr if it's not a tracee (rr or init). */
  TaskGroup* parent_;

  std::set<TaskGroup*> children_;

  uint32_t serial;

  std::unique_ptr<ThreadDb> thread_db_;
};

} // namespace rr

#endif /* RR_TASKGROUP_H_ */
