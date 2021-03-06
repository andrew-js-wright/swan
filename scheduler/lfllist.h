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
#ifndef LFLLIST_H
#define LFLLIST_H

#include <iterator>

#include "platform.h"
#include "lock.h"
#include "wf_task.h"
#include "wf_task.h"

// This lock-free linked-list implementation is a simplified implementation
// because insertion occurs only at the head of the list and there can be
// only one thread deleting elements by design of the run-time system.

// T contains a field volatile T * next;
// T contains a function bool is_ready() const;
template<typename T>
class lfllist {
    T * head;
public:
    lfllist() : head( 0 ) {
	assert( (intptr_t(&head) & (sizeof(head)-1)) == 0
		&& "Alignment of head pointer violated" );
    }

    bool empty() const { return head == 0; }

    void prepend( T * elm ) {
	while( 1 ) {
	    T * old_head = head;
	    elm->next = old_head;
	    if( __sync_bool_compare_and_swap( &head, old_head, elm ) )
		break;
	}
    }

    T * get_ready() {
	bool retry = false;
	do {
	    T * p = head, * q = 0;
	    if( !p )
		return 0;
	    if( p->is_ready() ) {
		if( __sync_bool_compare_and_swap( &head, p, p->next ) )
		    return p;
		p = head;
	    } else {
		q = p;
		p = p->next;
	    }
	    for( ; p; q=p, p=p->next ) {
		if( p->is_ready() ) {
		    if( q ) {
			q->next = p->next;
			return p;
		    } else {
			if( __sync_bool_compare_and_swap( &head, p, p->next ) )
			    return p;
			retry = true;
		    }
		}
	    }
	} while( retry );
	return 0;
    }
};

// TODO: split out get_depth() and is_ready() on pending_frame/full_frame type
//       and prev/next accessors on link_metadata type. Define latter in tickets.h
//       and former should remain here, but based on template (e.g. by defining
//       a stack_frame concept so that we can conditionally instantiate it).
//       Also, this would require the dl_list class to use two traits
//       specializations, depending on the desired functionality. Too much work
//       for the same assembly code ;-)
template<typename T>
struct dl_list_traits {
    typedef T ValueType;

    static size_t get_depth( T const * elm );
    static bool is_ready( T const * elm );

    static void set_prev( T * elm, T * prev );
    static T * get_prev( T const * elm );
    static void set_next( T * elm, T * next );
    static T * get_next( T const * elm );

    static ValueType * get_value( T const * elm );
};

template<typename T, typename IT, typename V>
class dl_list_iterator;

template<typename T>
class dl_list {
    typedef dl_list_traits<T> traits;

    T * head;
    T * tail;

public:
    template<typename U, typename IU, typename V>
    friend class dl_list_iterator;

    typedef dl_list_iterator<T, T, typename traits::ValueType> iterator;
    typedef dl_list_iterator<T, T const, typename traits::ValueType> const_iterator;

public:
    dl_list() : head( 0 ), tail( 0 ) { }

    bool empty() const { return head == 0; }

    void dump(size_t d) {
	for( T *p=head; p; p=traits::get_next(p) ) {
	    errs() << "     task " << p
		   << " at depth " << d
		   << " ready? "
		   << ( traits::is_ready(p) ? "yes\n" : "no\n" );
	}
    }

    void clear() { head = tail = 0; }

    void erase( iterator it ) {
	T * elm = it.cur;

	if( head == elm ) {
	    head = traits::get_next( elm );
	    if( head == 0 )
		tail = 0;
	} else if( tail == elm ) {
	    tail = traits::get_prev( tail );
	    traits::set_next( tail, 0 );
	} else {
	    // These checks on the validity of prev and next in elm are here
	    // because we need to support the use of erasing an element from
	    // the list that is not there, eg head and tail are null and we
	    // unlink elm. This is for the single-generation taskgraph
	    // (etaskgraph.h).
	    if( traits::get_prev(elm) )
		traits::set_next( traits::get_prev(elm), traits::get_next(elm) );
	    if( traits::get_next(elm) )
		traits::set_prev( traits::get_next(elm), traits::get_prev(elm) );
	}
    }
    void erase_half( iterator it ) {
	T * elm = it.cur;

	if( head == elm ) {
	    head = traits::get_next( elm );
	    if( head == 0 )
		tail = 0;
	} else if( tail == elm ) {
	    assert( 0 );
	    tail = traits::get_prev( tail );
	    traits::set_next( tail, 0 );
	}
	// don't do anything if it doesn't match the head or tail
    }

