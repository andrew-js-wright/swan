// -*- c++ -*-
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

#ifndef WF_TASK_H
#define WF_TASK_H

#include "config.h"

#include <cassert>
#include <cstdint>

#include "platform.h"
#include "wf_frames.h"

#include "../util/pp_time.h"
#include "lock.h"

// make this into a template
class future {
    return_value result;
    bool finished;

    friend class stack_frame;

public:
    future() : finished( false ) { }

    void set_result( intptr_t r ) {
	result = r;
	// Store-store barrier in between, if needed on the architecture,
	// because we won't lock a future. Since the finished flag
	// is only moving from false to true, we can safely poll its state
	// but we need a store-store barrier to make sure that we see the
	// right value for result after seeing the finished flag move to true.
	finished = true;
    }

    void flag_result() {
	// Store-store barrier
	finished = true;
    }

    void reset() {
	// Store-load barrier
	finished = false;
	// Store-load barrier
    }

    template<typename TR>
    TR get_result() const {
	static_assert( sizeof(TR) <= sizeof(result),
		       "Return type must fit within result type" );
	assert( (int)finished == 1 || (int)finished == 0 );
	return result.get_value<TR>();
    }
    bool is_finished() volatile const { return finished; }
};

#if DBG_CONTINUATION
class dbg_continuation {
    intptr_t dbg_bp; // for debugging
    intptr_t dbg_sp;
    size_t dbg_cnt;
public:
    dbg_continuation() : dbg_bp( 0 ), dbg_sp( 0 ), dbg_cnt( 0 ) { }

    void save_continuation( intptr_t bp ) {
	dbg_sp = bp + intptr_t(2*sizeof(intptr_t));
	dbg_bp = *reinterpret_cast<intptr_t*>( bp );
	dbg_cnt++;
    }
    void check_continuation( intptr_t bp ) const {
	assert( dbg_sp == bp + intptr_t(2*sizeof(intptr_t)) );
	assert( dbg_bp == *reinterpret_cast<intptr_t*>( bp ) );
    }
    void clear_continuation() {
	dbg_sp = dbg_bp = 0;
    }
};
#else
class dbg_continuation {
public:
    dbg_continuation() { }

    void save_continuation( intptr_t bp ) { }
    void check_continuation( intptr_t bp ) const { }
    void clear_continuation() { }
};
#endif

//----------------------------------------------------------------------
// A class that maybe behaves as type T (limited functionality
// implemented so far), or is empty and fakes the behavior.
//----------------------------------------------------------------------
template<typename T, bool onoff>
class static_maybe {
    typedef static_maybe<T, onoff> type;

    T val;
public:
    static_maybe() : val() { }
    static_maybe( const T & val_ ) : val( val_ ) { }
    static_maybe( const type & m ) : val( m.val ) { }

    const type & operator = ( const type & m ) {
	val = m.val;
	return *this;
    }
    operator T () const { return val; }
};

template<typename T>
class static_maybe<T,false> {
    typedef static_maybe<T, false> type;
public:
    static_maybe() { }
    static_maybe( const T & val_ ) { }
    static_maybe( const type & m ) { }

    const type & operator = ( const type & m ) {
	return *this;
    }
    operator T () const { return (T)0; }
};

//----------------------------------------------------------------------
// task_data_t: task-specific information that we need, also when
// transforming from pending_frame to stack_frame
//----------------------------------------------------------------------
class task_data_t {
    // future * cresult;
    char * arg_buf;
    char * args;
    char * tags;

#if CRITICAL_PATH
  //work related variables
  unsigned long start_time;
  unsigned long end_time;

  unsigned long child_work_done;
  unsigned long work_done;

  unsigned long EST; //Store the earliest execution time of the task - Critical path analysis 
  unsigned long child_EST;

  unsigned long critical_duration; //critical path
  unsigned long child_critical_duration;
#endif
  // Decided it was a good idea to put the parent here as we need it regardless
  // of which type of frame we are accessing.
  task_data_t * parent;

