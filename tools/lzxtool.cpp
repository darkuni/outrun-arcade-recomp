// lzxtool <in.lzx> <out.bin> <window_bits> <out_size>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <climits>
extern "C" {
#include <mspack.h>
#include <lzx.h>
}
struct memfile { mspack_system sys; unsigned char* buf; off_t size, off; };
static int mread(mspack_file* f, void* b, int n){ auto m=(memfile*)f; off_t t=std::min<off_t>(n,m->size-m->off); memcpy(b,m->buf+m->off,t); m->off+=t; return (int)t; }
static int mwrite(mspack_file* f, void* b, int n){ auto m=(memfile*)f; off_t t=std::min<off_t>(n,m->size-m->off); memcpy(m->buf+m->off,b,t); m->off+=t; return (int)t; }
static void* malloc_(mspack_system*, size_t n){ return calloc(n,1); }
static void free_(void* p){ free(p); }
static void copy_(void* s, void* d, size_t n){ memcpy(d,s,n); }
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<unsigned char> in(n); fread(in.data(),1,n,f); fclose(f);
  int wb=atoi(argv[3]); long outsz=atol(argv[4]); std::vector<unsigned char> out(outsz);
  mspack_system sys{}; sys.read=mread; sys.write=mwrite; sys.alloc=malloc_; sys.free=free_; sys.copy=copy_;
  memfile src{sys,in.data(),(off_t)n,0}, dst{sys,out.data(),(off_t)outsz,0};
  lzxd_stream* s=lzxd_init(&sys,(mspack_file*)&src,(mspack_file*)&dst,wb,0,0x8000,(off_t)outsz,0);
  int r=lzxd_decompress(s,(off_t)outsz); lzxd_free(s);
  printf("lzxd result %d, wrote %lld\n", r, (long long)dst.off);
  FILE* o=fopen(argv[2],"wb"); fwrite(out.data(),1,dst.off,o); fclose(o); return r;
}
