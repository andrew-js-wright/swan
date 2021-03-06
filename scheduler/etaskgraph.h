/*
 * Copyright (C) 2011 Hans Vandierendonck (hvandierendonck@acm.org)
 * Copyright (C) 2011 George Tzenakis (tzenakis@ics.forth.gr)
 * Copyright (C) 2011 Dimitrios S. Nikolopoulos (dsn@ics.forth.gr)
 * 
 * This file is part of Swan.
 * 
 * Swan is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Swan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Swan.  If not, see <http://www.gnu.org/licenses/>.
 */

// -*- c++ -*-
/* taskgraph.h
 * This file implements an explicit task graph where edges between tasks
 * are explicitly maintained.
 *
 * TODO:
 * + finalize() when adding to ready list
 */
#ifndef ETASKGRAPH_H
#define ETASKGRAPH_H

#include <cstdint>
#include <iostream>
#include <queue>

#include "wf_frames.h"
#include "lock.h"
#include "lfllist.h"

namespace obj {

// ----------------------------------------------------------------------
// link_metadata: task graph metadata per stored frame
// ----------------------------------------------------------------------
class link_metadata {
    link_metadata * next;
    friend class sl_list_traits<link_metadata>;
};

//----------------------------------------------------------------------
// Traits for accessing elements stored in a dl_list<>, in global
// namespace.
//----------------------------------------------------------------------

} // end namespace obj because the traits class must be in global namespace

template<>
struct sl_list_traits<obj::link_metadata> {
    typedef obj::link_metadata T;
    typedef obj::link_metadata ValueType;

    // not implemented -- static size_t get_depth( T * elm );
    // not implemented -- static bool is_ready( T * elm );

    // static void set_prev( T * elm, T * prev ) { elm->prev = prev; }
    // static T * get_prev( T const * elm ) { return elm->prev; }
    static void set_next( T * elm, T * next ) { elm->next = next; }
    static T * get_next( T const * elm ) { return elm->next; }
};

namespace obj { // reopen
// ----------------------------------------------------------------------
// taskgraph: task graph roots in ready_list
// ----------------------------------------------------------------------
class taskgraph {
    typedef cas_mutex mutex_t;

private:
    sl_list<link_metadata> ready_list;
    mutable mutex_t mutex;

public:
    ~taskgraph() {
	assert( ready_list.empty() && "Pending tasks at destruction time" );
    }

    link_metadata * get_ready_task() {
	lock();
	link_metadata * task = 0;
	if( !ready_list.empty() ) {
	    task = ready_list.front();
	    ready_list.pop();
	}
	unlock();
	// errs() << "get_ready_task from TG " << this << ": " << task << "\n";
	return task;
    }

    void add_ready_task( link_metadata * fr ) {
	// Translate from task_metadata to link_metadata
	// Can only be successfully done in case of queued_frame!
	// errs() << "add_ready_task to TG " << this << ": " << fr << "\n";
	lock();
	ready_list.push_back( fr );
	unlock();
    }

    // Don't need a lock in this check because it is based on polling a
    // single variable
    bool empty() const {
	// lock();
	bool ret = ready_list.empty();
	// unlock();
	return ret;
    }

private:
    void lock() const { mutex.lock(); }
    void unlock() const { mutex.unlock(); }
};

// ----------------------------------------------------------------------
// Functor for replacing the pointers to a pending frame with pointers
// to the corresponding full frame.
// ----------------------------------------------------------------------
// Replace frame functor
template<typename MetaData, typename Task>
struct replace_frame_functor {
    Task * from, * to;
    replace_frame_functor( Task * from_, Task * to_ )
	: from( from_ ), to( to_ ) { }