  // This flag will be set if the task has been spawned, regardless of whether it
  // is run in parallel or not.

public:
  bool spawned;
  int task_depth;
  unsigned long id;

  // Create lock when to prevent multiple access of the parent
  cas_mutex mutex;

#if STORED_ANNOTATIONS
    size_t nargs;
#endif
    // Optimize finalization (part of ready check). Finalization requires us
    // to scan over all arguments and check if each argument was involved in
    // a reduction. Most often, this check is redundant. So skip the scan if
    // none of the arguments was used in a reduction; else do the scan (the
    // latter part could be optimized by precomputing which arguments require
    // finalization).
    bool req_fin;

public:

    task_data_t() { }
    ~task_data_t() {
	if( arg_buf )
	    delete[] arg_buf;
    }
    void initialize( size_t args_size, size_t tags_size, size_t fn_tags_size,
		     size_t nargs_, task_data_t * parent_, bool spawned_ ){
	fn_tags_size = (fn_tags_size+15) & ~15;
	arg_buf = new char[((args_size+15)&~15)+fn_tags_size+tags_size];
	args = &arg_buf[0];
	tags = &arg_buf[(args_size+15)&~15];
	tags += fn_tags_size;
#if STORED_ANNOTATIONS
	nargs = nargs_;
#endif
	req_fin = false;
	assert( (intptr_t(args) & 15) == 0 );
	assert( (intptr_t(tags) & 15) == 0 );
#if CRITICAL_PATH	
	critical_duration = 0;
	child_critical_duration = 0;
	start_time = pp_time();
	task_depth = 0;
	id = pp_time();
	spawned = spawned_;
	parent = parent_;
	work_done = 0;
#endif
    }
    void initialize( size_t args_size, size_t tags_size, size_t fn_tags_size,
		     char * end_of_stack, size_t nargs_, task_data_t * parent_, bool spawned_ ){
      arg_buf = 0;
#if STORED_ANNOTATIONS
	nargs = nargs_;
#endif
	// align to 16 bytes (x86_64 ABI)
	fn_tags_size = (fn_tags_size+15) & ~15;
	tags = reinterpret_cast<char *>(
	    intptr_t(end_of_stack-tags_size) & ~intptr_t(15) );
	args = reinterpret_cast<char *>(
	    intptr_t(tags-fn_tags_size-args_size) & ~intptr_t(15) );
	req_fin = false;
	assert( (intptr_t(args) & 15) == 0 );
	assert( (intptr_t(tags) & 15) == 0 );
#if CRITICAL_PATH	
	parent = parent_;
	critical_duration = 0;
	start_time = pp_time();
	task_depth = 0;
	id = pp_time();
	child_critical_duration = 0;
	work_done = 0;
	spawned = spawned_;
#endif
    }
  void initialize( task_data_t & data, bool spawned_ ) {
	arg_buf = data.arg_buf;
	args = data.args;
	tags = data.tags;
#if STORED_ANNOTATIONS
	nargs = data.nargs;
#endif
	req_fin = data.req_fin;
	data.arg_buf = 0;
	assert( (intptr_t(args) & 15) == 0 );
	assert( (intptr_t(tags) & 15) == 0 );
#if CRITICAL_PATH
	start_time = pp_time();
	id = data.id;
	critical_duration = data.critical_duration;
	child_critical_duration = data.child_critical_duration;
	work_done = data.work_done;
	spawned = spawned;
#endif
    }

    char * get_args_ptr() const { return args; }
    char * get_tags_ptr() const { return tags; }
#if STORED_ANNOTATIONS
    size_t get_num_args() const { return nargs; }
#endif

    size_t get_args_size() const { return tags - args; }

#if STORED_ANNOTATIONS
    void push_args() { }
    template<typename... Tn>
    void push_args( Tn... an ) {
	char * tgt = get_args_ptr(); // limit updates to local copy
	copy_args( tgt, an... );
    }
#endif

    void set_finalization_required() { req_fin = true; }
    bool is_finalization_required() const { return req_fin; }

public:
    const task_data_t & get_task_data() const { return *this; }
          task_data_t & get_task_data()       { return *this; }

#if CRITICAL_PATH
  void set_start_time() { start_time = pp_time(); }
  unsigned long get_start_time() { return start_time; }

