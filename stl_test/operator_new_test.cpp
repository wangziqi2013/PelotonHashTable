
#include <cstdio>

class A {
 public:
  A() { printf("A Ctor!\n"); }
  ~A() { printf("A Dtor\n"); }
};

int main() {
  A *a = new A[8];
  delete[] a;

  printf("==========\n");

  a = new A[8];
  delete a;

  return 0;
}
