
#include <cstdlib>
#include <cstdio>

class A {
  void *a;
 public:
  ~A() { printf("A d'tor\n"); free(a); }
  A() { a = malloc(4); }
};

int main() {
  A *a = new A[123];

  printf("before pointer = %lu\n", *((unsigned long *)a - 1));

  delete[] a;

  return 0;
}
