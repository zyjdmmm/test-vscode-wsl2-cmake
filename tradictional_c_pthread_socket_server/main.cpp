#include <iostream>
#include <memory>

int main() {
    // 创建独占智能指针
    std::unique_ptr<int> ptr = std::make_unique<int>(200);
    std::cout << "Before move: ptr value = " << *ptr << std::endl;

    // 转移所有权
    std::unique_ptr<int> ptr_moved = std::move(ptr);
    std::cout << "After move: ptr_moved value = " << *ptr_moved << std::endl;
    std::cout << "Original ptr is null? " << (!ptr ? "Yes" : "No") << std::endl;

    return 0;
}