    template<typename T>
    bool operator () ( indep<T> & obj, typename indep<T>::dep_tags & sa ) {
	MetaData * md = obj.get_version()->get_metadata();
	md->lock();
	md->replace_readers( from, to, &sa );
	md->unlock();
	return true;
    }
    template<typename T>
    bool operator () ( outdep<T> & obj, typename outdep<T>::dep_tags & sa ) {
	MetaData * md = obj.get_version()->get_metadata();
	md->lock();
	md->replace_writer( from, to );
	md->unlock();
	return true;
    }
    template<typename T>
    bool operator () ( inoutdep<T> & obj, typename inoutdep<T>::dep_tags & sa ) {
	MetaData * md = obj.get_version()->get_metadata();
	md->lock();
	// md->replace_readers( from, to, &sa ); // -- always mute
	md->replace_writer( from, to );
	md->unlock();
	return true;
    }
    template<typename T>
    bool operator () ( truedep<T> & obj, typename truedep<T>::dep_tags & sa ) {
	return true;
    }
};

// A replace frame function
#if STORED_ANNOTATIONS
template<typename MetaData, typename Task>
static inline void arg_replace_fn( Task * from, Task * to, task_data_t & td ) {
    replace_frame_functor<MetaData, Task> fn( from, to );
    char * args = td.get_args_ptr();
    char * tags = td.get_tags_ptr();
    size_t nargs = td.get_num_args();
    arg_apply_stored_fn( fn, nargs, args, tags );
}
#else
template<typename MetaData, typename Task, typename... Tn>
static inline void arg_replace_fn( Task * from, Task * to, task_data_t & td ) {
    replace_frame_functor<MetaData, Task> fn( from, to );
    char * args = td.get_args_ptr();
    char * tags = td.get_tags_ptr();
    arg_apply_fn<replace_frame_functor<MetaData, Task>,Tn...>( fn, args, tags );
}
#endif

// ----------------------------------------------------------------------
// input_tags, sync_tags: tag storage for all dependency usage types with
// incoming semantics.
// Note: indep's go on readers list and deps list.
//       inoutdep's go on deps list only
//       outdep's go on none of these lists (assuming renaming, else they
//                behave like inoutdep's in this model)
// ----------------------------------------------------------------------
class task_metadata;
class etg_metadata;
class input_tags;

// sync_tags links indep and inoutdep tasks on the deps list of an outdep
// or an inoutdep task. Note an inoutdep will appear only if no indeps
// appear, thus deps is either a list of >=1 indeps, or 1! inoutdep, or empty
class sync_tags {
    task_metadata * st_task;
    sync_tags * st_next;

    friend class etg_metadata;
    friend class task_metadata;
    friend class sl_list_traits<sync_tags>;
    friend class dl_list_traits<input_tags>;
};

// input_tags applies to indep tasks, providing a way to link the indep
// task on the readers list of an object, and/or providing a way to point
// to the single following inoutdep task that may exist. indep tasks can wakeup
// at most one task and that's an inoutdep.
class readers_tags {
    input_tags * it_prev, * it_next;
    friend class dl_list_traits<input_tags>;
};

class input_tags : public sync_tags {
    union {
	readers_tags rd;
	sync_tags inout;
    };

    friend class etg_metadata;
    friend class dl_list_traits<input_tags>;
};

} // end namespace obj because the traits class must be in global namespace

template<>
struct dl_list_traits<obj::input_tags> {
    typedef obj::input_tags T;
    typedef obj::task_metadata ValueType;

    // not implemented -- static size_t get_depth( T * elm );
    // not implemented -- static bool is_ready( T * elm );

    static void set_prev( T * elm, T * prev ) { elm->rd.it_prev = prev; }
    static T * get_prev( T const * elm ) { return elm->rd.it_prev; }
    static void set_next( T * elm, T * next ) { elm->rd.it_next = next; }
    static T * get_next( T const * elm ) { return elm->rd.it_next; }

    static ValueType * get_value( T const * elm ) { return elm->st_task; }
};

template<>
struct sl_list_traits<obj::sync_tags> {
    typedef obj::sync_tags T;
    typedef obj::task_metadata ValueType;

    static void set_next( T * elm, T * next ) { elm->st_next = next; }
    static T * get_next( T const * elm ) { return elm->st_next; }

    static ValueType * get_value( T const * elm ) { return elm->st_task; }
};