  void set_end_time() { end_time = pp_time(); }
  unsigned long get_end_time() { return end_time; }

  // Getter and Setter for earliest execute time - Critical path analysis
  void set_est(unsigned long est) { EST = est; }
  unsigned long get_est() { return EST; }
  
  void set_child_est(unsigned long est) { child_EST = est; }
  unsigned long get_child_est() { return child_EST; }

  void set_work_done(unsigned long work) { work_done = work; }
  unsigned long get_work_done() { return work_done; }

  void set_child_work_done(unsigned long work) { child_work_done = work; }
  unsigned long get_child_work_done() { return child_work_done; }

  void set_critical_duration(unsigned long cd) { critical_duration = cd; }
  unsigned long get_critical_duration() { return critical_duration; }

  void set_child_critical_duration(unsigned long cd) { child_critical_duration = cd; }
  unsigned long get_child_critical_duration() { return child_critical_duration; }
  
  task_data_t * get_task_parent() { return parent; }

  bool get_spawned() { return spawned; }
#endif
};

//----------------------------------------------------------------------
// Argument type for internal tasks
//----------------------------------------------------------------------
struct internal_argument_type {
    char * args;
    char * tags;
    pending_frame * pnd;
    stack_frame * parent;
    pending_frame * next;

    inline internal_argument_type( pending_frame * pnd_,
				   const task_data_t & task_data )
	: args( task_data.get_args_ptr() ), tags( task_data.get_tags_ptr() ),
	  pnd( pnd_ ), parent( 0 ), next( 0 ) { }
};

//----------------------------------------------------------------------
// Delayed copy task
//----------------------------------------------------------------------
// By not implementing these traits functions, the compiler will
// generate an error message unless this struct is overridden.
template<typename ObjTy> // eg: obj::obj_instance
struct delayed_copy_traits {
    static char * get_address( ObjTy obj );
    static size_t get_size( ObjTy obj );
};

// The copy task
template<typename DstTy, typename SrcTy> // eg: obj::outdep and obj::indep
void delayed_copy_task( DstTy dst, SrcTy src ) {
    size_t size = delayed_copy_traits<DstTy>::get_size( dst );
    assert( size == delayed_copy_traits<SrcTy>::get_size( src )
	    && "Copying different-sized objects" );
    memcpy( delayed_copy_traits<DstTy>::get_address( dst ),
	    delayed_copy_traits<SrcTy>::get_address( src ), size );
}

// Creating a copy task and scheduling it.
// Implementation is in wf_interface.h
template<typename DstTy, typename SrcTy> // eg: obj::outdep and obj::indep
void create_copy_task( DstTy dst, SrcTy src );

//----------------------------------------------------------------------
// Parallel reduction task
//----------------------------------------------------------------------
// @Notes
//   The seq argument is there to force the task to wait until the reduction
//   phase is over. This is necessary because we don't know what threads will be
//   used for the reduction, and so we don't know which shadow objects will be
//   used. As such, we cannot do dependency tracking on the shadow objects
//   because the parallel reduction tasks are inserted in the task graph
//   before it is decided what shadow objects are used.
//   Each task may be passed the same argument at most once, however. Therefore
//   we have 2 versions, in case the seq argument equals the accumulator.
// AccumTy = inoutdep<Monad::value_type>
// InTy = indep<Monad::value_type>
#include "debug.h"
template<typename Monad, typename AccumTy, typename InTy>
void reduce_pair_task( AccumTy accum, InTy in ) {
    Monad::reduce( accum.get_ptr(), in.get_ptr() );
    // errs() << "reduce: accum=" << accum.get_version()
    // << " in=" << in.get_version() << "\n";
}

// Creating a parallel reduction task and scheduling it.
// Implementation is in wf_interface.h
template<typename Monad, typename AccumTy, typename ReductionTy>
void create_parallel_reduction_task( AccumTy accum, ReductionTy * reduc );

#endif // WF_TASK_H
