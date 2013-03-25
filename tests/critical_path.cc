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
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <iostream>

#include <unistd.h>

#include "wf_interface.h"

using namespace obj;
using obj::object_t;
using obj::indep;
using obj::outdep;
using obj::inoutdep;


void task_in( indep<int> in, int n ) {
    usleep( int(n)*1000 );
    errs() << "task_in n=" << int(n) << '\n';
}

void task_out( outdep<int> out, int n ) {
    usleep( int(n)*1000 );
    errs() << "task_out n=" << int(n) << '\n';
}

void task_inout( inoutdep<int> inout, int n ) {
    usleep( int(n)*1000 );
    errs() << "task_inout n=" << int(n) << '\n';
}

#if OBJECT_COMMUTATIVITY
void task_cinout( cinoutdep<int> inout, int n ) {
    usleep( int(n)*1000 );
    errs() << "task_cinout n=" << int(n) << '\n';
}
#endif

void chill( int n ){
  sleep ( int(n) );
}

void chill_recursive( int n ){
  if( n == 0 )
    return;
   
  spawn( chill_recursive, (n - 1) );
  call( chill, n );
  ssync();
}

void sleep_test( int n ) {
  call ( chill, n );
}

void recursive_test( int n ) {
  call( chill, n );
  chill_recursive( n );
}

void nest_test( int n, int p ) {
  
  int i;

  for ( i = 0; i < p; i ++){
    spawn ( sleep_test, n );
  }

  ssync ();

}

int main( int argc, char * argv[] ) {
  
  if( argc < 2 ) {
    std::cout << "expected: " << argv[0] << " <seconds to sleep> [number of spawns]" ;
    return 1;
  }

  int n, p;

  n = atoi( argv[1] );

  if( argc == 3 ) {
    p = atoi( argv[2] );
  }
  else{
    p = 1;
  }

  //nest_test( n, p );
  run ( nest_test, n, p );

  return 0;
}