namespace obj { // reopen

// ----------------------------------------------------------------------
// etg_metadata: dependency-tracking metadata (not versioning)
// ----------------------------------------------------------------------
class etg_metadata {
    typedef cas_mutex mutex_t;

private:
    // Every task can be on one last_readers list for every in/inout
    // dependency that it has. Embed this list in the tags with next/prev
    // links and a pointer to the task.
    task_metadata * last_writer; // last writer
    dl_list<input_tags> last_readers; // set of readers after the writer
    mutex_t mutex;

public:
    etg_metadata() : last_writer( 0 ) {
	// errs() << "etg_metadata create: " << this << "\n";
    }
    ~etg_metadata() {
	// errs() << "etg_metadata delete: " << this << "\n";
	assert( last_writer == 0
		&& "Must have zero writers when destructing etg_metadata" );
	assert( !has_readers()
		&& "Must have zero readers when destructing etg_metadata" );
    }

    // External inferface
    bool rename_has_readers() const { return has_readers(); }
    bool rename_has_writers() const { return has_writers(); }

    // Dependency queries on last_readers
    typedef dl_list<input_tags>::const_iterator reader_iterator;
    reader_iterator reader_begin() const { return last_readers.begin(); }
    reader_iterator reader_end() const { return last_readers.end(); }

    // Register users of this object.
    void add_reader( task_metadata * t, input_tags * tags ) {
	// errs() << "obj " << this << " add reader " << t << "\n";
	tags->st_task = t;
	last_readers.push( tags );
    }
    void add_writer( task_metadata * t ) {
	// errs() << "obj " << this << " add writer " << t << "\n";
	last_readers.clear();
	last_writer = t;
    }

    // Erase links if we are about to destroy a task
    void del_writer( task_metadata * fr ) {
	if( last_writer == fr )
	    last_writer = 0;
	// errs() << "* removing writer " << fr << " from "
	// << this << " last_writer is now " << last_writer << "\n";
    }
    void del_reader( task_metadata * fr, input_tags * tags ) {
	// This is already blazing fast ;-). The generic solution cannot
	// provide the pointer to the list node to delete by itself.
	last_readers.erase( dl_list<input_tags>::iterator( tags ) );
    }

    // Replace writer/readers
    void replace_writer( task_metadata * from, task_metadata * to ) {
	if( last_writer == from )
	    last_writer = to;
    }
    void replace_readers( task_metadata * from, task_metadata * to,
			  input_tags * tags ) {
	// This is already blazing fast ;-). The generic solution cannot
	// provide the pointer to the list node to update by itself.
	assert( tags->st_task == from && "replace_readers list corrupt" );
	tags->st_task = to;
    }

    // Dependency queries on last_readers and last_writer
    bool has_readers() const { return !last_readers.empty(); }
    bool has_writers() const { return get_last_writer() != 0; }
    task_metadata * get_last_writer() const { return last_writer; }

    // Link to readers
    inline void link_readers( task_metadata * fr, sync_tags * tags );
    inline void link_writer( task_metadata * fr, sync_tags * tags );