    void push( T * elm ) { prepend( elm ); }
    void push_back( T * elm ) { push( elm ); }

    void push_back( const_iterator * elm ) { push_back( elm.cur ); }

    void prepend( T * elm ) { // is an append operation but allow subst of class
	traits::set_next( elm, 0 );
	if( tail ) {
	    assert( head );
	    traits::set_prev( elm, tail );
	    traits::set_next( tail, elm );
	    tail = elm;
	} else {
	    assert( !head );
	    traits::set_prev( elm, 0 );
	    traits::set_next( elm, 0 );
	    head = tail = elm;
	}
    }

    T * get_ready() {
	if( empty() )
	    return 0;

	if( traits::is_ready( head ) ) {
	    T * ret = head;
	    head = traits::get_next( head );
	    if( head == 0 )
		tail = 0;
	    else
		traits::set_prev( head, 0 );
	    return ret;
	} else {
	    for( T *p=traits::get_next(head); p; p=traits::get_next(p) ) {
		if( traits::is_ready( p ) ) {
		    // Simplify control flow using end dummies.
		    traits::set_next( traits::get_prev(p), traits::get_next(p) );
		    if( traits::get_next( p ) == 0 )
			tail = traits::get_prev( p );
		    else
			traits::set_prev( traits::get_next(p), traits::get_prev(p) );
		    return p;
		}
	    }
	    return 0;
	}
    }

    T * front() const { return head; }
    void pop() {
	assert( !empty() && "List may not be empty on a pop" );
	head = traits::get_next( head );
	if( head == 0 )
	    tail = 0;
	else
	    traits::set_prev( head, 0 );
    }

public:
    iterator begin() { return iterator( head ); }
    iterator end() { return iterator( 0 ); }
    const_iterator begin() const { return const_iterator( head ); }
    const_iterator end() const { return const_iterator( 0 ); }
};

template<typename T, typename IT, typename V>
class dl_list_iterator
    : public std::iterator< std::forward_iterator_tag, T > {
    typedef dl_list_traits<T> traits;
    friend class dl_list<T>;

    IT * cur;

public:
    dl_list_iterator( IT * cur_ ) : cur( cur_ ) { }

public:
    bool operator == ( dl_list_iterator<T, IT, V> it ) const {
	return cur == it.cur;
    }
    bool operator != ( dl_list_iterator<T, IT, V> it ) const {
	return ! operator == ( it );
    }
    dl_list_iterator<T, IT, V> & operator ++ () {
	cur = traits::get_next( cur );
	return *this;
    }
    V * operator * () const { return traits::get_value( cur ); }

    operator dl_list_iterator<T, T const, typename traits::ValueType>() const { return *this; }

    T * get_node() const { return const_cast<T *>( cur ); }
};

// ----------------------------------------------------------------------
// Doubly linked list with head node.
// ----------------------------------------------------------------------
template<typename T, typename IT, typename V>
class dl_head_list_iterator;

template<typename T>
class dl_head_list {
    typedef dl_list_traits<T> traits;

    T head;

public:
    template<typename U, typename IU, typename V>
    friend class dl_head_list_iterator;

    typedef dl_head_list_iterator<T, T, typename traits::ValueType> iterator;
    typedef dl_head_list_iterator<T, T const, typename traits::ValueType> const_iterator;

public:
    dl_head_list() {
	traits::set_next( &head, &head );
	traits::set_prev( &head, &head );
    }

    bool empty() const { return traits::get_next( &head ) == &head; }

    void dump(size_t d) {
	for( T *p=traits::get_next(&head); p != &head; p=traits::get_next(p) ) {
	    errs() << "     task " << p
		   << " at depth " << d
		   << " ready? "
		   << ( traits::is_ready(p) ? "yes\n" : "no\n" );
	}
    }

