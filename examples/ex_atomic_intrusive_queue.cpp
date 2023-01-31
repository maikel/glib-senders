#include "glib-senders/atomic_intrusive_queue.hpp"

#include <cstdio>
#include <thread>

struct int_node {
  int value = 0;
  std::atomic<int_node*> next = nullptr;
};

int main() {
  int_node node1{1};
  int_node node2{2};
  gsenders::atomic_intrusive_queue<int_node, &int_node::next> queue{};
  std::thread t2{[&] {
    int_node* node = nullptr;
    for (int i = 0; i < 2; ++i) {
      while (!node) {
        node = queue.pop_front();
      }
      printf("%d\n", node->value);
      node = nullptr;
    }
  }};

  std::thread t1{[&] {
    queue.push_back(&node1);
    queue.push_back(&node2);
  }};

  t1.join();
  t2.join();
}