    // Locking
    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
};

// Some debugging support. Const-ness of printed argument is a C++ library
// requirement, but we want to keep the lock as non-const.
inline std::ostream &
operator << ( std::ostream & os, const etg_metadata & md_const ) {
    etg_metadata & md = *const_cast<etg_metadata *>( &md_const );
    os << "taskgraph_md={dep_tasks=";
    md.lock();
    for( etg_metadata::reader_iterator
	     I=md.reader_begin(), E=md.reader_end(); I != E; ++I )
	os << *I << ", ";
    md.unlock();
    os << '}';
    return os;
}

// ----------------------------------------------------------------------
// task_metadata: dependency-tracking metadata for tasks (pending and stack)
//     We require the list of objects that are held by this task. This is
//     obtained through the actual argument list of the task.
// ----------------------------------------------------------------------
class full_metadata;

class task_metadata : public task_data_t {
    typedef cas_mutex mutex_t;
#if !STORED_ANNOTATIONS
    typedef void (*replace_fn_t)( task_metadata *, task_metadata *, task_data_t & );
#endif

private:
    // Each task can be part of one "deps" list per argument (only in/inout).
    // Thus, we can implement this list using prev/next links in the
    // per-argument tags. Question then is how to recompute the task from
    // the tags address. Need to replicate it inside each of the tags.
    // Note that this is not worse than the std::vector<> solution, which
    // basically does the same thing.
    sl_list<sync_tags> deps;
    taskgraph * graph;
#if !STORED_ANNOTATIONS
    replace_fn_t replace_fn;
#endif
    size_t incoming_count;
    mutex_t mutex;

protected:
    // Default constructor
    task_metadata() : graph( 0 ),
#if !STORED_ANNOTATIONS
		      replace_fn( 0 ),
#endif
		      incoming_count( 0 ) {
	// errs() << "task_metadata create: " << this << '\n';
    }
    ~task_metadata() {
	// errs() << "task_metadata delete: " << this << '\n';
    }
    // Constructor for creating stack frame from pending frame
    inline void create_from_pending( task_metadata * from, full_metadata * ff );
    inline void convert_to_full( full_metadata * ff );

public:
    template<typename... Tn>
    inline void create( full_metadata * ff );

public:
    // Add edge: Always first increment incoming count, then add edge.
    // Reason: we don't have a lock on "to" and edge should not be observable
    // before we increment the incoming count. Otherwise, another (racing)
    // thread could erroneously conclude that task "to" is ready.
    // Note: we should have a lock on this such that we are thread-safe on deps
    void add_edge( task_metadata * to, sync_tags * tags ) {
	to->add_incoming();
	tags->st_task = to;
	deps.push_back( tags );
    }

    // Locking
    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }

    // Iterating
    typedef sl_list<sync_tags>::const_iterator dep_iterator;
    dep_iterator dep_begin() const { return deps.begin(); }
    dep_iterator dep_end()   const { return deps.end();   }

    // Waking up dependents - only for a stack_frame
    inline void wakeup_deps();

    // Self wakeup (change of state between arg_ready() and arg_issue())
    inline void add_to_graph();

    // Ready counter
    void add_incoming() { __sync_fetch_and_add( &incoming_count, 1 ); }
    bool del_incoming() {
	return __sync_fetch_and_add( &incoming_count, -1 ) == 1;
    }
    size_t get_incoming() const volatile { return incoming_count; }

    // To avoid races when adding a node to the graph, we must increment
    // the incoming_count to make sure that the task is not considered ready
    // when one of the inputs becomes available, but the others have not been
    // added yet.
    template<typename... Tn>
    void start_registration() { add_incoming(); }
    void stop_registration( bool wakeup = false ) {
	if( del_incoming() && wakeup )
	    add_to_graph();
    }

    void start_deregistration() { }
    void stop_deregistration() { lock(); wakeup_deps(); unlock(); }
};

void
etg_metadata::link_readers( task_metadata * fr, sync_tags * tags ) {
/*
    for( reader_iterator I=reader_begin(), E=reader_end(); I != E; ++I ) {
	task_metadata * t = *I;
	input_tags * itags = I.get_node();
	// errs() << "obj " << this << " link reader " << t << " to " << fr  << '\n';
	t->lock();
	assert( fr->dep_begin() == fr->dep_end() );
	t->add_edge( fr, &itags->inout ); // requires lock on fr
	t->unlock();
    }
*/
    // Need this slightly complicate loop structure because we are going
    // to recycle the link list node space from the readers list in the
    // deps list.
    reader_iterator I = reader_begin();
    reader_iterator E = reader_end();
    if( I != E ) {
	reader_iterator J=I;
	for( ++J; I != E; ++I ) {
	    task_metadata * t = *I;
	    input_tags * itags = I.get_node();
	    // errs() << "obj " << this << " link reader " << t << " to " << fr  << '\n';
	    t->lock();
	    assert( fr->dep_begin() == fr->dep_end() );
	    t->add_edge( fr, &itags->inout ); // requires lock on fr
	    t->unlock();
	}
    }
    last_readers.clear();
}

void
etg_metadata::link_writer( task_metadata * fr, sync_tags * tags ) {
    if( last_writer ) {
	// errs() << "obj " << this << " link writer " << last_writer << " to " << fr  << '\n';
	last_writer->lock();
	last_writer->add_edge( fr, tags ); // requires lock on fr
	last_writer->unlock();
    }
}