    void clear() {
	traits::set_next( &head, &head );
	traits::set_prev( &head, &head );
    }

    void erase( iterator it ) {
	T * elm = it.cur;
	traits::set_next( traits::get_prev(elm), traits::get_next(elm) );
	traits::set_prev( traits::get_next(elm), traits::get_prev(elm) );
    }

    void push( T * elm ) { prepend( elm ); }
    void push_back( T * elm ) { push( elm ); }

    void push_back( const_iterator * elm ) { push_back( elm.cur ); }

    void prepend( T * elm ) { // is an append operation but allow subst of class
	traits::set_next( traits::get_prev( &head ), elm );
	traits::set_prev( elm, traits::get_prev( &head ) );
	traits::set_next( elm, &head );
	traits::set_prev( &head, elm );
    }

    T * front() const { return empty() ? 0 : traits::get_next( &head ); }
    void pop() {
	assert( !empty() && "List may not be empty on a pop" );
	T * elm = traits::get_next( &head );
	traits::set_next( traits::get_prev(elm), traits::get_next(elm) );
	traits::set_prev( traits::get_next(elm), traits::get_prev(elm) );
    }

public:
    iterator begin() { return iterator( traits::get_next( &head ) ); }
    iterator end() { return iterator( &head ); }
    const_iterator begin() const {
	return const_iterator( traits::get_next( &head ) );
    }
    const_iterator end() const { return const_iterator( &head ); }
};

template<typename T, typename IT, typename V>
class dl_head_list_iterator
    : public std::iterator< std::forward_iterator_tag, T > {
    typedef dl_list_traits<T> traits;
    friend class dl_head_list<T>;

    IT * cur;

public:
    dl_head_list_iterator( IT * cur_ ) : cur( cur_ ) { }

public:
    bool operator == ( dl_head_list_iterator<T, IT, V> it ) const {
	return cur == it.cur;
    }
    bool operator != ( dl_head_list_iterator<T, IT, V> it ) const {
	return ! operator == ( it );
    }
    dl_head_list_iterator<T, IT, V> & operator ++ () {
	cur = traits::get_next( cur );
	return *this;
    }
    V * operator * () const { return traits::get_value( cur ); }

    operator dl_head_list_iterator<T, T const, typename traits::ValueType>() const { return *this; }

    T * get_node() const { return const_cast<T *>( cur ); }
};


// ----------------------------------------------------------------------
// Singly-linked list
// ----------------------------------------------------------------------
template<typename T>
struct sl_list_traits {
    typedef T ValueType;

    static void set_next( T * elm, T * next );
    static T * get_next( T const * elm );

    static ValueType * get_value( T const * elm );
};

template<typename T, typename IT, typename V>
class sl_list_iterator;

template<typename T>
class sl_list {
    typedef sl_list_traits<T> traits;

    T * head;
    T * tail;

public:
    template<typename U, typename IU, typename V>
    friend class sl_list_iterator;

    typedef sl_list_iterator<T, T, typename traits::ValueType> iterator;
    typedef sl_list_iterator<T, T const, typename traits::ValueType> const_iterator;

public:
    sl_list() : head( 0 ), tail( 0 ) { }

    bool empty() const { return head == 0; }

    void dump(size_t d) {
	for( T *p=head; p; p=traits::get_next(p) ) {
	    errs() << "     task " << p
		   << " at depth " << d
		   << " ready? "
		   << ( traits::is_ready(p) ? "yes\n" : "no\n" );
	}
    }

    void clear() { head = tail = 0; }

    void push_back( const_iterator * elm ) { push_back( elm.cur ); }
    void push_back( T * elm ) {
	traits::set_next( elm, 0 );
	if( tail ) {
	    assert( head );
	    traits::set_next( tail, elm );
	    tail = elm;
	} else {
	    assert( !head );
	    traits::set_next( elm, 0 );
	    head = tail = elm;
	}
    }

