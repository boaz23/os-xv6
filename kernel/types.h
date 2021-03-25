typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;

struct perf {
  int ctime;
  int ttime;
  int stime;
  int retime;
  int rutime;
  #ifdef FLOAT_ALLOWED
  float bursttime;
  #elif FLOAT_SIMULATE_BY_INT
  uint32 bursttime;
  #endif
};