// ----------------------------------------------------------------------
// pending_metadata: task graph metadata per pending frame
// ----------------------------------------------------------------------
class pending_metadata : public task_metadata, public link_metadata {
public:
    static inline
    pending_metadata * get_from_task( task_metadata * task ) {
	return static_cast<pending_metadata *>( task );
    }
    static inline
    pending_metadata * get_from_link( link_metadata * link ) {
	return static_cast<pending_metadata *>( link );
    }
};

// ----------------------------------------------------------------------
// full_metadata: task graph metadata per full frame
// ----------------------------------------------------------------------
class full_metadata {
    taskgraph graph;

protected:
    full_metadata() { }

public:
    // This functionality is implemented through arg_issue of the arguments
    void push_pending( pending_metadata * frame ) { }

    pending_metadata * get_ready_task() {
	return pending_metadata::get_from_link( graph.get_ready_task() );
    }
    pending_metadata * get_ready_task_after( task_metadata * prev ) {
	return pending_metadata::get_from_link( graph.get_ready_task() );
    }

    taskgraph * get_graph() { return &graph; }

    // void lock() { graph.lock(); }
    // void unlock() { graph.unlock(); }
};

// ----------------------------------------------------------------------
// Member function implementations with cyclic dependencies on class
// definitions.
// ----------------------------------------------------------------------
void
task_metadata::create_from_pending( task_metadata * from, full_metadata * ff ) {
    graph = ff->get_graph();
    
    // First change the task pointer for each argument's dependency list,
    // then assign all outgoing dependencies from <from> to us.
    lock();
#if STORED_ANNOTATIONS
    arg_replace_fn<etg_metadata,task_metadata>( from, this, get_task_data() );
#else
    assert( from->replace_fn && "Don't have a replace frame function" );
    (*from->replace_fn)( from, this, get_task_data() );
#endif

    assert( deps.empty() );
    // from->lock(); -- don't lock, nobody has a pointer any more
    deps = from->deps; // ain't that easy
    // from->unlock();
    unlock();
}

void
task_metadata::convert_to_full( full_metadata * ff ) {
    graph = ff->get_graph();
    assert( graph != 0 && "create_from_pending with null graph" );
}

template<typename... Tn>
void
task_metadata::create( full_metadata * ff ) {
    graph = ff->get_graph();
#if !STORED_ANNOTATIONS
    replace_fn = &arg_replace_fn<etg_metadata,task_metadata,Tn...>;
    // errs() << "set graph in " << this << " to " << graph << " fn to " << (void *)replace_fn << "\n";
#endif
}

void
task_metadata::wakeup_deps() {
    for( dep_iterator I=dep_begin(), E=dep_end(); I != E; ++I ) {
	task_metadata * t = *I;
	// Don't need a lock: only one task can be the last to
	// wakeup t (atomic decrement of join counter)
	// t->lock();
	// errs() << "task " << this << " dec_incoming " << t << '\n';
	if( t->del_incoming() ) {
	    graph->add_ready_task( pending_metadata::get_from_task( t ) );
	    // errs() << "task " << this << " wakes up " << t << '\n';
	}
	// t->unlock();
    }
}

void
task_metadata::add_to_graph() {
    graph->add_ready_task( pending_metadata::get_from_task( this ) );
}

// ----------------------------------------------------------------------
// Dependency handling traits
// ----------------------------------------------------------------------

// A fully serialized version
class serial_dep_tags { };

struct serial_dep_traits {
    static
    void arg_issue( task_metadata * fr,
		    obj_instance<etg_metadata> & obj,
		    sync_tags * sa ) {
	etg_metadata * md = obj.get_version()->get_metadata();
	md->lock();

	// Don't add an edge to the last writer if there are last
	// readers, because that edge is redundant.
	if( md->has_readers() )
	    md->link_readers( fr, sa );
	else
	    md->link_writer( fr, sa );
	md->add_writer( fr );

	md->unlock();
    }
    static
    bool arg_ini_ready( const obj_instance<etg_metadata> & obj ) {
	return !obj.get_version()->get_metadata()->has_readers()
	    & !obj.get_version()->get_metadata()->has_writers();
    }
    static
    void arg_release( task_metadata * fr, obj_instance<etg_metadata> & obj ) {
	obj_version<etg_metadata> * v = obj.get_version();
	etg_metadata * md = v->get_metadata();

	md->lock();
	md->del_writer( fr );
	md->unlock();
    }
};