    T * front() const { return head; }
    void pop() {
	assert( !empty() && "List may not be empty on a pop" );
	head = traits::get_next( head );
	if( head == 0 )
	    tail = 0;
    }

public:
    iterator begin() { return iterator( head ); }
    iterator end() { return iterator( 0 ); }
    const_iterator begin() const { return const_iterator( head ); }
    const_iterator end() const { return const_iterator( 0 ); }
};

template<typename T, typename IT, typename V>
class sl_list_iterator
    : public std::iterator< std::forward_iterator_tag, T > {
    typedef sl_list_traits<T> traits;
    friend class sl_list<T>;

    IT * cur;

public:
    sl_list_iterator( IT * cur_ ) : cur( cur_ ) { }

public:
    bool operator == ( sl_list_iterator<T, IT, V> it ) const {
	return cur == it.cur;
    }
    bool operator != ( sl_list_iterator<T, IT, V> it ) const {
	return ! operator == ( it );
    }
    sl_list_iterator<T, IT, V> & operator ++ () {
	cur = traits::get_next( cur );
	return *this;
    }
    V * operator * () const { return traits::get_value( cur ); }

    operator sl_list_iterator<T, T const, typename traits::ValueType>() const { return *this; }

    T * get_node() const { return const_cast<T *>( cur ); }
};

// ----------------------------------------------------------------------
// locked_dl_list: a doubly linked list with a lock
// ----------------------------------------------------------------------
template<typename T>
class locked_dl_list {
    dl_list<T> list;
    cas_mutex mutex;

public:
    locked_dl_list() { }

    void lock() { mutex.lock(); }
    bool try_lock() { return mutex.try_lock(); }
    void unlock() { mutex.unlock(); }

    bool empty() const { return list.empty(); }

    void dump( size_t d ) { list.dump( d ); }
    void clear() { list.clear(); }

    void push( T * elm ) { list.push( elm ); }
    void push_back( T * elm ) { list.push_back( elm ); }
    void prepend( T * elm ) { list.prepend( elm ); }

    T * get_ready() { return list.get_ready(); }
};

template<typename T>
class hashed_list {
    typedef locked_dl_list<T> list_t;
    static const size_t SIZE = 2048;
    list_t table[SIZE];
    size_t min_occ, max_occ;
    cas_mutex occ_mutex;

public:
    hashed_list() : min_occ( 0 ), max_occ( 0 ) { }

#ifdef UBENCH_HOOKS
    void reset() {
	min_occ = 0;
	max_occ = 0;
    }
#endif

    void prepend( T * elm ) { // is an append operation but allow subst of class
	size_t d = dl_list_traits<T>::get_depth( elm );
	size_t h = hash(d);
	list_t & list = table[h];
	list.lock();
	list.prepend( elm );
	list.unlock();

	update_bounds( h );
	// atomic_min( &min_occ, h );
	// atomic_max( &max_occ, h );
    }

    T * get_ready() { return scan(); }

    T * get_internal_ready() { return scan(); }

    T * get_ready( size_t prev_depth ) {
	if( !prev_depth ) // For some reason, this helps performance...
	    return scan();

	size_t h0, h1;
	if( T * ret = probe( table[h0 = hash(prev_depth)] ) ) {
#if TRACING
	    errs() << "h0: find ready " << ret << " depth " << prev_depth << " in " << this << '\n';
#endif
	    return ret;
	}
	if( T * ret = probe( table[h1 = hash(prev_depth+1)] ) ) {
#if TRACING
	    errs() << "h1: find ready " << ret << " depth "
		   << (prev_depth+1) << " in " << this << '\n';
#endif
	    return ret;
	}
	if( T * ret = scan( h0, h1 ) )
	    return ret;

/*
	errs() << "Dumping task graph at " << this << " [" << min_occ
	       << "," << max_occ << "]\n";
	for( size_t i=0; i < SIZE; ++i )
	    table[i].dump(i);
*/

	return 0;
    }

private:
    static size_t hash( size_t d ) {
	return d % SIZE;
    }

