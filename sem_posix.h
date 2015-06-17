#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>


typedef struct {
  char name[ NAME_MAX-3 ];
  sem_t* sem;
} ipc_sem_handle;


static void ipc_sem_error( char* buf, size_t len, int code ) {
  if( len > 0 && strerror_r( code, buf, len ) != (int)0 ) {
    strncpy( buf, "unknown error", len-1 );
    buf[ len-1 ] = '\0';
  }
}


/* the format of sem names is restricted (at least if you want to be
 * portable), so this functions checks the input and makes such a
 * portable name. */
static int ipc_sem_make_name( char const* s, ipc_sem_handle* h ) {
  size_t ilen = strlen( s );
  if( ilen == 0 || ilen+2 > sizeof( h->name ) )
    return ENAMETOOLONG;
  if( strchr( s, '/' ) != NULL )
    return EINVAL;
  h->name[ 0 ] = '/';
  memcpy( h->name+1, s, ilen+1 );
  return 0;
}


static int ipc_sem_create( ipc_sem_handle* h, char const* name,
                           unsigned value ) {
  int rv, flags = O_RDWR|O_CREAT|O_EXCL;
  rv = ipc_sem_make_name( name, h );
  if( rv != 0 )
    return rv;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  h->sem = sem_open( h->name, flags, S_IRUSR|S_IWUSR, value );
  if( h->sem == SEM_FAILED )
    return errno;
  return 0;
}


static int ipc_sem_open( ipc_sem_handle* h, char const* name ) {
  int rv, flags = O_RDWR;
  rv = ipc_sem_make_name( name, h );
  if( rv != 0 )
    return rv;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  h->sem = sem_open( h->name, flags );
  if( h->sem == SEM_FAILED )
    return errno;
  return 0;
}


static int ipc_sem_inc( ipc_sem_handle* h ) {
  if( sem_post( h->sem ) < 0 )
    return errno;
  return 0;
}


static int ipc_sem_dec( ipc_sem_handle* h, int* could_dec, unsigned milliseconds ) {
  int rv = 0;
  if( could_dec == NULL ) { /* blocking */
    while( (rv = sem_wait( h->sem )) < 0 && errno == EINTR )
      ;
    if( rv < 0 )
      return errno;
  } else if( milliseconds == 0 ) { /* peeking */
    if( sem_trywait( h->sem ) < 0 ) {
      if( errno == EAGAIN ) {
        *could_dec = 0;
        return 0;
      }
      return errno;
    }
    *could_dec = 1;
  } else { /* waiting with a timeout */
    struct timespec timeout;
#if defined( _POSIX_TIMERS ) && _POSIX_TIMERS > 0
    if( clock_gettime( CLOCK_REALTIME, &timeout ) < 0 )
      return errno;
#else
    struct timeval tv;
    if( gettimeofday( &tv, NULL ) < 0 )
      return errno;
    timeout.tv_sec = tv.tv_sec;
    timeout.tv_nsec = tv.tv_usec * 1000;
#endif
    timeout.tv_sec += milliseconds / 1000;
    timeout.tv_nsec += milliseconds % 1000;
    if( timeout.tv_nsec >= 1000000000L ) {
      timeout.tv_sec++;
      timeout.tv_nsec -= 1000000000L;
    }
    while( (rv = sem_timedwait( h->sem, &timeout ) ) < 0 && errno == EINTR )
      ;
    if( rv < 0 ) {
      if( errno == ETIMEDOUT ) {
        *could_dec = 0;
        return 0;
      }
      return errno;
    }
    *could_dec = 1;
  }
  return 0;
}


static int ipc_sem_close( ipc_sem_handle* h ) {
  if( sem_close( h->sem ) < 0 )
    return errno;
  return 0;
}


static int ipc_sem_remove( ipc_sem_handle* h ) {
  if( sem_close( h->sem ) < 0 ) {
    int saved_errno = errno;
    sem_unlink( h->name );
    return saved_errno;
  }
  sem_unlink( h->name );
  return 0;
}