// Input dependency tags
class indep_tags : public indep_tags_base, public input_tags,
		   public serial_dep_tags { };

// Output dependency tags require fully serialized tags in the worst case
class outdep_tags : public outdep_tags_base, public serial_dep_tags { };

// Input/output dependency tags require fully serialized tags
class inoutdep_tags : public inoutdep_tags_base, public sync_tags,
		      public serial_dep_tags { };

/* @note
 * Locking strategy when doing arg_issue:
 *    Set incoming +1 artificially early in arg_issue_fn or in constructor
 *    and then add dependencies *without* a lock. When done, dec incoming,
 *    allowing the task to become ready (and also move to graph if it happens,
 *    which may be the case).
 *    Make sure that we always increment incoming before storing the link
 *    (see task_metadata::add_edge).
 */


// indep traits
template<>
struct dep_traits<etg_metadata, task_metadata, indep> {
    template<typename T>
    static void arg_issue( task_metadata * fr, indep<T> & obj,
			   typename indep<T>::dep_tags * sa ) {
	etg_metadata * md = obj.get_version()->get_metadata();
	md->lock();

	if( md->has_writers() )
	    md->link_writer( fr, sa );
	md->add_reader( fr, sa );

	md->unlock();
    }
    template<typename T>
    static bool arg_ini_ready( const indep<T> & obj ) {
	return !obj.get_version()->get_metadata()->has_writers();
    }
    template<typename T>
    static void arg_release( task_metadata * fr, indep<T> & obj,
			     typename indep<T>::dep_tags & sa  ) {
	etg_metadata * md = obj.get_version()->get_metadata();

	md->lock();
	// This check is necessary to make the overlapping of a link list
	// node on the deps chain for a dependent out/inout (sync_tags inout)
	// with the link list node on the readers list (input_tags rd) valid.
	// This swap has occured if a new writer is set in the metadata.
	// The writer cannot become null until we have executed.
	if( !md->has_writers() )
	    md->del_reader( fr, &sa );
	md->unlock();
    }
};

// output dependency traits
template<>
struct dep_traits<etg_metadata, task_metadata, outdep> {
    template<typename T>
    static void arg_issue( task_metadata * fr, outdep<T> & obj,
			  typename outdep<T>::dep_tags * sa ) {
	// Assuming we always rename if there are outstanding readers/writers,
	// we know that there are no prior writers or readers.
	etg_metadata * md = obj.get_version()->get_metadata();
	md->lock();
	md->add_writer( fr );
	md->unlock();
    }
    template<typename T>
    static bool arg_ini_ready( const outdep<T> & obj ) {
	assert( v->is_versionable() ); // enforced by applicators
	return true;
    }
    template<typename T>
    static void arg_release( task_metadata * fr, outdep<T> & obj,
			     typename outdep<T>::dep_tags & sa  ) {
	serial_dep_traits::arg_release( fr, obj );
    }
};

// inout dependency traits
template<>
struct dep_traits<etg_metadata, task_metadata, inoutdep> {
    template<typename T>
    static void arg_issue( task_metadata * fr, inoutdep<T> & obj,
			  typename inoutdep<T>::dep_tags * sa ) {
	serial_dep_traits::arg_issue( fr, obj, sa );
    }
    template<typename T>
    static bool arg_ini_ready( const inoutdep<T> & obj ) {
	return serial_dep_traits::arg_ini_ready( obj );
    }
    template<typename T>
    static void arg_release( task_metadata * fr, inoutdep<T> & obj,
			     typename inoutdep<T>::dep_tags & sa  ) {
	serial_dep_traits::arg_release( fr, obj );
    }
};

} // end of namespace obj

#endif // ETASKGRAPH_IMPL_H