    T * probe( list_t & list ) {
	if( list.empty() )
	    return 0;

	list.lock();
	T * ret = list.get_ready();
	list.unlock();
	return ret;
    }

#if 0
    void update_bounds() {
	volatile size_t pm;
	errs() << " ... update_bounds ...\n";
	while( table[min_occ].empty() && min_occ < max_occ ) {
	    do {
		pm = min_occ;
		if( !table[pm].empty() )
		    break;
	    } while( !__sync_bool_compare_and_swap( &min_occ, pm, pm+1 ) );
	}
	while( table[max_occ].empty() && min_occ < max_occ ) {
	    do {
		pm = max_occ;
		if( pm <= min_occ )
		    break;
		if( !table[pm].empty() )
		    break;
	    } while( !__sync_bool_compare_and_swap( &max_occ, pm, pm-1 ) );
	}
    }
#else
    void update_bounds( size_t h ) {
	if( h < min_occ || h > max_occ ) {
	    occ_mutex.lock();
	    min_occ = h < min_occ ? h : min_occ;
	    max_occ = h > max_occ ? h : max_occ;
	    occ_mutex.unlock();
	}
    }
    void update_bounds() {
	if( table[min_occ].empty() ) {
	    occ_mutex.lock();
	    size_t i;
	    // not i=min_occ+1 because of race with late lock
	    for( i=min_occ; i < max_occ; ++i )
		if( !table[i].empty() )
		    break;
	    min_occ = i;
	    occ_mutex.unlock();
	}
	if( table[max_occ].empty() ) {
	    occ_mutex.lock();
	    size_t i;
	    // not i=max_occ-1 because of race with late lock
	    for( i=max_occ; i > min_occ; --i )
		if( !table[i].empty() )
		    break;
	    max_occ = i;
	    occ_mutex.unlock();
	}
    }
#endif

    T * scan() {
	// errs() << "scan from " << min_occ << " to " << max_occ << "\n";
	for( size_t i=min_occ; i <= max_occ; ++i ) {
	    if( T * ret = probe( table[i] ) ) {
		// printf( "A0 %ld-%ld @%ld\n", min_occ, max_occ, i );
		update_bounds();
		// printf( "A1 %ld-%ld @%ld\n", min_occ, max_occ, i );
#if TRACING
		errs() << "scan: find ready " << ret << " depth " << i << " in " << this << '\n';
#endif
		return ret;
	    }
	}
	// if( table[min_occ].empty() )
	    // atomic_max( &min_occ, min_occ+1 );
	// if( table[max_occ].empty() && max_occ > 0 )
	    // atomic_min( &max_occ, max_occ-1 );
	// printf( "A0 %ld-%ld\n", min_occ, max_occ );
	update_bounds();
	// printf( "A1 %ld-%ld\n", min_occ, max_occ );
	return 0;
    }
    T * scan( size_t h0, size_t h1 ) {
	for( size_t i=min_occ; i <= max_occ; ++i ) {
	    if( i == h0 || i == h1 )
		continue;
	    if( T * ret = probe( table[i] ) ) {
		// printf( "F%ld %ld-%ld @%ld\n", h0, min_occ, max_occ, i );
#if TRACING
		errs() << "bscan: find ready " << ret << " depth " << i << " in " << this << '\n';
#endif
		return ret;
	    }
	}
	// if( table[min_occ].empty() )
	    // atomic_max( &min_occ, min_occ+1 );
	// if( table[max_occ].empty() && max_occ > 0 )
	    // atomic_min( &max_occ, max_occ-1 );
	// printf( "F0 %ld %ld-%ld\n", h0, min_occ, max_occ );
	update_bounds();
	// printf( "F1 %ld %ld-%ld\n", h0, min_occ, max_occ );
	return 0;
    }

    static void atomic_min( volatile size_t * min, size_t v ) {
	if( v < *min ) {
	    volatile size_t pm = *min;
	    do {
		pm = *min;
		if( v >= pm )
		    break;
	    } while( !__sync_bool_compare_and_swap( min, pm, v ) );
	}
    }
    static void atomic_max( volatile size_t * max, size_t v ) {
	if( v > *max ) {
	    volatile size_t pm = *max;
	    do {
		pm = *max;
		if( v <= pm )
		    break;
	    } while( !__sync_bool_compare_and_swap( max, pm, v ) );
	}
    }
};

#endif // LFLLIST_H
