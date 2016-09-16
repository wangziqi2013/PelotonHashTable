
// This file tests whether STL container vector calls constructor for 
// each element in the array even if nothing is inserted

#include <vector>
#include <cstdio>

class A {
 public:
  int a;
  A() { printf("Default ctor!\n"); }
  A(int pa) {
    printf("A ctor! %d\n", pa);
    a = pa;

    return;
  }

  A(const A &other) {
    printf("A c'ctor! %d\n", other.a);
    a = other.a;

    return;
  }
};

int main() {
  std::vector<A> av;
  av.resize(8);

  printf("=========\n");

  std::vector<A> av2;

  // If constructor is always called then we will see 3 constructor calls
  // since the second call to push_back() extends the hash table
  av2.push_back(A{1});
  av2.push_back(A{2});

  printf("Capacity = %lu\n", av2.capacity());

  return 0;
}
